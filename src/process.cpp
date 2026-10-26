// language: C++, file: src/process.cpp, target: Windows 10/11 x64, MSVC
// *Toolhelp32 enumeration + per-process architecture probe for the Process List panel*

#include "process.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string_view>
#include <unordered_map>

namespace process
{
    namespace
    {
        std::vector< entry > g_list;
        std::mutex g_mutex;
        std::uint64_t g_last_refresh_ms = 0;

        // Title lookup is the expensive half of the search, so it is rebuilt with the
        // list and only consulted for rows the cheap name/pid pass did not match.
        std::unordered_map< std::uint32_t, std::string > g_titles;
        std::uint64_t g_title_generation = 0;

        bool ascii_contains_ci( const char* haystack, const char* needle )
        {
            if ( !needle || needle[0] == '\0' )
                return true;
            if ( !haystack )
                return false;
            const std::size_t needle_length = std::strlen( needle );
            for ( const char* at = haystack; *at; ++at )
            {
                std::size_t index = 0;
                while ( index < needle_length && at[index] &&
                        std::tolower( static_cast< unsigned char >( at[index] ) ) == std::tolower( static_cast< unsigned char >( needle[index] ) ) )
                    ++index;
                if ( index == needle_length )
                    return true;
            }
            return false;
        }

        architecture native_architecture( )
        {
            SYSTEM_INFO system{ };
            ::GetNativeSystemInfo( &system );
            switch ( system.wProcessorArchitecture )
            {
            case PROCESSOR_ARCHITECTURE_AMD64: return architecture::x64;
            case PROCESSOR_ARCHITECTURE_ARM64: return architecture::arm64;
            case PROCESSOR_ARCHITECTURE_INTEL: return architecture::x86;
            default: return architecture::unknown;
            }
        }

        // Only inspect a confirmed executable path. An unreadable image stays unknown.
        architecture architecture_from_image( const std::wstring& path, architecture known )
        {
            if ( known != architecture::unknown || path.empty( ) )
                return known;

            HANDLE file = ::CreateFileW( path.c_str( ), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr );
            if ( file == INVALID_HANDLE_VALUE )
                return architecture::unknown;

            IMAGE_DOS_HEADER dos{ };
            IMAGE_NT_HEADERS32 nt32{ };
            DWORD read = 0;
            architecture result = architecture::unknown;

            if ( ::ReadFile( file, &dos, sizeof( dos ), &read, nullptr ) && read == sizeof( dos ) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
                 ::SetFilePointer( file, dos.e_lfanew, nullptr, FILE_BEGIN ) != INVALID_SET_FILE_POINTER &&
                 ::ReadFile( file, &nt32, sizeof( nt32 ), &read, nullptr ) && read >= 6 && nt32.Signature == IMAGE_NT_SIGNATURE )
            {
                switch ( nt32.FileHeader.Machine )
                {
                case IMAGE_FILE_MACHINE_I386:  result = architecture::x86;   break;
                case IMAGE_FILE_MACHINE_AMD64: result = architecture::x64;   break;
                case IMAGE_FILE_MACHINE_ARM64: result = architecture::arm64; break;
                default: break;
                }
            }
            ::CloseHandle( file );
            return result;
        }

