#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include <imgui.h>
#include "fonts.hpp"

namespace ui
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

    inline preferences prefs;

    struct variables
    {
        int page = 0;
        char dll_path[256] = "";   // filled by the Browse dialog on the Inject page
        char search_2[128] = "";
        int method = 0;
        float injection_delay = 150.f;
        float window_opacity = 100.f;
        int accent_color = 0;
        float log_retention = 14.f;

        // process list, filled by process::refresh() through ui::pages::tick()
        int process_count = 0;
        bool list_live = true;             // false freezes the list until Refresh is pressed
        bool target_selected = false;
        unsigned int target_pid = 0;
        char target_name[128] = "";
        char target_arch[8] = "";
    };

    inline variables vars;

    ImVec2 frame_size( );
    void render( );
}
