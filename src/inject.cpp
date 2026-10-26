#include "inject.hpp"

#ifndef NOMINMAX
#   define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace inject
{
    namespace
    {
        // ---- shared state --------------------------------------------------
        //
        // The UI thread only reads what these mutexed copies expose; the worker
        // thread owns everything else. No state is shared unlocked.

        std::mutex lock;
        std::vector< entry > ring;             // newest at the back, capped
        std::atomic< bool > busy{ false };
        std::atomic< int > status_code{ 0 };   // 0 = Ready, 1 = Working, 2 = Failed
        std::atomic< bool > loaded{ false };
        unsigned int done_epoch = 0;           // guarded by `lock`
        bool done_success = false;             // guarded by `lock`
        std::string finished_target;           // guarded by `lock`
        std::string finished_dll;              // guarded by `lock`

        std::thread worker;

        struct file_log_settings
        {
            bool enabled = false;
            int retention_days = 14;
        } file_log;                            // guarded by `lock`

        struct file_log_watch                  // UI thread only
        {
            bool was_enabled = false;
            int retention_days = -1;
            bool pruned = false;
        } file_watch;

        constexpr std::size_t ring_cap = 240;

        const char* const level_names[] = { "INFO", "OK", "WARN", "ERROR" };

        // ---- small helpers -------------------------------------------------

        std::wstring to_wide( const char* text )
        {
            if ( !text || !*text )
                return { };
            const int needed = MultiByteToWideChar( CP_UTF8, 0, text, -1, nullptr, 0 );
            if ( needed <= 0 )
                return { };
            std::wstring out( static_cast< std::size_t >( needed - 1 ), L'\0' );
            MultiByteToWideChar( CP_UTF8, 0, text, -1, out.data( ), needed );
            return out;
        }

        std::string to_utf8( const wchar_t* text )
        {
            if ( !text || !*text )
                return { };
            const int needed = WideCharToMultiByte( CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr );
            if ( needed <= 0 )
                return { };
            std::string out( static_cast< std::size_t >( needed - 1 ), '\0' );
            WideCharToMultiByte( CP_UTF8, 0, text, -1, out.data( ), needed, nullptr, nullptr );
            return out;
        }

        std::string base_name( const char* path )
        {
            const char* slash = std::strrchr( path, '\\' );
            const char* slash2 = std::strrchr( path, '/' );
            const char* cut = slash > slash2 ? slash : slash2;
            return cut ? cut + 1 : path;
        }

        std::string system_error_text( DWORD gle )
        {
            wchar_t buffer[256] = { };
            FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM, nullptr, gle, 0, buffer, 256, nullptr );
            std::string text = to_utf8( buffer );
            while ( !text.empty( ) && ( text.back( ) == '\r' || text.back( ) == '\n' ) )
                text.pop_back( );
            return text;
        }

        void win32_fail( const char* what, char* why, std::size_t why_size )
        {
            const DWORD gle = GetLastError( );
            _snprintf_s( why, why_size, _TRUNCATE, "%s (%lu: %s)", what,
                         static_cast< unsigned long >( gle ), system_error_text( gle ).c_str( ) );
        }

        // ---- log file ------------------------------------------------------

        std::string log_file_path( )
        {
            wchar_t module[ MAX_PATH ] = { };
            GetModuleFileNameW( nullptr, module, MAX_PATH );
            std::string path = to_utf8( module );
            return path.substr( 0, path.find_last_of( "\\/" ) + 1 ) + "injector.log";
        }

        // Days-from-civil so Log Retention can compare YYYY-MM-DD dates exactly.
        long days_from_civil( int y, int m, int d )
        {
            y -= m <= 2;
            const long era = ( y >= 0 ? y : y - 399 ) / 400;
            const long yoe = y - era * 400;
            const long mp = ( m > 2 ? m - 3 : m + 9 );
            const long doy = ( 153 * mp + 2 ) / 5 + d - 1;
            return era * 146097 + ( yoe * 365 + yoe / 4 - yoe / 100 + doy );
        }

        long today_number( )
        {
            std::time_t now = std::time( nullptr );
            std::tm tm { };
            localtime_s( &tm, &now );
            return days_from_civil( tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday );
        }

        void prune_log_file( int retention_days )
        {
            const std::string path = log_file_path( );
            FILE* file = nullptr;
            if ( fopen_s( &file, path.c_str( ), "rb" ) != 0 || !file )
                return;

            std::string content;
            char chunk[4096];
            std::size_t got = 0;
            while ( ( got = std::fread( chunk, 1, sizeof chunk, file ) ) > 0 )
                content.append( chunk, got );
            std::fclose( file );

            const long floor_day = today_number( ) - retention_days;
            std::string kept;
            std::size_t at = 0;
            while ( at < content.size( ) )
            {
                const std::size_t eol = content.find( '\n', at );
                const std::size_t end = eol == std::string::npos ? content.size( ) : eol + 1;
                const std::string line = content.substr( at, end - at );
                int y = 0, m = 0, d = 0;
                // Our lines start with "[YYYY-MM-DD"; anything else always survives.
                if ( sscanf_s( line.c_str( ), "[%d-%d-%d", &y, &m, &d ) != 3
                     || days_from_civil( y, m, d ) >= floor_day )
                    kept += line;
                at = end;
            }

            FILE* out = nullptr;
            if ( fopen_s( &out, path.c_str( ), "wb" ) == 0 && out )
            {
                std::fwrite( kept.data( ), 1, kept.size( ), out );
                std::fclose( out );
            }
        }

        // ---- log core ------------------------------------------------------

        void emit_line( int level, const char* time_text, const char* text )
        {
            file_log_settings cfg;
            {
                std::lock_guard< std::mutex > guard( lock );
                cfg = file_log;
            }
            if ( !cfg.enabled )
                return;

            std::time_t now = std::time( nullptr );
            std::tm tm { };
            localtime_s( &tm, &now );

            FILE* file = nullptr;
            if ( fopen_s( &file, log_file_path( ).c_str( ), "ab" ) != 0 || !file )
                return;
            std::fprintf( file, "[%04d-%02d-%02d %s] [%s] %s\n",
                          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, time_text,
                          level_names[level & 3], text );
            std::fclose( file );
        }

        void log( int level, const char* fmt, ... )
        {
            std::time_t now = std::time( nullptr );
            std::tm tm { };
            localtime_s( &tm, &now );

            entry line { };
            line.level = level;
            _snprintf_s( line.time, sizeof line.time, _TRUNCATE, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec );

            va_list args;
            va_start( args, fmt );
            _vsnprintf_s( line.text, sizeof line.text, _TRUNCATE, fmt, args );
            va_end( args );

            {
                std::lock_guard< std::mutex > guard( lock );
                if ( ring.size( ) >= ring_cap )
                    ring.erase( ring.begin( ), ring.begin( ) + static_cast< std::ptrdiff_t >( ring.size( ) - ring_cap + 1 ) );
                ring.push_back( line );
            }
            emit_line( level, line.time, line.text );
        }

        // ---- target helpers ------------------------------------------------

        bool enable_debug_privilege( )
        {
            HANDLE token = nullptr;
            if ( !OpenProcessToken( GetCurrentProcess( ), TOKEN_ADJUST_PRIVILEGES, &token ) )
                return false;

            LUID luid { };
            bool ok = false;
            if ( LookupPrivilegeValueW( nullptr, SE_DEBUG_NAME, &luid ) )
            {
                TOKEN_PRIVILEGES privileges { };
                privileges.PrivilegeCount = 1;
                privileges.Privileges[0].Luid = luid;
                privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                AdjustTokenPrivileges( token, FALSE, &privileges, sizeof privileges, nullptr, nullptr );
                ok = GetLastError( ) == ERROR_SUCCESS;
            }
            CloseHandle( token );
            return ok;
        }

        bool file_exists( const char* path )
        {
            const std::wstring wide = to_wide( path );
            const DWORD attr = GetFileAttributesW( wide.c_str( ) );
            return attr != INVALID_FILE_ATTRIBUTES && ( attr & FILE_ATTRIBUTE_DIRECTORY ) == 0;
        }

        // A thread exit code is a DWORD, so it cannot carry an x64 HMODULE.
        // Obtain the full base address from the target's module snapshot instead.
        uintptr_t loaded_module_base( HANDLE process, const std::wstring& dll, char* why, std::size_t why_size )
        {
            const DWORD pid = GetProcessId( process );
            if ( !pid )
            {
                win32_fail( "could not query the target process ID", why, why_size );
                return 0;
            }
            HANDLE snapshot = INVALID_HANDLE_VALUE;
            for ( int attempt = 0; attempt < 8; ++attempt )
            {
                snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE, pid );
                if ( snapshot != INVALID_HANDLE_VALUE || GetLastError( ) != ERROR_BAD_LENGTH )
                    break;
            }
            if ( snapshot == INVALID_HANDLE_VALUE )
            {
                win32_fail( "could not enumerate the target modules", why, why_size );
                return 0;
            }

            const bool has_path = dll.find_first_of( L"\\/" ) != std::wstring::npos;
            MODULEENTRY32W entry{ };
            entry.dwSize = sizeof entry;
            uintptr_t base = 0;
            if ( Module32FirstW( snapshot, &entry ) )
            {
                do
                {
                    if ( _wcsicmp( has_path ? entry.szExePath : entry.szModule, dll.c_str( ) ) == 0 )
                    {
                        base = reinterpret_cast< uintptr_t >( entry.modBaseAddr );
                        break;
                    }
                } while ( Module32NextW( snapshot, &entry ) );
            }
            const DWORD enumeration_error = GetLastError( );
            CloseHandle( snapshot );
            if ( !base )
            {
                if ( enumeration_error != ERROR_NO_MORE_FILES && enumeration_error != ERROR_SUCCESS )
                {
                    SetLastError( enumeration_error );
                    win32_fail( "could not read the target module list", why, why_size );
                }
                else
                    _snprintf_s( why, why_size, _TRUNCATE, "loaded module '%s' was not found in the target module list", to_utf8( dll.c_str( ) ).c_str( ) );
            }
            return base;
        }

        // A remote LoadLibraryW call shared by every method that needs a module
        // inside the target. Returns the module's base address, 0 on failure.
        uintptr_t remote_load_library( HANDLE process, const std::wstring& dll, char* why, std::size_t why_size )
        {
            void* mem = VirtualAllocEx( process, nullptr, ( dll.size( ) + 1 ) * sizeof( wchar_t ),
                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
            if ( !mem )
            {
                win32_fail( "VirtualAllocEx failed", why, why_size );
                return 0;
            }

            uintptr_t result = 0;
            if ( !WriteProcessMemory( process, mem, dll.c_str( ), ( dll.size( ) + 1 ) * sizeof( wchar_t ), nullptr ) )
            {
                win32_fail( "WriteProcessMemory failed", why, why_size );
            }
            else
            {
                const FARPROC entry = GetProcAddress( GetModuleHandleW( L"kernel32.dll" ), "LoadLibraryW" );
                HANDLE thread = CreateRemoteThread( process, nullptr, 0,
                                                    reinterpret_cast< LPTHREAD_START_ROUTINE >( entry ), mem, 0, nullptr );
                if ( !thread )
                {
                    win32_fail( "CreateRemoteThread failed", why, why_size );
                }
                else
                {
                    if ( WaitForSingleObject( thread, 15000 ) != WAIT_OBJECT_0 )
                    {
                        win32_fail( "LoadLibraryW did not finish in time", why, why_size );
                    }
                    else
                    {
                        DWORD code = 0;
                        if ( !GetExitCodeThread( thread, &code ) )
                            win32_fail( "could not query the loader thread result", why, why_size );
                        else if ( !code )
                            _snprintf_s( why, why_size, _TRUNCATE, "LoadLibraryW returned NULL in the target for '%s'",
                                         to_utf8( dll.c_str( ) ).c_str( ) );
                        else
                            result = loaded_module_base( process, dll, why, why_size );
                    }
                    CloseHandle( thread );
                }
            }
            VirtualFreeEx( process, mem, 0, MEM_RELEASE );
            return result;
        }

        // Reads a remote module's export table and resolves one function, following
        // forwarders ("KERNELBASE.Xxx") like a real loader would.
        uintptr_t remote_get_proc( HANDLE process, uintptr_t module, std::string func, char* why, std::size_t why_size )
        {
            for ( int depth = 0; depth < 6; ++depth )
            {
                std::uint8_t header[0x1000];
                if ( !ReadProcessMemory( process, reinterpret_cast< void* >( module ), header, sizeof header, nullptr ) )
                {
                    win32_fail( "could not read a remote module header", why, why_size );
                    return 0;
                }
                const IMAGE_DOS_HEADER* dos = reinterpret_cast< IMAGE_DOS_HEADER* >( header );
                if ( dos->e_magic != IMAGE_DOS_SIGNATURE
                     || static_cast< std::size_t >( dos->e_lfanew ) > sizeof header - sizeof( IMAGE_NT_HEADERS64 ) )
                {
                    _snprintf_s( why, why_size, _TRUNCATE, "remote module has unreadable headers" );
                    return 0;
                }
                const IMAGE_NT_HEADERS64* nt = reinterpret_cast< IMAGE_NT_HEADERS64* >( header + dos->e_lfanew );
                if ( nt->Signature != IMAGE_NT_SIGNATURE )
                {
                    _snprintf_s( why, why_size, _TRUNCATE, "remote module has unreadable headers" );
                    return 0;
                }

                const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
                if ( dir.VirtualAddress == 0 || dir.Size == 0 )
                {
                    _snprintf_s( why, why_size, _TRUNCATE, "remote module exports no functions" );
                    return 0;
                }

                std::vector< std::uint8_t > exp( dir.Size );
                if ( !ReadProcessMemory( process, reinterpret_cast< void* >( module + dir.VirtualAddress ),
                                         exp.data( ), dir.Size, nullptr ) )
                {
                    win32_fail( "could not read a remote export table", why, why_size );
                    return 0;
                }

                const IMAGE_EXPORT_DIRECTORY* table = reinterpret_cast< IMAGE_EXPORT_DIRECTORY* >( exp.data( ) );
                const DWORD* names = reinterpret_cast< const DWORD* >( exp.data( ) + ( table->AddressOfNames - dir.VirtualAddress ) );
                const WORD* ords = reinterpret_cast< const WORD* >( exp.data( ) + ( table->AddressOfNameOrdinals - dir.VirtualAddress ) );
                const DWORD* funcs = reinterpret_cast< const DWORD* >( exp.data( ) + ( table->AddressOfFunctions - dir.VirtualAddress ) );

                DWORD index = 0xFFFFFFFF;
                if ( func[0] == '#' )   // ordinal form "#123" from a forwarder
                {
                    const DWORD ordinal = static_cast< DWORD >( std::atoi( func.c_str( ) + 1 ) );
                    if ( ordinal < table->Base || ordinal >= table->Base + table->NumberOfFunctions )
                    {
                        _snprintf_s( why, why_size, _TRUNCATE, "ordinal %lu out of range", static_cast< unsigned long >( ordinal ) );
                        return 0;
                    }
                    index = ordinal - table->Base;
                }
                else
                {
                    for ( DWORD n = 0; n < table->NumberOfNames; ++n )
                    {
                        const DWORD name_rva = names[n];
                        if ( name_rva < dir.VirtualAddress || name_rva >= dir.VirtualAddress + dir.Size )
                            continue;
                        if ( std::strcmp( reinterpret_cast< const char* >( exp.data( ) + ( name_rva - dir.VirtualAddress ) ), func.c_str( ) ) == 0 )
                        {
                            index = ords[n];
                            break;
                        }
                    }
                    if ( index == 0xFFFFFFFF )
                    {
                        _snprintf_s( why, why_size, _TRUNCATE, "import '%s' not found in the target's copy", func.c_str( ) );
                        return 0;
                    }
                }

                const DWORD func_rva = funcs[index];
                if ( func_rva >= dir.VirtualAddress && func_rva < dir.VirtualAddress + dir.Size )
                {
                    // Forwarder: "MODULE.FUNC" inside the export range -> resolve again.
                    const std::string forward = reinterpret_cast< const char* >( exp.data( ) + ( func_rva - dir.VirtualAddress ) );
                    const std::size_t dot = forward.find( '.' );
                    if ( dot == std::string::npos )
                    {
                        _snprintf_s( why, why_size, _TRUNCATE, "malformed forwarder for '%s'", func.c_str( ) );
                        return 0;
                    }
                    const uintptr_t next_mod = remote_load_library( process, to_wide( ( forward.substr( 0, dot ) + ".dll" ).c_str( ) ),
                                                                    why, why_size );
                    if ( !next_mod )
                        return 0;
                    module = next_mod;
                    func = forward.substr( dot + 1 );
                    continue;
                }
                return module + func_rva;
            }
            _snprintf_s( why, why_size, _TRUNCATE, "forwarder chain too deep for '%s'", func.c_str( ) );
            return 0;
        }

        // ---- PE inspection -------------------------------------------------

        struct pe_info
        {
            bool valid = false;
            bool is_dll = false;
            bool machine_x64 = false;
        };

        pe_info inspect_pe( const std::vector< std::uint8_t >& image )
        {
            pe_info info { };
            if ( image.size( ) < sizeof( IMAGE_DOS_HEADER ) )
                return info;
            const IMAGE_DOS_HEADER* dos = reinterpret_cast< const IMAGE_DOS_HEADER* >( image.data( ) );
            if ( dos->e_magic != IMAGE_DOS_SIGNATURE )
                return info;
            if ( image.size( ) < static_cast< std::size_t >( dos->e_lfanew ) + sizeof( IMAGE_NT_HEADERS64 ) )
                return info;
            const IMAGE_NT_HEADERS64* nt = reinterpret_cast< const IMAGE_NT_HEADERS64* >( image.data( ) + dos->e_lfanew );
            if ( nt->Signature != IMAGE_NT_SIGNATURE )
                return info;
            info.valid = true;
            info.machine_x64 = nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64;
            info.is_dll = ( nt->FileHeader.Characteristics & IMAGE_FILE_DLL ) != 0;
            return info;
        }

        bool load_file( const char* path, std::vector< std::uint8_t >& out )
        {
            FILE* file = nullptr;
            if ( fopen_s( &file, path, "rb" ) != 0 || !file )
                return false;
            std::fseek( file, 0, SEEK_END );
            const long size = std::ftell( file );
            std::fseek( file, 0, SEEK_SET );
            if ( size <= 0 )
            {
                std::fclose( file );
                return false;
            }
            out.resize( static_cast< std::size_t >( size ) );
            const bool ok = std::fread( out.data( ), 1, out.size( ), file ) == out.size( );
            std::fclose( file );
            return ok;
        }

        // ---- method: LoadLibrary -------------------------------------------

        bool do_load_library( HANDLE process, const std::wstring& dll, bool verbose, char* why, std::size_t why_size )
        {
            const uintptr_t module = remote_load_library( process, dll, why, why_size );
            if ( !module )
                return false;
            if ( verbose )
                log( info, "Module base: 0x%llX", static_cast< unsigned long long >( module ) );
            return true;
        }

        // ---- method: thread hijack -----------------------------------------
        //
        // Suspends the first live thread, redirects it through a small stub that
        // calls LoadLibraryW, stores the result into a sentinelled slot and jumps
        // back to the original RIP. Volatile registers ride the thread's real
        // stack; the original execution context continues untouched.

        bool do_thread_hijack( HANDLE process, const options& o, bool verbose, char* why, std::size_t why_size )
        {
            HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, 0 );
            if ( snap == INVALID_HANDLE_VALUE )
            {
                win32_fail( "could not list the target threads", why, why_size );
                return false;
            }

            DWORD tid = 0;
            THREADENTRY32 te { };
            te.dwSize = sizeof te;
            for ( BOOL has = Thread32First( snap, &te ); has && !tid; has = Thread32Next( snap, &te ) )
                if ( te.th32OwnerProcessID == o.pid )
                    tid = te.th32ThreadID;
            CloseHandle( snap );

            if ( !tid )
            {
                _snprintf_s( why, why_size, _TRUNCATE, "the target has no hijackable thread" );
                return false;
            }
            if ( verbose )
                log( info, "Hijacking thread %lu", static_cast< unsigned long >( tid ) );

            HANDLE thread = OpenThread( THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, tid );
            if ( !thread )
            {
                win32_fail( "could not open the target thread", why, why_size );
                return false;
            }

            bool ok = false;
            bool suspended = false;
            bool context_taken = false;
            CONTEXT ctx { };
            void* mem = nullptr;

            for ( ;; )
            {
                if ( SuspendThread( thread ) == static_cast< DWORD >( -1 ) )
                {
                    win32_fail( "SuspendThread failed", why, why_size );
                    break;
                }
                suspended = true;

                ctx.ContextFlags = CONTEXT_FULL;
                if ( !GetThreadContext( thread, &ctx ) )
                {
                    win32_fail( "could not read the thread context", why, why_size );
                    break;
                }
                context_taken = true;

                const std::wstring dll = to_wide( o.dll_path );
                const std::size_t path_bytes = ( dll.size( ) + 1 ) * sizeof( wchar_t );
                constexpr std::size_t stub_size = 0xA0;

                mem = VirtualAllocEx( process, nullptr, stub_size + path_bytes,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
                if ( !mem )
                {
                    win32_fail( "VirtualAllocEx failed", why, why_size );
                    break;
                }
                void* const slot_addr = reinterpret_cast< std::uint8_t* >( mem ) + 0x80;
                void* const path_addr = reinterpret_cast< std::uint8_t* >( mem ) + stub_size;

                // -- stub bytes (position-independent, x64) --
                std::uint8_t stub[0x70] = { };
                std::size_t n = 0;
                auto b1 = [ & ]( std::uint8_t v ) { stub[n++] = v; };
                auto b8 = [ & ]( std::uint64_t v ) { std::memcpy( stub + n, &v, 8 ); n += 8; };

                const std::uint8_t pushes[] = { 0x50, 0x51, 0x52, 0x41, 0x50, 0x41,
                                                0x51, 0x41, 0x52, 0x41, 0x53 };
                for ( std::uint8_t op : pushes )
                    b1( op );                                            // push rax rcx rdx r8 r9 r10 r11
                b1( 0x48 ); b1( 0x83 ); b1( 0xEC ); b1( 0x28 );          // sub rsp,0x28
                b1( 0x48 ); b1( 0xB9 );                                  // mov rcx, path_addr
                b8( reinterpret_cast< std::uint64_t >( path_addr ) );
                const FARPROC load_library = GetProcAddress( GetModuleHandleW( L"kernel32.dll" ), "LoadLibraryW" );
                b1( 0x48 ); b1( 0xB8 );                                  // mov rax, LoadLibraryW
                b8( reinterpret_cast< std::uint64_t >( load_library ) );
                b1( 0xFF ); b1( 0xD0 );                                  // call rax
                b1( 0x48 ); b1( 0xBA );                                  // mov rdx, slot_addr
                b8( reinterpret_cast< std::uint64_t >( slot_addr ) );
                b1( 0x48 ); b1( 0x89 ); b1( 0x02 );                      // mov [rdx], rax
                b1( 0x48 ); b1( 0x83 ); b1( 0xC4 ); b1( 0x28 );          // add rsp,0x28
                const std::uint8_t pops[] = { 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59,
                                              0x41, 0x58, 0x5A, 0x59, 0x58 };
                for ( std::uint8_t op : pops )
                    b1( op );                                            // pop r11 r10 r9 r8 rdx rcx rax
                b1( 0x48 ); b1( 0xB8 ); b8( ctx.Rip );                   // mov rax, original RIP
                b1( 0xFF ); b1( 0xE0 );                                  // jmp rax

                const std::uint64_t sentinel = 0xFFFFFFFFFFFFFFFFULL;
                if ( !WriteProcessMemory( process, mem, stub, stub_size, nullptr )
                     || !WriteProcessMemory( process, path_addr, dll.c_str( ), path_bytes, nullptr )
                     || !WriteProcessMemory( process, slot_addr, &sentinel, sizeof sentinel, nullptr ) )
                {
                    win32_fail( "could not write the hijack stub", why, why_size );
                    break;
                }

                CONTEXT redirected = ctx;
                redirected.Rip = reinterpret_cast< DWORD64 >( mem );
                if ( !SetThreadContext( thread, &redirected ) )
                {
                    win32_fail( "could not redirect the thread", why, why_size );
                    break;
                }
                ResumeThread( thread );
                suspended = false;

                std::uint64_t result = sentinel;
                for ( int spin = 0; spin < 100 && result == sentinel; ++spin )
                {
                    ReadProcessMemory( process, slot_addr, &result, sizeof result, nullptr );
                    if ( result == sentinel )
                        Sleep( 50 );
                }
                if ( result == sentinel )
                {
                    _snprintf_s( why, why_size, _TRUNCATE,
                                 "the hijacked thread never reported back (it may be blocked inside the loader)" );
                    break;
                }
                if ( result == 0 )
                {
                    _snprintf_s( why, why_size, _TRUNCATE,
                                 "LoadLibraryW returned NULL inside the target (not a valid 64-bit DLL?)" );
                    break;
                }
                if ( verbose )
                    log( info, "Module base: 0x%llX", static_cast< unsigned long long >( result ) );
                ok = true;
                break;
            }

            if ( !ok && context_taken )
                SetThreadContext( thread, &ctx );   // put the original RIP back
            if ( suspended )
                ResumeThread( thread );
            if ( mem )
                VirtualFreeEx( process, mem, 0, MEM_RELEASE );
            CloseHandle( thread );
            return ok;
        }

        // ---- method: manual map --------------------------------------------
        //
        // Maps the DLL into the target without registering it with the OS
        // loader: headers + sections are laid out, relocations rebased, imports
        // resolved against the target's own module space and DllMain invoked
        // through a bootstrap stub. Dependencies themselves load through the
        // normal loader, exactly like a real import table would.

        bool do_manual_map( HANDLE process, const std::vector< std::uint8_t >& image,
                            bool verbose, char* why, std::size_t why_size )
        {
            const IMAGE_DOS_HEADER* dos = reinterpret_cast< const IMAGE_DOS_HEADER* >( image.data( ) );
            const IMAGE_NT_HEADERS64* nt = reinterpret_cast< const IMAGE_NT_HEADERS64* >( image.data( ) + dos->e_lfanew );
            const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION( nt );
            const std::size_t image_size = nt->OptionalHeader.SizeOfImage;

            void* const remote = VirtualAllocEx( process, nullptr, image_size,
                                                 MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
            if ( !remote )
            {
                win32_fail( "VirtualAllocEx (image) failed", why, why_size );
                return false;
            }

            bool ok = false;
            std::uint8_t* stub_mem = nullptr;
            std::vector< std::uint64_t > tls_callbacks;   // declared before the gotos
            std::vector< std::uint8_t > local( image_size, 0 );
            std::memcpy( local.data( ), image.data( ), nt->OptionalHeader.SizeOfHeaders );

            for ( WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s )
            {
                const std::size_t count = section[s].SizeOfRawData;
                if ( count == 0 )
                    continue;
                const std::size_t avail = image.size( ) - section[s].PointerToRawData;
                std::memcpy( local.data( ) + section[s].VirtualAddress,
                             image.data( ) + section[s].PointerToRawData,
                             count < avail ? count : avail );
            }

            const std::int64_t delta = reinterpret_cast< std::uint64_t >( remote ) - nt->OptionalHeader.ImageBase;

            // relocations
            int patched = 0;
            if ( delta != 0 )
            {
                const IMAGE_DATA_DIRECTORY& rel_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
                std::size_t at = rel_dir.VirtualAddress;
                const std::size_t end = at + rel_dir.Size;
                while ( at + sizeof( IMAGE_BASE_RELOCATION ) <= end && end <= local.size( ) )
                {
                    const IMAGE_BASE_RELOCATION* block = reinterpret_cast< const IMAGE_BASE_RELOCATION* >( local.data( ) + at );
                    if ( block->SizeOfBlock < sizeof( IMAGE_BASE_RELOCATION ) || at + block->SizeOfBlock > end )
                        break;
                    const int entries = static_cast< int >( ( block->SizeOfBlock - sizeof( IMAGE_BASE_RELOCATION ) ) / sizeof( WORD ) );
                    const WORD* fixups = reinterpret_cast< const WORD* >( block + 1 );
                    for ( int e = 0; e < entries; ++e )
                    {
                        const WORD f = fixups[e];
                        if ( ( f >> 12 ) == IMAGE_REL_BASED_DIR64 )
                        {
                            const std::size_t slot = block->VirtualAddress + ( f & 0xFFF );
                            if ( slot + 8 <= local.size( ) )
                            {
                                *reinterpret_cast< std::int64_t* >( local.data( ) + slot ) += delta;
                                ++patched;
                            }
                        }
                    }
                    at += block->SizeOfBlock;
                }
            }
            if ( verbose && patched > 0 )
                log( info, "Rebased image (%d relocations)", patched );

            // imports
            int import_count = 0;
            if ( nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress != 0 )
            {
                const IMAGE_IMPORT_DESCRIPTOR* desc = reinterpret_cast< const IMAGE_IMPORT_DESCRIPTOR* >(
                    local.data( ) + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress );
                for ( ; desc->Name != 0 && ok == false; ++desc )
                {
                    const char* mod_name = reinterpret_cast< const char* >( local.data( ) + desc->Name );
                    if ( verbose )
                        log( info, "Resolving import: %s", mod_name );
                    const uintptr_t remote_mod = remote_load_library( process, to_wide( mod_name ), why, why_size );
                    if ( !remote_mod )
                        goto finish;

                    const DWORD thunk_rva = desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk;
                    const std::uint64_t* thunk = reinterpret_cast< const std::uint64_t* >( local.data( ) + thunk_rva );
                    std::uint64_t* iat = reinterpret_cast< std::uint64_t* >( local.data( ) + desc->FirstThunk );
                    for ( std::size_t t = 0; thunk[t] != 0; ++t )
                    {
                        uintptr_t addr = 0;
                        if ( thunk[t] & 0x8000000000000000ULL )
                        {
                            char ordinal_name[16];
                            _snprintf_s( ordinal_name, sizeof ordinal_name, _TRUNCATE, "#%llu",
                                         static_cast< unsigned long long >( thunk[t] & 0xFFFF ) );
                            addr = remote_get_proc( process, remote_mod, ordinal_name, why, why_size );
                        }
                        else
                        {
                            const IMAGE_IMPORT_BY_NAME* ibn =
                                reinterpret_cast< const IMAGE_IMPORT_BY_NAME* >( local.data( ) + thunk[t] );
                            addr = remote_get_proc( process, remote_mod, ibn->Name, why, why_size );
                        }
                        if ( !addr )
                            goto finish;
                        iat[t] = addr;
                        ++import_count;
                    }
                }
            }
            if ( verbose )
                log( info, "Imports resolved (%d functions)", import_count );

            // TLS callbacks: after relocation the table already holds remote
            // addresses because its entries are covered by DIR64 fixups.
            {
                const IMAGE_DATA_DIRECTORY& tls_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
                if ( tls_dir.VirtualAddress != 0 )
                {
                    const IMAGE_TLS_DIRECTORY64* tls =
                        reinterpret_cast< const IMAGE_TLS_DIRECTORY64* >( local.data( ) + tls_dir.VirtualAddress );
                    const std::uint64_t* cb = reinterpret_cast< const std::uint64_t* >(
                        tls->AddressOfCallBacks - nt->OptionalHeader.ImageBase );
                    while ( *cb != 0 && tls_callbacks.size( ) < 64 )
                        tls_callbacks.push_back( *cb++ );
                }
            }

            if ( !WriteProcessMemory( process, remote, local.data( ), image_size, nullptr ) )
            {
                win32_fail( "could not write the mapped image", why, why_size );
                goto finish;
            }

            // per-section page protection, like the real loader applies
            for ( WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s )
            {
                const DWORD c = section[s].Characteristics;
                const bool r = ( c & IMAGE_SCN_MEM_READ ) != 0;
                const bool x = ( c & IMAGE_SCN_MEM_EXECUTE ) != 0;
                const bool w = ( c & IMAGE_SCN_MEM_WRITE ) != 0;
                DWORD protect = PAGE_NOACCESS;
                if ( x )
                    protect = w ? PAGE_EXECUTE_READWRITE : ( r ? PAGE_EXECUTE_READ : PAGE_EXECUTE );
                else if ( w )
                    protect = PAGE_READWRITE;
                else if ( r )
                    protect = PAGE_READONLY;
                DWORD old = 0;
                VirtualProtectEx( process,
                                  reinterpret_cast< std::uint8_t* >( remote ) + section[s].VirtualAddress,
                                  section[s].Misc.VirtualSize, protect, &old );
            }

            // bootstrap stub: sub rsp / TLS cbs / DllMain / ExitThread(result)
            stub_mem = static_cast< std::uint8_t* >(
                VirtualAllocEx( process, nullptr, 0x100, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE ) );
            if ( !stub_mem )
            {
                win32_fail( "VirtualAllocEx (stub) failed", why, why_size );
                goto finish;
            }
            {
                std::uint8_t stub[0x80] = { };
                std::size_t n = 0;
                auto b1 = [ & ]( std::uint8_t v ) { stub[n++] = v; };
                auto b8 = [ & ]( std::uint64_t v ) { std::memcpy( stub + n, &v, 8 ); n += 8; };

                b1( 0x48 ); b1( 0x83 ); b1( 0xEC ); b1( 0x28 );          // sub rsp,0x28
                b1( 0x48 ); b1( 0xB9 );                                  // mov rcx, image base
                b8( reinterpret_cast< std::uint64_t >( remote ) );
                b1( 0xBA ); b1( 0x01 ); b1( 0x00 ); b1( 0x00 ); b1( 0x00 );   // mov edx, 1 (DLL_PROCESS_ATTACH)
                b1( 0x4D ); b1( 0x31 ); b1( 0xC0 );                      // xor r8,r8
                for ( std::uint64_t cb : tls_callbacks )
                {
                    b1( 0x48 ); b1( 0xB8 ); b8( cb );                    // mov rax, tls callback
                    b1( 0xFF ); b1( 0xD0 );                              // call rax
                }
                b1( 0x48 ); b1( 0xB8 );                                  // mov rax, entry point
                b8( reinterpret_cast< std::uint64_t >( remote ) + nt->OptionalHeader.AddressOfEntryPoint );
                b1( 0xFF ); b1( 0xD0 );                                  // call rax
                b1( 0x48 ); b1( 0x83 ); b1( 0xC4 ); b1( 0x28 );          // add rsp,0x28
                b1( 0x89 ); b1( 0xC1 );                                  // mov ecx, eax
                const FARPROC exit_thread = GetProcAddress( GetModuleHandleW( L"kernel32.dll" ), "ExitThread" );
                b1( 0x48 ); b1( 0xB8 );                                  // mov rax, ExitThread
                b8( reinterpret_cast< std::uint64_t >( exit_thread ) );
                b1( 0xFF ); b1( 0xE0 );                                  // jmp rax (never returns)

                if ( !WriteProcessMemory( process, stub_mem, stub, n, nullptr ) )
                {
                    win32_fail( "could not write the bootstrap stub", why, why_size );
                    goto finish;
                }
            }

            {
                HANDLE thread = CreateRemoteThread( process, nullptr, 0,
                                                    reinterpret_cast< LPTHREAD_START_ROUTINE >( stub_mem ),
                                                    nullptr, 0, nullptr );
                if ( !thread )
                {
                    win32_fail( "could not start the mapped DLL", why, why_size );
                    goto finish;
                }
                const DWORD wait = WaitForSingleObject( thread, 15000 );
                DWORD code = 0;
                if ( wait != WAIT_OBJECT_0 )
                    _snprintf_s( why, why_size, _TRUNCATE, "the mapped DLL did not initialize in time" );
                else if ( !GetExitCodeThread( thread, &code ) || code == 0 )
                    _snprintf_s( why, why_size, _TRUNCATE, "DllMain returned FALSE (the DLL refused DLL_PROCESS_ATTACH)" );
                else
                    ok = true;
                CloseHandle( thread );
            }

            finish:
            if ( stub_mem )
                VirtualFreeEx( process, stub_mem, 0, MEM_RELEASE );
            if ( !ok )
                VirtualFreeEx( process, remote, 0, MEM_RELEASE );
            else if ( verbose )
                log( info, "Module base: 0x%llX",
                     static_cast< unsigned long long >( reinterpret_cast< std::uintptr_t >( remote ) ) );
            return ok;
        }

        // ---- run -------------------------------------------------------------

        const char* const method_names[] = { "LoadLibrary", "Manual Map", "Thread Hijack" };

        void run( options o )
        {
            // temporary crash trace: markers land in crash_dbg.txt
            auto mark = []( const char* tag ) {
                FILE* m = nullptr;
                if ( fopen_s( &m, "C:\\Users\\Null!\\Downloads\\DLL-Injector\\test\\crash_dbg.txt", "ab" ) == 0 && m )
                {
                    std::fprintf( m, "%u %s\n", static_cast< unsigned >( GetTickCount( ) ), tag );
                    std::fclose( m );
                }
            };
            mark( "run-enter" );
            status_code.store( 1 );
            loaded.store( false );

            log( info, "Starting injection (%s) -> %s (PID %u)",
                 method_names[static_cast< int >( o.how ) % 3], o.name, o.pid );

            bool success = false;
            char why[512] = { };

            for ( ;; )
            {
                if ( !o.pid )
                {
                    log( error, "No target selected: pick a process in the Process List first" );
                    break;
                }
                if ( !o.dll_path[0] )
                {
                    log( error, "No DLL selected: pick one with Browse first" );
                    break;
                }
                if ( !file_exists( o.dll_path ) )
                {
                    log( error, "DLL not found: %s", o.dll_path );
                    break;
                }
                if ( !o.target_x64 )
                {
                    log( error, "Unsupported target: '%s' (PID %u) is 32-bit (x86); this injector build only supports 64-bit (x64) processes",
                         o.name, o.pid );
                    break;
                }

                std::vector< std::uint8_t > image;
                if ( !load_file( o.dll_path, image ) )
                {
                    log( error, "Could not read '%s'", o.dll_path );
                    break;
                }
                const pe_info pe = inspect_pe( image );
                if ( !pe.valid )
                {
                    log( error, "'%s' is not a valid PE file", base_name( o.dll_path ).c_str( ) );
                    break;
                }
                if ( !pe.is_dll )
                {
                    log( error, "'%s' is not flagged as a DLL in its PE header", base_name( o.dll_path ).c_str( ) );
                    break;
                }
                if ( !pe.machine_x64 )
                {
                    log( error, "Architecture mismatch: '%s' is 32-bit (x86) but '%s' is a 64-bit process",
                         base_name( o.dll_path ).c_str( ), o.name );
                    break;
                }
                if ( o.verbose )
                    log( info, "DLL: %s (64-bit, %u bytes)", base_name( o.dll_path ).c_str( ),
                         static_cast< unsigned int >( image.size( ) ) );

                if ( o.delay_ms > 0 )
                {
                    if ( o.verbose )
                        log( info, "Waiting %dms (Injection Delay)", o.delay_ms );
                    mark( "pre-sleep" );
                    Sleep( static_cast< DWORD >( o.delay_ms ) );
                    mark( "post-sleep" );
                }
                mark( "after-sleep" );

                const bool debug_ok = enable_debug_privilege( );
                mark( "after-debugpriv" );
                if ( !debug_ok )
                    log( warn, "Debug privileges unavailable: protected processes may fail to open (try running as Administrator)" );

                HANDLE process = OpenProcess( PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE
                                              | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, o.pid );
                mark( "after-openprocess" );
                if ( !process )
                {
                    win32_fail( "OpenProcess failed", why, sizeof why );
                    if ( GetLastError( ) == ERROR_ACCESS_DENIED
                         || std::strstr( why, "denied" ) != nullptr )
                        log( warn, "Hint: run the injector as Administrator, or pick a process owned by your session" );
                    break;
                }
                if ( o.verbose )
                    log( info, "Target handle open (PID %u)", o.pid );

                const int attempts = o.auto_retry ? 2 : 1;
                for ( int attempt = 1; attempt <= attempts && !success; ++attempt )
                {
                    why[0] = '\0';
                    if ( attempt > 1 )
                        log( warn, "Retrying (Auto Retry On Fail), attempt %d of %d", attempt, attempts );

                    mark( "before-method" );
                    switch ( o.how )
                    {
                        case method::load_library:
                            success = do_load_library( process, to_wide( o.dll_path ), o.verbose, why, sizeof why );
                            break;
                        case method::manual_map:
                            success = do_manual_map( process, image, o.verbose, why, sizeof why );
                            break;
                        case method::thread_hijack:
                            success = do_thread_hijack( process, o, o.verbose, why, sizeof why );
                            break;
                    }
                    mark( "after-method" );
                }
                CloseHandle( process );
                break;
            }
            mark( "after-loop" );

            if ( success )
                log( ok, "Injection successful: '%s' loaded into %s (PID %u)",
                     base_name( o.dll_path ).c_str( ), o.name, o.pid );
            else
                log( error, "Injection failed%s%s", why[0] ? ": " : "", why );

            {
                std::lock_guard< std::mutex > guard( lock );
                ++done_epoch;
                done_success = success;
                finished_target = o.name;
                finished_dll = base_name( o.dll_path );
            }
            mark( "completion-written" );

            status_code.store( success ? 0 : 2 );
            loaded.store( success );
            if ( o.sound_effects )
                MessageBeep( success ? MB_ICONASTERISK : MB_ICONHAND );
            busy.store( false );
        }
    }

    bool running( )
    {
        return busy.load( );
    }

    bool start( const options& o )
    {
        if ( busy.load( ) )
            return false;
        if ( worker.joinable( ) )
            worker.join( );
        busy.store( true );
        worker = std::thread( run, o );
        // Detach: completion is published through locked counters/strings, so the
        // main thread never joins. A global joinable thread at static teardown
        // calls std::terminate (0xC0000409) the instant the app closes mid-run.
        worker.detach( );
        return true;
    }

    std::vector< entry > snapshot( )
    {
        std::lock_guard< std::mutex > guard( lock );
        return ring;
    }

    void clear_log( )
    {
        std::lock_guard< std::mutex > guard( lock );
        ring.clear( );
    }

    void configure_file_log( bool enabled, int retention_days )
    {
        {
            std::lock_guard< std::mutex > guard( lock );
            file_log.enabled = enabled;
            file_log.retention_days = retention_days;
        }
        if ( enabled && ( !file_watch.was_enabled || file_watch.retention_days != retention_days || !file_watch.pruned ) )
        {
            prune_log_file( retention_days );
            file_watch.pruned = true;
        }
        file_watch.was_enabled = enabled;
        file_watch.retention_days = retention_days;
    }

    int status( )
    {
        return status_code.load( );
    }

    bool dll_loaded( )
    {
        return loaded.load( );
    }

    completion last_completion( )
    {
        std::lock_guard< std::mutex > guard( lock );
        completion out { };
        out.epoch = done_epoch;
        out.success = done_success;
        return out;
    }

    const char* last_target( )
    {
        std::lock_guard< std::mutex > guard( lock );
        static std::string cached;
        cached = finished_target;
        return cached.c_str( );
    }

    const char* last_dll( )
    {
        std::lock_guard< std::mutex > guard( lock );
        static std::string cached;
        cached = finished_dll;
        return cached.c_str( );
    }
}
