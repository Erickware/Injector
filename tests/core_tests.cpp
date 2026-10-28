#include "inject.hpp"
#include "process.hpp"
#include "core/diagnostics.hpp"
#include "core/modules.hpp"
#include "core/pe_image.hpp"
#include "core/profile.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <algorithm>
#include <cstring>

namespace
{
    unsigned checks = 0;
    void check( bool value, const char* label )
    {
        if ( !value ) throw std::runtime_error( label );
        ++checks; std::cout << "PASS " << label << '\n';
    }
    struct child
    {
        win::handle stop, ready, process, marker;
        DWORD pid = 0;
        explicit child( const std::filesystem::path& executable, bool prohibit_dynamic_code = false )
        {
            const auto prefix = L"Local\\InjectorTests-" + std::to_wstring( GetCurrentProcessId( ) ) + L"-" + std::to_wstring( GetTickCount64( ) );
            stop.reset( CreateEventW( nullptr, TRUE, FALSE, ( prefix + L"-stop" ).c_str( ) ) );
            ready.reset( CreateEventW( nullptr, TRUE, FALSE, ( prefix + L"-ready" ).c_str( ) ) );
            auto command = L"\"" + executable.wstring( ) + L"\" \"" + prefix + L"\"";
            if ( prohibit_dynamic_code ) command += L" deny-dynamic-code";
            STARTUPINFOW startup{ sizeof( startup ) }; PROCESS_INFORMATION info{};
            if ( !CreateProcessW( executable.c_str( ), command.data( ), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info ) )
                throw std::runtime_error( win::failure( "Create fixture" ).message );
            process.reset( info.hProcess ); CloseHandle( info.hThread ); pid = info.dwProcessId;
            marker.reset( CreateEventW( nullptr, TRUE, FALSE, ( L"Local\\InjectorFixture-" + std::to_wstring( pid ) ).c_str( ) ) );
            if ( WaitForSingleObject( ready, 5000 ) != WAIT_OBJECT_0 ) throw std::runtime_error( "Fixture startup timeout" );
        }
        ~child( )
        {
            SetEvent( stop );
            if ( process && WaitForSingleObject( process, 4000 ) == WAIT_TIMEOUT )
            { TerminateProcess( process, 99 ); WaitForSingleObject( process, 3000 ); }
        }
    };
    inject::options request( const child& target, const std::filesystem::path& dll, inject::method how )
    {
        inject::options o;
        o.pid = target.pid; o.target_creation_time = process::creation_time( target.process );
        strcpy_s( o.name, "InjectorTestTarget.exe" );
        strcpy_s( o.dll_path, win::utf8( dll.wstring( ) ).c_str( ) );
        o.how = how; o.verbose = true; o.timeout_ms = 5000;
        return o;
    }
    inject::operation finish( )
    {
        const auto end = GetTickCount64( ) + 10000;
        while ( inject::running( ) && GetTickCount64( ) < end ) Sleep( 10 );
        if ( inject::running( ) ) throw std::runtime_error( "Operation test deadline" );
        return inject::current_operation( );
    }
}

