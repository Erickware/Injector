#include "diagnostics.hpp"
#include "profile.hpp"
#include "../inject.hpp"
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <filesystem>

namespace inject::detail
{
    std::mutex lock;
    std::mutex file_mutex;
    std::mutex config_mutex;
    std::vector<entry> ring;
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

        std::string base_name( const char* path )
        {
            const std::string value = path ? path : "";
            const auto at = value.find_last_of( "\\/" );
            return at == std::string::npos ? value : value.substr( at + 1 );
        }

        void win32_fail( const char* what, char* why, std::size_t why_size )
        {
            const auto failure = win::failure( what );
            _snprintf_s( why, why_size, _TRUNCATE, "%s", failure.message.c_str( ) );
        }

        // ---- log file ------------------------------------------------------

        std::string log_file_path( )
        {
            const auto directory = profile::default_path( ).parent_path( );
            std::error_code ec;
            if ( !directory.empty( ) ) std::filesystem::create_directories( directory, ec );
            return win::utf8( ( directory / L"injector.log" ).wstring( ) );
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
            std::lock_guard file_guard( file_mutex );
            const std::string path = log_file_path( );
            FILE* file = nullptr;
            if ( _wfopen_s( &file, to_wide( path.c_str( ) ).c_str( ), L"rb" ) != 0 || !file )
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
            if ( _wfopen_s( &out, to_wide( path.c_str( ) ).c_str( ), L"wb" ) == 0 && out )
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
            std::lock_guard file_guard( file_mutex );

            std::time_t now = std::time( nullptr );
            std::tm tm { };
            localtime_s( &tm, &now );

            FILE* file = nullptr;
            if ( _wfopen_s( &file, to_wide( log_file_path( ).c_str( ) ).c_str( ), L"ab" ) != 0 || !file )
            {
                static bool reported = false; // file_mutex protects this flag
                if ( !reported )
                {
                    reported = true;
                    entry failure{}; failure.level = error;
                    strcpy_s( failure.time, time_text );
                    strcpy_s( failure.text, "Cannot write injector.log in LocalAppData/Injector" );
                    std::lock_guard guard( lock );
                    if ( ring.size( ) >= ring_cap ) ring.erase( ring.begin( ) );
                    ring.push_back( failure );
                }
                return;
            }
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


}
namespace inject
{
    using namespace detail;
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
        std::lock_guard configuration_guard( config_mutex );
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

}