        // Preferred path: ask the kernel, which also resolves processes with no readable
        // image on disk. Falls back to the PE header when the handle is denied.
        architecture probe_architecture( HANDLE handle )
        {
            using is_wow64_process2_fn = BOOL ( WINAPI* )( HANDLE, USHORT*, USHORT* );
            using is_wow64_process_fn = BOOL ( WINAPI* )( HANDLE, PBOOL );

            static is_wow64_process2_fn wow64_process2 = reinterpret_cast< is_wow64_process2_fn >(
                reinterpret_cast< void* >( ::GetProcAddress( ::GetModuleHandleW( L"kernel32.dll" ), "IsWow64Process2" ) ) );
            static is_wow64_process_fn wow64_process = reinterpret_cast< is_wow64_process_fn >(
                reinterpret_cast< void* >( ::GetProcAddress( ::GetModuleHandleW( L"kernel32.dll" ), "IsWow64Process" ) ) );

            if ( wow64_process2 )
            {
                USHORT process_machine = IMAGE_FILE_MACHINE_UNKNOWN;
                USHORT native_machine = IMAGE_FILE_MACHINE_UNKNOWN;
                if ( wow64_process2( handle, &process_machine, &native_machine ) )
                {
                    const USHORT machine = process_machine == IMAGE_FILE_MACHINE_UNKNOWN ? native_machine : process_machine;
                    switch ( machine )
                    {
                    case IMAGE_FILE_MACHINE_I386:  return architecture::x86;
                    case IMAGE_FILE_MACHINE_AMD64: return architecture::x64;
                    case IMAGE_FILE_MACHINE_ARM64: return architecture::arm64;
                    default: break;
                    }
                }
            }

            if ( wow64_process )
            {
                BOOL wow64 = FALSE;
                if ( wow64_process( handle, &wow64 ) )
                {
                    if ( wow64 )
                        return native_architecture( ) == architecture::arm64 ? architecture::unknown : architecture::x86;
                    return native_architecture( );
                }
            }
            return architecture::unknown;
        }

        bool probe_elevation( HANDLE handle )
        {
            // Only proves elevation when the token opens and reports a high integrity
            // level; a failure to query is reported as "not proven", never as "no".
            HANDLE token = nullptr;
            if ( !::OpenProcessToken( handle, TOKEN_QUERY, &token ) )
                return false;

            bool elevated = false;
            DWORD returned = 0;
            TOKEN_ELEVATION elevation{ };
            if ( ::GetTokenInformation( token, TokenElevation, &elevation, sizeof( elevation ), &returned ) )
                elevated = elevation.TokenIsElevated != 0;
            ::CloseHandle( token );
            return elevated;
        }

        std::uint32_t count_threads( const PROCESSENTRY32W& record )
        {
            return record.cntThreads;
        }

        std::string wide_to_utf8( const wchar_t* wide, int length )
        {
            if ( !wide || length <= 0 )
                return { };
            const int bytes = ::WideCharToMultiByte( CP_UTF8, 0, wide, length, nullptr, 0, nullptr, nullptr );
            if ( bytes <= 0 )
                return { };
            std::string utf8( static_cast< std::size_t >( bytes ), '\0' );
            ::WideCharToMultiByte( CP_UTF8, 0, wide, length, utf8.data( ), bytes, nullptr, nullptr );
            return utf8;
        }

        BOOL CALLBACK collect_title( HWND window, LPARAM parameter )
        {
            auto* out = reinterpret_cast< std::unordered_map< std::uint32_t, std::string >* >( parameter );

            if ( !::IsWindowVisible( window ) )
                return TRUE;

            const int length = ::GetWindowTextLengthW( window );
            if ( length <= 0 )
                return TRUE;

            std::wstring wide( static_cast< std::size_t >( length ) + 1, L'\0' );
            const int written = ::GetWindowTextW( window, wide.data( ), length + 1 );
            if ( written <= 0 )
                return TRUE;

            DWORD pid = 0;
            ::GetWindowThreadProcessId( window, &pid );
            if ( pid == 0 )
                return TRUE;

            std::string utf8 = wide_to_utf8( wide.c_str( ), written );
            if ( utf8.empty( ) )
                return TRUE;

            auto& slot = ( *out )[ pid ];
            if ( slot.empty( ) )
                slot = std::move( utf8 );
            return TRUE;
        }

        void collect_titles( std::unordered_map< std::uint32_t, std::string >& out )
        {
            ::EnumWindows( &collect_title, reinterpret_cast< LPARAM >( &out ) );
        }