int wmain( int argc, wchar_t** argv )
{
    try
    {
        if ( argc < 5 ) throw std::runtime_error( "Expected target, success DLL, slow DLL and rejecting DLL" );
        const std::filesystem::path target = argv[1], dll = argv[2], slow = argv[3], reject = argv[4];
        const auto temp = std::filesystem::temp_directory_path( ) / ( L"InjectorTests-" + std::to_wstring( GetCurrentProcessId( ) ) );
        std::filesystem::create_directories( temp );
        std::vector<std::uint8_t> bytes; pe::image image;
        check( !pe::read( win::utf8( dll.wstring( ) ), bytes, image ), "Read native DLL" );
        check( image.is_dll && image.arch == process::architecture::x64, "DLL metadata" );
        check( !pe::manual_map_support( bytes, image ), "Fixture supported by Manual Map preflight" );
        check( !pe::compatible( image, process::architecture::x64 ), "Matching architectures" );
        check( pe::compatible( image, process::architecture::x86 ).code == ERROR_EXE_MACHINE_TYPE_MISMATCH, "Architecture mismatch" );
        check( pe::compatible( image, process::architecture::unknown ).code == ERROR_NOT_SUPPORTED, "Unknown architecture rejected" );
        pe::image invalid;
        check( bool( pe::inspect( {}, invalid ) ), "Empty PE rejected" );
        for ( std::size_t length : { 1u, 63u, 100u, 255u } ) check( bool( pe::inspect( std::span( bytes ).first( length ), invalid ) ), "Truncated PE rejected" );
        auto corrupt = bytes;
        reinterpret_cast<IMAGE_DOS_HEADER*>( corrupt.data( ) )->e_lfanew = -1;
        check( bool( pe::inspect( corrupt, invalid ) ), "Negative PE offset rejected" );
        corrupt = bytes;
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>( corrupt.data( ) + reinterpret_cast<IMAGE_DOS_HEADER*>( corrupt.data( ) )->e_lfanew );
        IMAGE_FIRST_SECTION( nt )->PointerToRawData = UINT32_MAX;
        check( bool( pe::inspect( corrupt, invalid ) ), "Raw section overflow rejected" );
        corrupt = bytes;
        nt = reinterpret_cast<IMAGE_NT_HEADERS64*>( corrupt.data( ) + reinterpret_cast<IMAGE_DOS_HEADER*>( corrupt.data( ) )->e_lfanew );
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = { UINT32_MAX, 80 };
        check( bool( pe::inspect( corrupt, invalid ) ), "Directory overflow rejected" );
        const auto unicode_dll = temp / L"biblioteca-ação.dll";
        std::filesystem::copy_file( dll, unicode_dll, std::filesystem::copy_options::overwrite_existing );
        check( !pe::read( win::utf8( unicode_dll.wstring( ) ), corrupt, invalid ), "Unicode DLL path" );

        profile::settings profile; profile.dll_path = win::utf8( unicode_dll.wstring( ) ); profile.method = 1; profile.flags.reduce_motion = true;
        const auto profile_path = temp / L"perfil-ação.ini";
        check( !profile::save( profile_path, profile ), "Save Unicode profile" );
        profile::settings loaded;
        check( !profile::load( profile_path, loaded ) && loaded.dll_path == profile.dll_path && loaded.method == 1 && loaded.flags.reduce_motion, "Profile round trip" );
        check( !profile::load( profile_path, loaded ) && loaded.dll_path == profile.dll_path, "Repeated profile load replaces state" );
        { std::ofstream bad( profile_path ); bad << "version=999\nmethod=-1\n"; }
        const auto previous = loaded.dll_path;
        check( bool( profile::load( profile_path, loaded ) ) && loaded.dll_path == previous, "Invalid profile leaves state unchanged" );

        check( process::query_architecture( GetCurrentProcess( ) ) == process::architecture::x64, "Current process architecture" );
        check( process::refresh( ), "Process snapshot" );
        const auto list = process::list( );
        check( std::any_of( list.begin( ), list.end( ), []( const auto& p ) { return p.pid == GetCurrentProcessId( ) && p.creation_time != 0; } ), "Real PID identity" );
        const auto mods = modules::inspect( GetCurrentProcessId( ) );
        check( !mods.failure && modules::find( mods, L"kernel32.dll" ) != 0, "Module enumeration and full base" );
        check( bool( modules::inspect( 0 ).failure ), "Invalid module target rejected" );
        check( modules::inspect( GetCurrentProcessId( ), process::creation_time( GetCurrentProcess( ) ) + 1 ).failure.code == ERROR_INVALID_HANDLE,
            "Module snapshot rejects stale PID identity" );
        wchar_t windows[32768]{}; GetWindowsDirectoryW( windows, 32768 );
        pe::image image32; std::vector<std::uint8_t> bytes32;
        check( !pe::read( win::utf8( ( std::filesystem::path( windows ) / L"SysWOW64/kernel32.dll" ).wstring( ) ), bytes32, image32 ) &&
            image32.arch == process::architecture::x86 && image32.is_dll, "Inspect a real PE32 DLL" );
        DWORD handles_before = 0, handles_after = 0;
        GetProcessHandleCount( GetCurrentProcess( ), &handles_before );
        for ( int i = 0; i < 30; ++i ) (void)modules::inspect( GetCurrentProcessId( ) );
        GetProcessHandleCount( GetCurrentProcess( ), &handles_after );
        check( handles_before == handles_after, "No handle leak during repeated module snapshots" );
        process::request_refresh( );
        std::jthread reader( []( ) { for ( int i = 0; i < 100; ++i ) { (void)process::list( ); (void)process::last_refresh_ms( ); } } );
        reader.join( ); process::shutdown( );
        check( !process::list( ).empty( ), "Background process refresh and concurrent readers" );

        for ( auto method : { inject::method::load_library, inject::method::manual_map, inject::method::thread_hijack } )
        {
            child instance( target );
            auto o = request( instance, unicode_dll, method );
            check( inject::start( o ), "Start loading operation" );
            const auto result = finish( );
            if ( result.state != inject::phase::succeeded ) throw std::runtime_error( std::string( "Loading failed: " ) + result.message );
            check( WaitForSingleObject( instance.marker, 1000 ) == WAIT_OBJECT_0, "DllMain executed in owned child" );
            Sleep( 100 );
            check( WaitForSingleObject( instance.process, 0 ) == WAIT_TIMEOUT, "Target alive after load" );
            const auto modules = modules::inspect( instance.pid );
            if ( method != inject::method::manual_map ) check( modules::find( modules, unicode_dll.wstring( ) ) != 0, "Loaded DLL visible to module snapshot" );
            SetEvent( instance.stop );
            DWORD exit_code = STILL_ACTIVE;
            check( WaitForSingleObject( instance.process, 2000 ) == WAIT_OBJECT_0 && GetExitCodeProcess( instance.process, &exit_code ) && exit_code == 0,
                "Target main thread resumes and exits normally" );
        }
        {
            child instance( target, true );
            auto o = request( instance, dll, inject::method::manual_map );
            check( inject::start( o ), "Start target with dynamic-code prohibition" );
            check( finish( ).error_code == ERROR_ACCESS_DISABLED_BY_POLICY && WaitForSingleObject( instance.marker, 0 ) == WAIT_TIMEOUT,
                "Target mitigation respected before remote execution" );
        }
        {
            child instance( target ); auto o = request( instance, dll, inject::method::load_library );
            o.how = static_cast<inject::method>( -1 );
            check( inject::start( o ) && finish( ).error_code == ERROR_INVALID_PARAMETER, "Invalid method rejected" );
            o = request( instance, temp / L"missing.dll", inject::method::load_library );
            check( inject::start( o ) && finish( ).error_code == ERROR_FILE_NOT_FOUND, "Missing DLL reported with native error" );
        }
        {
            child instance( target ); auto o = request( instance, dll, inject::method::load_library ); o.delay_ms = 2000;
            check( inject::start( o ) && !inject::start( o ), "Concurrent start rejected" );
            Sleep( 50 ); inject::cancel( );
            check( finish( ).state == inject::phase::cancelled && WaitForSingleObject( instance.marker, 0 ) == WAIT_TIMEOUT, "Cancel before execution" );
        }
        {
            child instance( target ); auto o = request( instance, dll, inject::method::load_library ); ++o.target_creation_time;
            check( inject::start( o ), "Start stale identity request" );
            check( finish( ).error_code == ERROR_INVALID_HANDLE, "Reused/stale PID identity rejected" );
        }
        for ( auto method : { inject::method::load_library, inject::method::manual_map } )
        {
            child instance( target ); auto o = request( instance, slow, method ); o.timeout_ms = 100; o.auto_retry = true;
            inject::clear_log( ); check( inject::start( o ), "Start slow DLL" );
            check( finish( ).state == inject::phase::indeterminate, "Timeout has indeterminate result" );
            const auto lines = inject::snapshot( );
            check( std::none_of( lines.begin( ), lines.end( ), []( const auto& line ) { return strstr( line.text, "Retrying" ) != nullptr; } ), "No automatic retry after timeout" );
            check( WaitForSingleObject( instance.marker, 4000 ) == WAIT_OBJECT_0 && WaitForSingleObject( instance.process, 0 ) == WAIT_TIMEOUT, "Timed-out execution can finish without freed-buffer crash" );
        }
        {
            child instance( target ); auto o = request( instance, reject, inject::method::manual_map );
            check( inject::start( o ), "Start rejecting DLL" );
            check( finish( ).state == inject::phase::failed && !inject::dll_loaded( ), "DllMain FALSE reported as failure" );
        }
        if ( argc > 5 )
        {
            child instance( argv[5] );
            check( process::query_architecture( instance.process ) == process::architecture::x86, "WOW64 target architecture" );
            auto o = request( instance, dll, inject::method::load_library );
            check( inject::start( o ), "Start mismatched real target" );
            check( finish( ).error_code == ERROR_EXE_MACHINE_TYPE_MISMATCH, "x64 DLL / x86 process blocked" );
        }
        {
            child instance( target ); auto o = request( instance, dll, inject::method::load_library ); o.delay_ms = 2000;
            check( inject::start( o ), "Start shutdown test" );
            const auto before = GetTickCount64( ); inject::shutdown( );
            check( !inject::running( ) && GetTickCount64( ) - before < 1000, "Shutdown joins and cancels worker" );
        }
        // Remove only files created by this run, from its unique temporary directory.
        std::filesystem::remove( unicode_dll ); std::filesystem::remove( profile_path ); std::filesystem::remove( temp );
        std::cout << checks << " checks passed\n";
        return 0;
    }
    catch ( const std::exception& error )
    {
        std::cerr << "FAIL " << error.what( ) << '\n';
        for ( const auto& line : inject::snapshot( ) ) std::cerr << line.text << '\n';
        inject::shutdown( ); process::shutdown( );
        return 1;
    }
}
