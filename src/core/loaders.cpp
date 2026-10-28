#include "loaders.hpp"
#include "diagnostics.hpp"
#include "modules.hpp"
#include <tlhelp32.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace inject::detail
{
    namespace
    {
        thread_local ULONGLONG deadline = 0;
        thread_local std::stop_token stop_token;
        thread_local bool uncertain = false;
        DWORD wait_for_execution( HANDLE process, HANDLE thread )
        {
            const HANDLE handles[] = { thread, process };
            while ( !stop_token.stop_requested( ) && GetTickCount64( ) < deadline )
            {
                const auto remaining = deadline - GetTickCount64( );
                const DWORD wait = WaitForMultipleObjects( 2, handles, FALSE, static_cast<DWORD>( (std::min)( remaining, 50ull ) ) );
                if ( wait == WAIT_OBJECT_0 ) return wait;
                if ( wait == WAIT_OBJECT_0 + 1 ) { SetLastError( ERROR_PROCESS_ABORTED ); return WAIT_FAILED; }
                if ( wait == WAIT_FAILED ) return wait;
            }
            SetLastError( stop_token.stop_requested( ) ? ERROR_CANCELLED : ERROR_TIMEOUT );
            return WAIT_TIMEOUT;
        }
        // A thread exit code is a DWORD, so it cannot carry an x64 HMODULE.
        // Obtain the full base address from the target's module snapshot instead.
        uintptr_t loaded_module_base( HANDLE process, const std::wstring& dll, char* why, std::size_t why_size )
        {
            const auto list = modules::inspect( GetProcessId( process ) );
            if ( list.failure ) { _snprintf_s( why, why_size, _TRUNCATE, "%s", list.failure.message.c_str( ) ); return 0; }
            const auto base = modules::find( list, dll );
            if ( !base ) _snprintf_s( why, why_size, _TRUNCATE, "Loader finished but module was not found in the target snapshot" );
            return base;
        }

        uintptr_t system_function( HANDLE process, const char* name, char* why, std::size_t why_size )
        {
            const auto function = GetProcAddress( GetModuleHandleW( L"kernel32.dll" ), name );
            HMODULE owner = nullptr;
            if ( !function || !GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                 reinterpret_cast<LPCWSTR>( function ), &owner ) )
            { win32_fail( "Locate system function", why, why_size ); return 0; }
            wchar_t path[32768]{};
            const auto length = GetModuleFileNameW( owner, path, 32768 );
            if ( !length || length >= 32768 ) { win32_fail( "Locate system function module", why, why_size ); return 0; }
            const auto list = modules::inspect( GetProcessId( process ) );
            const auto offset = reinterpret_cast<uintptr_t>( function ) - reinterpret_cast<uintptr_t>( owner );
            for ( const auto& item : list.items )
                if ( offset < item.size && _wcsicmp( win::wide( item.path ).c_str( ), path ) == 0 ) return item.base + offset;
            _snprintf_s( why, why_size, _TRUNCATE, "System function '%s' has no matching module in the target", name );
            return 0;
        }

        uintptr_t remote_load_library( HANDLE process, const std::wstring& dll, char* why, std::size_t why_size )
        {
            if ( stop_token.stop_requested( ) || GetTickCount64( ) >= deadline )
            { _snprintf_s( why, why_size, _TRUNCATE, "Operation deadline reached or cancelled before loader execution" ); return 0; }
            const auto entry = system_function( process, "LoadLibraryW", why, why_size );
            if ( !entry ) return 0;
            void* mem = VirtualAllocEx( process, nullptr, ( dll.size( ) + 1 ) * sizeof( wchar_t ),
                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
            if ( !mem )
            {
                win32_fail( "VirtualAllocEx failed", why, why_size );
                return 0;
            }

            uintptr_t result = 0;
            bool safe_to_free = true;
            win::remote_allocation path_storage( process, mem );
            if ( !WriteProcessMemory( process, mem, dll.c_str( ), ( dll.size( ) + 1 ) * sizeof( wchar_t ), nullptr ) )
            {
                win32_fail( "WriteProcessMemory failed", why, why_size );
            }
            else
            {
                win::handle thread( CreateRemoteThread( process, nullptr, 0,
                                                    reinterpret_cast< LPTHREAD_START_ROUTINE >( entry ), mem, 0, nullptr ) );
                if ( !thread )
                {
                    win32_fail( "CreateRemoteThread failed", why, why_size );
                }
                else
                {
                    if ( wait_for_execution( process, thread ) != WAIT_OBJECT_0 )
                    {
                        safe_to_free = false;
                        uncertain = true;
                        win32_fail( "LoadLibraryW did not finish in time", why, why_size );
                    }
                    else
                    {
                        DWORD code = 0;
                        if ( !GetExitCodeThread( thread, &code ) )
                            win32_fail( "could not query the loader thread result", why, why_size );
                        else
                        {
                            result = loaded_module_base( process, dll, why, why_size );
                            if ( !result && !code ) _snprintf_s( why, why_size, _TRUNCATE, "LoadLibraryW returned NULL in the target for '%s'", to_utf8( dll.c_str( ) ).c_str( ) );
                        }
                    }

                }
            }
            if ( !safe_to_free ) { path_storage.release( ); log( warn, "Remote loader may still use its path buffer; retained, retry disabled" ); }
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

                if ( dir.Size < sizeof( IMAGE_EXPORT_DIRECTORY ) || dir.Size > 64u * 1024 * 1024 ||
                     dir.VirtualAddress > nt->OptionalHeader.SizeOfImage || dir.Size > nt->OptionalHeader.SizeOfImage - dir.VirtualAddress )
                { _snprintf_s( why, why_size, _TRUNCATE, "Invalid remote export directory" ); return 0; }
                std::vector< std::uint8_t > exp( dir.Size );
                if ( !ReadProcessMemory( process, reinterpret_cast< void* >( module + dir.VirtualAddress ),
                                         exp.data( ), dir.Size, nullptr ) )
                {
                    win32_fail( "could not read a remote export table", why, why_size );
                    return 0;
                }

                const IMAGE_EXPORT_DIRECTORY* table = reinterpret_cast< IMAGE_EXPORT_DIRECTORY* >( exp.data( ) );
                auto table_range = [&]( DWORD rva, std::size_t count, std::size_t width )
                { return rva >= dir.VirtualAddress && count <= exp.size( ) / width && rva - dir.VirtualAddress <= exp.size( ) - count * width; };
                if ( !table_range( table->AddressOfNames, table->NumberOfNames, sizeof( DWORD ) ) ||
                     !table_range( table->AddressOfNameOrdinals, table->NumberOfNames, sizeof( WORD ) ) ||
                     !table_range( table->AddressOfFunctions, table->NumberOfFunctions, sizeof( DWORD ) ) )
                { _snprintf_s( why, why_size, _TRUNCATE, "Remote export arrays exceed directory bounds" ); return 0; }
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
                        if ( !std::memchr( exp.data( ) + name_rva - dir.VirtualAddress, 0, exp.size( ) - ( name_rva - dir.VirtualAddress ) ) )
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

                if ( index >= table->NumberOfFunctions ) { _snprintf_s( why, why_size, _TRUNCATE, "Export ordinal outside function table" ); return 0; }
                const DWORD func_rva = funcs[index];
                if ( !func_rva || func_rva >= nt->OptionalHeader.SizeOfImage ) { _snprintf_s( why, why_size, _TRUNCATE, "Invalid export RVA" ); return 0; }
                if ( func_rva >= dir.VirtualAddress && func_rva < dir.VirtualAddress + dir.Size )
                {
                    // Forwarder: "MODULE.FUNC" inside the export range -> resolve again.
                    if ( !std::memchr( exp.data( ) + func_rva - dir.VirtualAddress, 0, exp.size( ) - ( func_rva - dir.VirtualAddress ) ) )
                    { _snprintf_s( why, why_size, _TRUNCATE, "Unterminated export forwarder" ); return 0; }
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
        // State preservation adapted from Blackbone RemoteExec::ExecInAnyThread.
        // Save registers when execution resumes: a suspended syscall's captured
        // RAX is not necessarily the syscall return value. Also save volatile XMM.
        // Copyright (c) 2015 DarthTon. MIT: ../../licenses/Blackbone.txt.
        bool do_thread_hijack( HANDLE process, const options& o, bool verbose, char* why, std::size_t why_size )
        {
            win::handle snap( CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, 0 ) );
            if ( !snap )
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


            if ( !tid )
            {
                _snprintf_s( why, why_size, _TRUNCATE, "the target has no hijackable thread" );
                return false;
            }
            if ( verbose )
                log( info, "Hijacking thread %lu", static_cast< unsigned long >( tid ) );

            const auto kernel = loaded_module_base( process, L"kernel32.dll", why, why_size );
            if ( !kernel ) return false;
            const auto loader = remote_get_proc( process, kernel, "LoadLibraryW", why, why_size );
            if ( !loader ) return false;
            win::handle thread( OpenThread( THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, tid ) );
            if ( !thread )
            {
                win32_fail( "could not open the target thread", why, why_size );
                return false;
            }

            bool ok = false;
            bool suspended = false;
            const std::wstring dll = to_wide( o.dll_path );
            bool context_taken = false;
            bool execution_started = false;
            CONTEXT ctx { };
            void* mem = nullptr;
            win::remote_allocation stub_storage( process );
            struct restore_suspension
            {
                HANDLE thread; CONTEXT& context; bool& suspended; bool& context_taken;
                ~restore_suspension( )
                {
                    if ( suspended )
                    {
                        if ( context_taken ) SetThreadContext( thread, &context );
                        ResumeThread( thread );
                    }
                }
            } restore{ thread.get( ), ctx, suspended, context_taken };

            for ( ;; )
            {
                const DWORD previous_suspensions = SuspendThread( thread );
                if ( previous_suspensions == static_cast< DWORD >( -1 ) )
                {
                    win32_fail( "SuspendThread failed", why, why_size );
                    break;
                }
                suspended = true;
                if ( previous_suspensions != 0 )
                { _snprintf_s( why, why_size, _TRUNCATE, "Selected thread was already suspended" ); break; }

                ctx.ContextFlags = CONTEXT_FULL;
                if ( !GetThreadContext( thread, &ctx ) )
                {
                    win32_fail( "could not read the thread context", why, why_size );
                    break;
                }
                context_taken = true;
                if ( verbose ) log( info, "Thread context: RIP=0x%llX RSP=0x%llX", ctx.Rip, ctx.Rsp );

                const std::size_t path_bytes = ( dll.size( ) + 1 ) * sizeof( wchar_t );
                constexpr std::size_t code_size = 0x1000;
                const std::size_t total_size = code_size + 8 + path_bytes;
                mem = VirtualAllocEx( process, nullptr, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
                if ( !mem ) { win32_fail( "Allocate thread context", why, why_size ); break; }
                stub_storage.reset( mem );
                void* slot_addr = static_cast<std::uint8_t*>( mem ) + code_size;
                void* path_addr = static_cast<std::uint8_t*>( slot_addr ) + 8;
                std::uint8_t stub[256]{};
                std::size_t n = 0;
                auto b1 = [&]( std::uint8_t value ) { stub[n++] = value; };
                auto b8 = [&]( std::uint64_t value ) { std::memcpy( stub + n, &value, 8 ); n += 8; };
                b1( 0x9C ); // pushfq
                const std::uint8_t pushes[] = { 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53 };
                for ( auto op : pushes ) b1( op );
                b1( 0x49 ); b1( 0x89 ); b1( 0xE3 ); // mov r11,rsp (saved stack)
                b1( 0x48 ); b1( 0x83 ); b1( 0xE4 ); b1( 0xF0 ); // and rsp,-16
                b1( 0x48 ); b1( 0x81 ); b1( 0xEC ); b1( 0x90 ); b1( 0 ); b1( 0 ); b1( 0 );
                const std::uint8_t store_stack[] = { 0x4C, 0x89, 0x9C, 0x24, 0x80, 0, 0, 0 };
                for ( auto op : store_stack ) b1( op );
                for ( int reg = 0; reg < 6; ++reg )
                { b1( 0xF3 ); b1( 0x0F ); b1( 0x7F ); b1( static_cast<std::uint8_t>( 0x44 + reg * 8 ) ); b1( 0x24 ); b1( static_cast<std::uint8_t>( 0x20 + reg * 16 ) ); }
                b1( 0x48 ); b1( 0xB9 ); b8( reinterpret_cast<std::uint64_t>( path_addr ) );
                b1( 0x48 ); b1( 0xB8 ); b8( loader ); b1( 0xFF ); b1( 0xD0 );
                b1( 0x48 ); b1( 0xBA ); b8( reinterpret_cast<std::uint64_t>( slot_addr ) );
                b1( 0x48 ); b1( 0x89 ); b1( 0x02 );
                for ( int reg = 0; reg < 6; ++reg )
                { b1( 0xF3 ); b1( 0x0F ); b1( 0x6F ); b1( static_cast<std::uint8_t>( 0x44 + reg * 8 ) ); b1( 0x24 ); b1( static_cast<std::uint8_t>( 0x20 + reg * 16 ) ); }
                const std::uint8_t load_stack[] = { 0x48, 0x8B, 0xA4, 0x24, 0x80, 0, 0, 0 };
                for ( auto op : load_stack ) b1( op );
                const std::uint8_t pops[] = { 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58 };
                for ( auto op : pops ) b1( op );
                b1( 0x9D ); // popfq
                b1( 0xFF ); b1( 0x25 ); b1( 0 ); b1( 0 ); b1( 0 ); b1( 0 ); b8( ctx.Rip );
                const std::uint64_t sentinel = UINT64_MAX;
                if ( !WriteProcessMemory( process, mem, stub, n, nullptr ) ||
                     !WriteProcessMemory( process, slot_addr, &sentinel, sizeof( sentinel ), nullptr ) ||
                     !WriteProcessMemory( process, path_addr, dll.c_str( ), path_bytes, nullptr ) )
                { win32_fail( "Write thread context", why, why_size ); break; }
                DWORD old = 0;
                if ( !VirtualProtectEx( process, mem, code_size, PAGE_EXECUTE_READ, &old ) || !FlushInstructionCache( process, mem, n ) )
                { win32_fail( "Prepare thread bootstrap", why, why_size ); break; }

                CONTEXT redirected = ctx;
                redirected.Rip = reinterpret_cast< DWORD64 >( mem );
                if ( !SetThreadContext( thread, &redirected ) )
                {
                    win32_fail( "could not redirect the thread", why, why_size );
                    break;
                }
                if ( ResumeThread( thread ) == static_cast<DWORD>( -1 ) )
                { win32_fail( "ResumeThread failed", why, why_size ); break; }
                execution_started = true;
                stub_storage.release( );
                suspended = false;

                std::uint64_t result = sentinel;
                for ( ; GetTickCount64( ) < deadline && !stop_token.stop_requested( ) && result == sentinel; )
                {
                    if ( WaitForSingleObject( process, 0 ) == WAIT_OBJECT_0 )
                    {
                        DWORD exit_code = 0; GetExitCodeProcess( process, &exit_code );
                        _snprintf_s( why, why_size, _TRUNCATE, "Target exited during Thread Hijack (0x%08lX)", exit_code );
                        uncertain = true;
                        return false;
                    }
                    ReadProcessMemory( process, slot_addr, &result, sizeof result, nullptr );
                    if ( result == sentinel )
                        Sleep( 50 );
                }
                if ( result == sentinel )
                {
                    uncertain = true;
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

            if ( execution_started )
            {
                if ( !ok ) uncertain = true;
                log( warn, "Thread Hijack stub retained because completion does not prove its return sequence has finished" );
            }

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

            void* remote = VirtualAllocEx( process, reinterpret_cast<void*>( nt->OptionalHeader.ImageBase ), image_size,
                                           MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
            if ( !remote ) remote = VirtualAllocEx( process, nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
            if ( !remote )
            {
                win32_fail( "VirtualAllocEx (image) failed", why, why_size );
                return false;
            }

            bool ok = false;
            std::uint8_t* stub_mem = nullptr;
            bool execution_complete = true;
            win::remote_allocation image_storage( process, remote );
            win::remote_allocation stub_storage( process );
            const auto exception_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
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
            int import_count = 0;
            int patched = 0;
            if ( delta != 0 )
            {
                const IMAGE_DATA_DIRECTORY& rel_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
                if ( !rel_dir.Size ) { _snprintf_s( why, why_size, _TRUNCATE, "Image has no relocations and its preferred base is unavailable" ); goto finish; }
                std::size_t at = rel_dir.VirtualAddress;
                const std::size_t end = at + rel_dir.Size;
                while ( at + sizeof( IMAGE_BASE_RELOCATION ) <= end && end <= local.size( ) )
                {
                    const IMAGE_BASE_RELOCATION* block = reinterpret_cast< const IMAGE_BASE_RELOCATION* >( local.data( ) + at );
                    if ( block->SizeOfBlock < sizeof( IMAGE_BASE_RELOCATION ) || at + block->SizeOfBlock > end )
                    { _snprintf_s( why, why_size, _TRUNCATE, "Malformed relocation block after rebasing" ); goto finish; }
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
                                std::uint64_t value = 0;
                                std::memcpy( &value, local.data( ) + slot, sizeof( value ) );
                                value += static_cast<std::uint64_t>( delta );
                                std::memcpy( local.data( ) + slot, &value, sizeof( value ) );
                                ++patched;
                            }
                        }
                    }
                    at += block->SizeOfBlock;
                }
            }
            if ( verbose && patched > 0 )
                log( info, "Rebased image (%d relocations)", patched );

            // Validate each RVA again after relocation; malformed data must not
            // become an unchecked pointer into the local mapping.
            {
                auto within = [&]( std::uint64_t at, std::size_t size ) { return at <= local.size( ) && size <= local.size( ) - at; };
                auto string_at = [&]( std::uint64_t at ) -> const char*
                {
                    if ( at >= local.size( ) || !std::memchr( local.data( ) + at, 0, local.size( ) - at ) ) return nullptr;
                    return reinterpret_cast<const char*>( local.data( ) + at );
                };
                const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
                const std::uint64_t end = std::uint64_t( directory.VirtualAddress ) + directory.Size;
                for ( std::uint64_t at = directory.VirtualAddress; directory.Size && at < end; at += sizeof( IMAGE_IMPORT_DESCRIPTOR ) )
                {
                    IMAGE_IMPORT_DESCRIPTOR descriptor{};
                    if ( !within( at, sizeof( descriptor ) ) || at + sizeof( descriptor ) > end )
                    { _snprintf_s( why, why_size, _TRUNCATE, "Import descriptor exceeds image" ); goto finish; }
                    std::memcpy( &descriptor, local.data( ) + at, sizeof( descriptor ) );
                    if ( !descriptor.Name ) break;
                    const char* mod_name = string_at( descriptor.Name );
                    if ( !mod_name ) { _snprintf_s( why, why_size, _TRUNCATE, "Invalid import module name" ); goto finish; }
                    if ( verbose ) log( info, "Resolving import: %s", mod_name );
                    const auto remote_module = remote_load_library( process, to_wide( mod_name ), why, why_size );
                    if ( !remote_module ) goto finish;
                    std::uint64_t thunk_at = descriptor.OriginalFirstThunk ? descriptor.OriginalFirstThunk : descriptor.FirstThunk;
                    std::uint64_t iat_at = descriptor.FirstThunk;
                    for ( ;; thunk_at += 8, iat_at += 8 )
                    {
                        std::uint64_t thunk = 0;
                        if ( !within( thunk_at, 8 ) || !within( iat_at, 8 ) )
                        { _snprintf_s( why, why_size, _TRUNCATE, "Import thunk exceeds image" ); goto finish; }
                        std::memcpy( &thunk, local.data( ) + thunk_at, 8 );
                        if ( !thunk ) break;
                        uintptr_t address = 0;
                        if ( thunk & IMAGE_ORDINAL_FLAG64 )
                        {
                            const auto name = "#" + std::to_string( thunk & 0xffff );
                            address = remote_get_proc( process, remote_module, name, why, why_size );
                        }
                        else
                        {
                            const char* name = within( thunk, sizeof( WORD ) + 1 ) ? string_at( thunk + sizeof( WORD ) ) : nullptr;
                            if ( !name ) { _snprintf_s( why, why_size, _TRUNCATE, "Invalid import symbol name" ); goto finish; }
                            address = remote_get_proc( process, remote_module, name, why, why_size );
                        }
                        if ( !address ) goto finish;
                        std::memcpy( local.data( ) + iat_at, &address, sizeof( address ) );
                        ++import_count;
                    }
                }
            }
            if ( verbose )
                log( info, "Imports resolved (%d functions)", import_count );

            if ( !WriteProcessMemory( process, remote, local.data( ), image_size, nullptr ) )
            {
                win32_fail( "could not write the mapped image", why, why_size );
                goto finish;
            }

            // per-section page protection, like the real loader applies
            {
                DWORD old = 0;
                if ( !VirtualProtectEx( process, remote, nt->OptionalHeader.SizeOfHeaders, PAGE_READONLY, &old ) )
                { win32_fail( "Protect mapped headers", why, why_size ); goto finish; }
            }
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
                if ( (std::max)( section[s].Misc.VirtualSize, section[s].SizeOfRawData ) && !VirtualProtectEx( process,
                                  reinterpret_cast< std::uint8_t* >( remote ) + section[s].VirtualAddress,
                                  (std::max)( section[s].Misc.VirtualSize, section[s].SizeOfRawData ), protect, &old ) )
                { win32_fail( "Protect mapped section", why, why_size ); goto finish; }
            }

            // Register x64 unwind data through documented RtlAddFunctionTable.
            // The table remains in the mapped image for its lifetime.
            stub_mem = static_cast<std::uint8_t*>( VirtualAllocEx( process, nullptr, 0x200, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE ) );
            if ( !stub_mem ) { win32_fail( "Allocate bootstrap", why, why_size ); goto finish; }
            stub_storage.reset( stub_mem );
            {
                std::vector<std::uint8_t> stub;
                auto b1 = [&]( std::uint8_t value ) { stub.push_back( value ); };
                auto b4 = [&]( std::uint32_t value ) { const auto* bytes = reinterpret_cast<const std::uint8_t*>( &value ); stub.insert( stub.end( ), bytes, bytes + 4 ); };
                auto b8 = [&]( std::uint64_t value ) { const auto* bytes = reinterpret_cast<const std::uint8_t*>( &value ); stub.insert( stub.end( ), bytes, bytes + 8 ); };
                auto mov_rcx = [&]( std::uint64_t value ) { b1( 0x48 ); b1( 0xB9 ); b8( value ); };
                auto call = [&]( std::uint64_t address ) { b1( 0x48 ); b1( 0xB8 ); b8( address ); b1( 0xFF ); b1( 0xD0 ); };
                uintptr_t add_table = 0, delete_table = 0;
                if ( exception_dir.Size )
                {
                    const auto native = loaded_module_base( process, L"ntdll.dll", why, why_size );
                    if ( !native ) goto finish;
                    add_table = remote_get_proc( process, native, "RtlAddFunctionTable", why, why_size );
                    delete_table = remote_get_proc( process, native, "RtlDeleteFunctionTable", why, why_size );
                    if ( !add_table || !delete_table ) goto finish;
                }
                b1( 0x53 ); // preserve RBX
                b1( 0x48 ); b1( 0x83 ); b1( 0xEC ); b1( 0x20 ); // aligned shadow space
                std::size_t registration_failure_jump = 0;
                if ( add_table )
                {
                    mov_rcx( reinterpret_cast<std::uint64_t>( remote ) + exception_dir.VirtualAddress );
                    b1( 0xBA ); b4( exception_dir.Size / sizeof( RUNTIME_FUNCTION ) );
                    b1( 0x49 ); b1( 0xB8 ); b8( reinterpret_cast<std::uint64_t>( remote ) );
                    call( add_table );
                    b1( 0x85 ); b1( 0xC0 ); // test eax,eax
                    b1( 0x74 ); registration_failure_jump = stub.size( ); b1( 0 );
                }
                mov_rcx( reinterpret_cast<std::uint64_t>( remote ) );
                b1( 0xBA ); b4( DLL_PROCESS_ATTACH );
                b1( 0x45 ); b1( 0x31 ); b1( 0xC0 );
                call( reinterpret_cast<std::uint64_t>( remote ) + nt->OptionalHeader.AddressOfEntryPoint );
                b1( 0x89 ); b1( 0xC3 ); // mov ebx,eax
                b1( 0x85 ); b1( 0xDB );
                b1( 0x75 ); const auto success_jump = stub.size( ); b1( 0 );
                if ( delete_table )
                {
                    mov_rcx( reinterpret_cast<std::uint64_t>( remote ) + exception_dir.VirtualAddress );
                    call( delete_table );
                    // If deregistration fails, return 2 and retain the image.
                    b1( 0x85 ); b1( 0xC0 ); b1( 0x75 ); b1( 5 );
                    b1( 0xBB ); b4( 2 );
                }
                b1( 0xEB ); const auto failure_done_jump = stub.size( ); b1( 0 );
                const auto registration_failure = stub.size( );
                b1( 0x31 ); b1( 0xDB ); // xor ebx,ebx
                b1( 0xEB ); const auto skip_success = stub.size( ); b1( 0 );
                const auto success = stub.size( ); b1( 0xBB ); b4( 1 );
                const auto epilogue = stub.size( );
                stub[skip_success] = static_cast<std::uint8_t>( epilogue - skip_success - 1 );
                stub[success_jump] = static_cast<std::uint8_t>( success - success_jump - 1 );
                stub[failure_done_jump] = static_cast<std::uint8_t>( epilogue - failure_done_jump - 1 );
                if ( add_table ) stub[registration_failure_jump] = static_cast<std::uint8_t>( registration_failure - registration_failure_jump - 1 );
                b1( 0x89 ); b1( 0xD9 ); // mov ecx,ebx
                b1( 0x48 ); b1( 0x83 ); b1( 0xC4 ); b1( 0x20 ); b1( 0x5B );
                const auto exit = system_function( process, "ExitThread", why, why_size );
                if ( !exit ) goto finish;
                b1( 0x48 ); b1( 0xB8 ); b8( exit ); b1( 0xFF ); b1( 0xE0 );
                if ( stub.size( ) > 0x200 || !WriteProcessMemory( process, stub_mem, stub.data( ), stub.size( ), nullptr ) )
                { win32_fail( "Write bootstrap", why, why_size ); goto finish; }
            }

            {
                DWORD old = 0;
                if ( !VirtualProtectEx( process, stub_mem, 0x200, PAGE_EXECUTE_READ, &old ) || !FlushInstructionCache( process, nullptr, 0 ) )
                { win32_fail( "Prepare mapped bootstrap", why, why_size ); goto finish; }
                win::handle thread( CreateRemoteThread( process, nullptr, 0,
                                                    reinterpret_cast< LPTHREAD_START_ROUTINE >( stub_mem ),
                                                    nullptr, 0, nullptr ) );
                if ( !thread )
                {
                    win32_fail( "could not start the mapped DLL", why, why_size );
                    goto finish;
                }
                execution_complete = false;
                const DWORD wait = wait_for_execution( process, thread );
                execution_complete = wait == WAIT_OBJECT_0;
                if ( !execution_complete ) uncertain = true;
                DWORD code = 0;
                if ( wait != WAIT_OBJECT_0 )
                    _snprintf_s( why, why_size, _TRUNCATE, "the mapped DLL did not initialize in time" );
                else if ( !GetExitCodeThread( thread, &code ) )
                { win32_fail( "Query bootstrap result", why, why_size ); execution_complete = false; uncertain = true; }
                else if ( code != 1 )
                    _snprintf_s( why, why_size, _TRUNCATE, "DLL initialization failed (bootstrap result 0x%08lX)", code );
                else
                    ok = true;
                if ( execution_complete && ( code == 2 || code >= 0x80000000u ) ) { uncertain = true; execution_complete = false; }
            }

            finish:
            if ( !execution_complete ) { stub_storage.release( ); image_storage.release( ); }
            else if ( ok ) image_storage.release( );
            if ( ok && verbose )
                log( info, "Module base: 0x%llX",
                     static_cast< unsigned long long >( reinterpret_cast< std::uintptr_t >( remote ) ) );
            return ok;
        }


    }
    backend_result execute( HANDLE target, const options& request, const std::vector<std::uint8_t>& image, std::stop_token stop )
    {
        stop_token = stop;
        deadline = GetTickCount64( ) + request.timeout_ms;
        uncertain = false;
        char why[2048]{ };
        bool success = false;
        switch ( request.how )
        {
        case method::load_library: success = do_load_library( target, to_wide( request.dll_path ), request.verbose, why, sizeof( why ) ); break;
        case method::manual_map: success = do_manual_map( target, image, request.verbose, why, sizeof( why ) ); break;
        case method::thread_hijack: success = do_thread_hijack( target, request, request.verbose, why, sizeof( why ) ); break;
        }
        if ( uncertain ) log( warn, "Remote operation is indeterminate; memory still in use is retained and automatic retry is disabled" );
        return { success, uncertain, why };
    }
}
