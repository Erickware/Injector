#pragma once
#include "win32.hpp"
#include <filesystem>

namespace profile
{
    struct preferences
    {
        bool auto_retry = false;
        bool injection_alerts = true;
        bool error_alerts = true;
        bool sound_effects = false;
        bool enable_animations = true;
        bool reduce_motion = false;
        bool save_log_to_file = true;
        bool verbose_logging = true;
    };

    // Configuration model adapted from Xenos ProfileMgr::ConfigData.
    // Copyright (c) 2013 DarthTon. MIT: ../../licenses/Xenos.txt.
    // Serialization and UI preferences are implemented by Injector.
    struct settings
    {
        std::string dll_path;
        int method = 0;
        int delay_ms = 150;
        int timeout_ms = 15000;
        int opacity = 100;
        int accent = 0;
        int retention_days = 14;
        preferences flags;
    };
    inline settings current;
    std::filesystem::path default_path( );
    win::error save( const std::filesystem::path& path, const settings& value );
    win::error load( const std::filesystem::path& path, settings& value );
}
