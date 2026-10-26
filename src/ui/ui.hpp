#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include <imgui.h>
#include "fonts.hpp"

namespace ui
{
    inline bool bools[ 14 ]{
        true,  // Cs2.exe • 4872 • x64
        false, // Chrome.exe • 8436 • x64
        false, // Explorer.exe • 6320 • x64
        false, // Svchost.exe • 4188 • x64
        false, // Discord.exe • 10244 • x64
        false, // Steam.exe • 7296 • x64
        false, // Auto Retry On Fail
        true,  // Injection Alerts
        true,  // Error Alerts
        false, // Sound Effects
        true,  // Enable Animations
        false, // Reduce Motion
        true,  // Save Log To File
        true   // Verbose Logging
    };

    struct variables
    {
        int page = 0;
        int target_process = 0;
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
