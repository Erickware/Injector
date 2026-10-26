#pragma once

#include <imgui.h>

namespace ui::theme
{
    inline constexpr float frame_width = 900.f;
    inline constexpr float frame_height = 812.f;

    inline constexpr ImU32 background = IM_COL32( 2, 5, 12, 255 );
    inline constexpr ImU32 surface = IM_COL32( 8, 14, 25, 255 );
    inline constexpr ImU32 surface_2 = IM_COL32( 11, 20, 35, 255 );
    inline constexpr ImU32 surface_3 = IM_COL32( 28, 36, 50, 255 );
    inline constexpr ImU32 surface_4 = IM_COL32( 10, 18, 31, 255 );
    inline constexpr ImU32 surface_5 = IM_COL32( 36, 46, 62, 255 );
    inline constexpr ImU32 surface_6 = IM_COL32( 5, 9, 18, 255 );
    inline constexpr ImU32 surface_7 = IM_COL32( 20, 26, 37, 255 );
    inline constexpr ImU32 text = IM_COL32( 238, 242, 249, 255 );
    inline constexpr ImU32 text_muted = IM_COL32( 220, 224, 231, 255 );
    inline constexpr ImU32 text_subtle = IM_COL32( 186, 191, 199, 255 );
    inline constexpr ImU32 text_faint = IM_COL32( 148, 157, 171, 255 );
    inline constexpr ImU32 text_5 = IM_COL32( 116, 136, 166, 255 );
    inline constexpr ImU32 text_6 = IM_COL32( 83, 100, 128, 255 );
    inline constexpr ImU32 icon = IM_COL32( 255, 255, 255, 255 );
    inline constexpr ImU32 icon_muted = IM_COL32( 74, 87, 108, 255 );

    // ---- accent system -----------------------------------------------------
    //
    // Settings > Appearance > Accent Color swaps this whole palette live: icons,
    // focus rings, buttons, switches, the scrollbar and the panel borders all
    // read through it. Blue is the design's original theme; Cyan and Violet are
    // hue-shifted siblings tuned inside the same contrast grid.

    struct accent_palette
    {
        ImU32 icon;        // icon color the panels use everywhere
        ImU32 icon_2;      // muted outline variant (selection frames)
        ImU32 border;      // structural border (panel strokes, separators)
        ImU32 border_a94;  // 94% alpha border
        ImU32 border_a60;  // 60% alpha border
        ImU32 fill;        // input focus fill
        ImU32 fill_2;      // element base fill (checkboxes, switches)
        ImU32 fill_3;      // hovered element fill (scrollbar thumb base)
        ImU32 fill_4;      // bright hover mix (buttons)
        ImU32 fill_5;      // pressed mix
        ImU32 fill_6;      // subtle knob / unchecked fill
        ImU32 fill_a40;    // 40% fill for highlight overlays
        ImU32 fill_a40_2;  // 40% icon-tinted overlay
    };

    inline constexpr accent_palette accent_blue{
        IM_COL32( 30, 117, 252, 255 ),  IM_COL32( 54, 100, 173, 255 ),
        IM_COL32( 21, 34, 56, 255 ),    IM_COL32( 21, 34, 56, 240 ),
        IM_COL32( 21, 34, 56, 153 ),
        IM_COL32( 23, 32, 46, 255 ),    IM_COL32( 23, 31, 45, 255 ),
        IM_COL32( 25, 50, 90, 255 ),    IM_COL32( 46, 127, 252, 255 ),
        IM_COL32( 26, 103, 222, 255 ),  IM_COL32( 35, 48, 71, 255 ),
        IM_COL32( 34, 46, 68, 102 ),    IM_COL32( 30, 117, 252, 102 )
    };

    inline constexpr accent_palette accent_cyan{
        IM_COL32( 6, 182, 212, 255 ),   IM_COL32( 44, 140, 170, 255 ),
        IM_COL32( 14, 48, 64, 255 ),    IM_COL32( 14, 48, 64, 240 ),
        IM_COL32( 14, 48, 64, 153 ),
        IM_COL32( 16, 42, 55, 255 ),    IM_COL32( 16, 41, 54, 255 ),
        IM_COL32( 18, 60, 82, 255 ),    IM_COL32( 16, 140, 172, 255 ),
        IM_COL32( 14, 120, 150, 255 ),  IM_COL32( 26, 54, 70, 255 ),
        IM_COL32( 24, 56, 72, 102 ),    IM_COL32( 6, 182, 212, 102 )
    };

    inline constexpr accent_palette accent_violet{
        IM_COL32( 168, 85, 247, 255 ),  IM_COL32( 124, 78, 190, 255 ),
        IM_COL32( 44, 28, 70, 255 ),    IM_COL32( 44, 28, 70, 240 ),
        IM_COL32( 44, 28, 70, 153 ),
        IM_COL32( 32, 26, 48, 255 ),    IM_COL32( 32, 25, 47, 255 ),
        IM_COL32( 46, 32, 74, 255 ),    IM_COL32( 150, 74, 224, 255 ),
        IM_COL32( 132, 64, 202, 255 ),  IM_COL32( 42, 32, 62, 255 ),
        IM_COL32( 42, 32, 66, 102 ),    IM_COL32( 168, 85, 247, 102 )
    };

    // Written by the frame loop from vars.accent_color (0 Blue, 1 Cyan, 2 Violet).
    inline int accent_choice = 0;

    inline const accent_palette& palette( )
    {
        static const accent_palette* const all[3] = { &accent_blue, &accent_cyan, &accent_violet };
        return *all[ accent_choice < 0 || accent_choice > 2 ? 0 : accent_choice ];
    }

    inline ImU32 icon_blue( )   { return palette( ).icon; }
    inline ImU32 icon_blue_2( ) { return palette( ).icon_2; }
    inline ImU32 accent( )      { return palette( ).border; }
    inline ImU32 accent_a94( )  { return palette( ).border_a94; }
    inline ImU32 accent_a60( )  { return palette( ).border_a60; }
    inline ImU32 fill_blue( )   { return palette( ).fill; }
    inline ImU32 fill_blue_2( ) { return palette( ).fill_2; }
    inline ImU32 fill_blue_3( ) { return palette( ).fill_3; }
    inline ImU32 fill_blue_4( ) { return palette( ).fill_4; }
    inline ImU32 fill_blue_5( ) { return palette( ).fill_5; }
    inline ImU32 fill_blue_6( ) { return palette( ).fill_6; }
    inline ImU32 fill_blue_a40( )   { return palette( ).fill_a40; }
    inline ImU32 fill_blue_a40_2( ) { return palette( ).fill_a40_2; }
}
