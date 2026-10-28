#include "ui.hpp"
#include "core/diagnostics.hpp"
#include "inject.hpp"
#include <algorithm>
#include <cstring>

namespace ui
{
    static profile::settings capture_settings( )
    {
        profile::settings value;
        value.flags = prefs;
        value.dll_path = vars.dll_path;
        value.method = vars.method;
        value.delay_ms = static_cast<int>( vars.injection_delay );
        value.opacity = static_cast<int>( vars.window_opacity );
        value.accent = vars.accent_color;
        value.retention_days = static_cast<int>( vars.log_retention );
        return value;
    }
    static void apply_settings( const profile::settings& value )
    {
        profile::current = value;
        strcpy_s( vars.dll_path, value.dll_path.c_str( ) );
        vars.method = value.method;
        vars.injection_delay = static_cast<float>( value.delay_ms );
        vars.window_opacity = static_cast<float>( value.opacity );
        vars.accent_color = value.accent;
        vars.log_retention = static_cast<float>( value.retention_days );
    }
    void load_settings( )
    {
        profile::settings value;
        const auto error = profile::load( profile::default_path( ), value );
        if ( !error ) apply_settings( value );
        else if ( error.code != ERROR_FILE_NOT_FOUND ) inject::detail::log( inject::warn, "%s", error.message.c_str( ) );
    }
    void save_settings( )
    {
        const auto error = profile::save( profile::default_path( ), capture_settings( ) );
        if ( error ) inject::detail::log( inject::warn, "%s", error.message.c_str( ) );
    }
}
