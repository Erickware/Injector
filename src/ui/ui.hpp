#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include <imgui.h>
#include "fonts.hpp"
#include "core/profile.hpp"

namespace ui
{
    using preferences = profile::preferences;
    inline preferences& prefs = profile::current.flags;

    struct variables
    {
        int page = 0;
        char dll_path[32768] = "";   // filled by the Browse dialog on the Inject page
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
        std::uint64_t target_creation_time = 0;
        char target_name[128] = "";
        char target_arch[8] = "";
    };

    inline variables vars;

    ImVec2 frame_size( );
    void render( );
    void load_settings( );
    void save_settings( );
}