        std::string image_name( const wchar_t* path )
        {
            const std::wstring wide = path ? path : L"";
            const std::size_t slash = wide.find_last_of( L"\\/" );
            const std::wstring_view leaf = slash == std::wstring::npos
                ? std::wstring_view{ wide }
                : std::wstring_view{ wide }.substr( slash + 1 );
            return wide_to_utf8( leaf.data( ), static_cast< int >( leaf.size( ) ) );
        }

    }

    bool refresh( )
    {
        HANDLE snapshot = ::CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
        if ( snapshot == INVALID_HANDLE_VALUE )
            return false;

        PROCESSENTRY32W record{ };
        record.dwSize = sizeof( record );

        std::vector< entry > collected;
        collected.reserve( 512 );

        if ( ::Process32FirstW( snapshot, &record ) )
        {
            do
            {
                entry item;
                item.name = image_name( record.szExeFile );
                item.pid = record.th32ProcessID;
                item.parent_pid = record.th32ParentProcessID;
                item.threads = count_threads( record );
                item.system = record.th32ProcessID == 0 || record.th32ProcessID == 4;

                if ( item.pid != 0 )
                {
                    HANDLE handle = ::OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, item.pid );
                    if ( handle )
                    {
                        item.arch = probe_architecture( handle );
                        item.elevated = probe_elevation( handle );
                        ::CloseHandle( handle );
                    }
                }

                if ( item.arch == architecture::unknown )
                {
                    // Denied handle (elevated or protected target) or a process with no
                    // readable image: resolve the executable path and read its PE header.
                    wchar_t path[1024]{ };
                    DWORD length = static_cast< DWORD >( std::size( path ) );
                    HANDLE query = ::OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, item.pid );
                    if ( query )
                    {
                        if ( ::QueryFullProcessImageNameW( query, 0, path, &length ) )
                            item.arch = architecture_from_image( std::wstring( path, length ), item.arch );
                        ::CloseHandle( query );
                    }
                }

                collected.push_back( std::move( item ) );
            } while ( ::Process32NextW( snapshot, &record ) );
        }

        ::CloseHandle( snapshot );

        if ( collected.empty( ) )
            return false;

        std::sort( collected.begin( ), collected.end( ),
            []( const entry& left, const entry& right )
            {
                const int order = _stricmp( left.name.c_str( ), right.name.c_str( ) );
                if ( order != 0 )
                    return order < 0;
                return left.pid < right.pid;
            } );

        std::unordered_map< std::uint32_t, std::string > titles;
        collect_titles( titles );

        {
            std::lock_guard< std::mutex > lock( g_mutex );
            g_list = std::move( collected );
            g_titles = std::move( titles );
            g_last_refresh_ms = ::GetTickCount64( );
            ++g_title_generation;
        }
        return true;
    }

    const std::vector< entry >& list( )
    {
        return g_list;
    }

    std::uint64_t last_refresh_ms( )
    {
        return g_last_refresh_ms;
    }

    bool matches( const entry& item, const char* query )
    {
        if ( !query || query[0] == '\0' )
            return true;
        if ( ascii_contains_ci( item.name.c_str( ), query ) )
            return true;

        char pid_text[16]{ };
        std::snprintf( pid_text, sizeof( pid_text ), "%u", item.pid );
        return std::strstr( pid_text, query ) != nullptr;
    }

    bool title_matches( std::uint32_t pid, const char* query )
    {
        if ( !query || query[0] == '\0' )
            return true;

        std::lock_guard< std::mutex > lock( g_mutex );
        const auto found = g_titles.find( pid );
        if ( found == g_titles.end( ) )
            return false;
        return ascii_contains_ci( found->second.c_str( ), query );
    }

    const char* architecture_name( architecture value )
    {
        switch ( value )
        {
        case architecture::x86:   return "x86";
        case architecture::x64:   return "x64";
        case architecture::arm64: return "arm64";
        default:                  return "?";
        }
    }
}
