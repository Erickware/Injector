#include "modules.hpp"
#include <tlhelp32.h>
#include "../process.hpp"

namespace modules
{
    snapshot inspect( DWORD pid, std::uint64_t expected_creation_time )
    {
        snapshot result;
        if ( pid == 0 ) { result.failure = { ERROR_INVALID_PARAMETER, "Select a process before inspecting modules" }; return result; }
        win::handle target( OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid ) );
        if ( !target ) { result.failure = win::failure( "Open module target" ); return result; }
        if ( WaitForSingleObject( target, 0 ) != WAIT_TIMEOUT ||
             ( expected_creation_time && process::creation_time( target ) != expected_creation_time ) )
        { result.failure = { ERROR_INVALID_HANDLE, "Module target exited or its PID was reused" }; return result; }
        win::handle handle;
        for ( unsigned attempt = 0; attempt < 8; ++attempt )
        {
            handle.reset( CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid ) );
            if ( handle || GetLastError( ) != ERROR_BAD_LENGTH ) break;
        }
        if ( !handle ) { result.failure = win::failure( "Snapshot modules" ); return result; }
        MODULEENTRY32W record{ sizeof( record ) };
        if ( !Module32FirstW( handle, &record ) )
        {
            if ( GetLastError( ) != ERROR_NO_MORE_FILES ) result.failure = win::failure( "Enumerate modules" );
            return result;
        }
        do
        {
            result.items.push_back( { win::utf8( record.szModule ), win::utf8( record.szExePath ),
                reinterpret_cast<std::uintptr_t>( record.modBaseAddr ), record.modBaseSize } );
        } while ( Module32NextW( handle, &record ) );
        const DWORD code = GetLastError( );
        if ( code != ERROR_NO_MORE_FILES ) { result.items.clear( ); result.failure = win::failure( "Enumerate modules", code ); }
        if ( WaitForSingleObject( target, 0 ) != WAIT_TIMEOUT )
        { result.items.clear( ); result.failure = { ERROR_PROCESS_ABORTED, "Target exited during module enumeration" }; }
        return result;
    }

    std::uintptr_t find( const snapshot& list, const std::wstring& value )
    {
        const bool full_path = value.find_first_of( L"\\/" ) != std::wstring::npos;
        for ( const auto& item : list.items )
            if ( _wcsicmp( win::wide( full_path ? item.path : item.name ).c_str( ), value.c_str( ) ) == 0 ) return item.base;
        return 0;
    }
}
