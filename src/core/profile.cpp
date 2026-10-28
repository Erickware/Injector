#include "profile.hpp"
#include <shlobj.h>
#include <atomic>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace profile
{
    namespace
    {
        win::error invalid( ) { return { ERROR_INVALID_DATA, "Invalid profile: unsupported version, missing field or value outside its limits" }; }
        bool valid( const settings& s )
        {
            return s.method >= 0 && s.method <= 2 && s.delay_ms >= 0 && s.delay_ms <= 2000 &&
                s.timeout_ms >= 100 && s.timeout_ms <= 120000 && s.opacity >= 12 && s.opacity <= 100 &&
                s.accent >= 0 && s.accent <= 2 && s.retention_days >= 0 && s.retention_days <= 30 &&
                s.dll_path.size( ) < 32768 && s.dll_path.find_first_of( "\r\n" ) == std::string::npos &&
                s.dll_path.find( '\0' ) == std::string::npos && ( s.dll_path.empty( ) || !win::wide( s.dll_path ).empty( ) );
        }
    }

    std::filesystem::path default_path( )
    {
        PWSTR directory = nullptr;
        if ( FAILED( SHGetKnownFolderPath( FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &directory ) ) ) return { };
        const auto path = std::filesystem::path( directory ) / L"Injector" / L"settings.ini";
        CoTaskMemFree( directory );
        return path;
    }

    win::error save( const std::filesystem::path& path, const settings& s )
    {
        if ( path.empty( ) ) return { ERROR_INVALID_NAME, "No profile path" };
        if ( !valid( s ) ) return invalid( );
        std::error_code ec;
        if ( !path.parent_path( ).empty( ) ) std::filesystem::create_directories( path.parent_path( ), ec );
        if ( ec ) return { static_cast<DWORD>( ec.value( ) ), "Cannot create profile directory: " + ec.message( ) };
        static std::atomic_uint sequence{ 0 };
        auto temporary = path;
        temporary += L".tmp." + std::to_wstring( GetCurrentProcessId( ) ) + L"." + std::to_wstring( ++sequence );
        std::ofstream stream( temporary, std::ios::binary | std::ios::trunc );
        if ( !stream ) return { ERROR_WRITE_FAULT, "Cannot open profile for writing" };
        stream << "version=1\ndll_path=" << std::quoted( s.dll_path ) << '\n';
#define WRITE_VALUE(key, value) stream << key "=" << (value) << '\n'
        WRITE_VALUE( "method", s.method ); WRITE_VALUE( "delay_ms", s.delay_ms ); WRITE_VALUE( "timeout_ms", s.timeout_ms );
        WRITE_VALUE( "opacity", s.opacity ); WRITE_VALUE( "accent", s.accent ); WRITE_VALUE( "retention_days", s.retention_days );
        WRITE_VALUE( "auto_retry", s.flags.auto_retry ); WRITE_VALUE( "injection_alerts", s.flags.injection_alerts );
        WRITE_VALUE( "error_alerts", s.flags.error_alerts ); WRITE_VALUE( "sound_effects", s.flags.sound_effects );
        WRITE_VALUE( "enable_animations", s.flags.enable_animations ); WRITE_VALUE( "reduce_motion", s.flags.reduce_motion );
        WRITE_VALUE( "save_log_to_file", s.flags.save_log_to_file ); WRITE_VALUE( "verbose_logging", s.flags.verbose_logging );
#undef WRITE_VALUE
        stream.close( );
        if ( stream.fail( ) ) { std::filesystem::remove( temporary, ec ); return { ERROR_WRITE_FAULT, "Could not write complete profile" }; }
        if ( !MoveFileExW( temporary.c_str( ), path.c_str( ), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) )
        {
            auto error = win::failure( "Replace profile" );
            std::filesystem::remove( temporary, ec );
            return error;
        }
        return { };
    }

    win::error load( const std::filesystem::path& path, settings& value )
    {
        std::error_code ec;
        if ( path.empty( ) || !std::filesystem::exists( path, ec ) ) return { ERROR_FILE_NOT_FOUND, "Profile not found" };
        if ( std::filesystem::file_size( path, ec ) > 65536 || ec ) return invalid( );
        std::ifstream stream( path, std::ios::binary );
        if ( !stream ) return { ERROR_READ_FAULT, "Cannot read profile" };
        std::map<std::string, std::string> values;
        std::string line;
        while ( std::getline( stream, line ) )
        {
            if ( !line.empty( ) && line.back( ) == '\r' ) line.pop_back( );
            if ( line.empty( ) || line[0] == '#' ) continue;
            const auto equal = line.find( '=' );
            if ( equal == std::string::npos || !values.emplace( line.substr( 0, equal ), line.substr( equal + 1 ) ).second ) return invalid( );
        }
        if ( stream.bad( ) ) return { ERROR_READ_FAULT, "Incomplete profile read" };
        settings candidate;
        auto number = [&]( const char* name, int& out )
        {
            const auto it = values.find( name ); if ( it == values.end( ) ) return false;
            auto [end, status] = std::from_chars( it->second.data( ), it->second.data( ) + it->second.size( ), out );
            return status == std::errc{ } && end == it->second.data( ) + it->second.size( );
        };
        auto boolean = [&]( const char* name, bool& out )
        { int i = -1; if ( !number( name, i ) || i < 0 || i > 1 ) return false; out = i != 0; return true; };
        int version = 0;
        if ( !number( "version", version ) || version != 1 || !values.count( "dll_path" ) ) return invalid( );
        std::istringstream path_value( values["dll_path"] );
        path_value >> std::quoted( candidate.dll_path );
        if ( !path_value || !(path_value >> std::ws).eof( ) ) return invalid( );
        if ( !number( "method", candidate.method ) || !number( "delay_ms", candidate.delay_ms ) ||
             !number( "timeout_ms", candidate.timeout_ms ) || !number( "opacity", candidate.opacity ) ||
             !number( "accent", candidate.accent ) || !number( "retention_days", candidate.retention_days ) ||
             !boolean( "auto_retry", candidate.flags.auto_retry ) || !boolean( "injection_alerts", candidate.flags.injection_alerts ) ||
             !boolean( "error_alerts", candidate.flags.error_alerts ) || !boolean( "sound_effects", candidate.flags.sound_effects ) ||
             !boolean( "enable_animations", candidate.flags.enable_animations ) || !boolean( "reduce_motion", candidate.flags.reduce_motion ) ||
             !boolean( "save_log_to_file", candidate.flags.save_log_to_file ) || !boolean( "verbose_logging", candidate.flags.verbose_logging ) ||
             !valid( candidate ) ) return invalid( );
        value = std::move( candidate );
        return { };
    }
}
