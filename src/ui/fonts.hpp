#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include <imgui.h>

namespace ui
{
    struct text_glyph
    {
        unsigned int codepoint;
        float x;
        float baseline;
    };

    bool load_fonts( );
    void reset_fonts( );
    bool fonts_ready( );

    void draw_glyph_run( ImDrawList* draw, int slot, ImVec2 origin, ImU32 color, const text_glyph* glyphs, int count );

    template < int count >
    void draw_glyph_run( ImDrawList* draw, int slot, ImVec2 origin, ImU32 color, const text_glyph ( &glyphs )[count] )
    {
        draw_glyph_run( draw, slot, origin, color, glyphs, count );
    }

    struct glyph_text
    {
        const text_glyph* glyphs;
        int count;

        template < int length >
        constexpr glyph_text( const text_glyph ( &table )[length] ) : glyphs( table ), count( length ) { }
    };

    inline void draw_glyph_run( ImDrawList* draw, int slot, ImVec2 origin, ImU32 color, glyph_text text )
    {
        draw_glyph_run( draw, slot, origin, color, text.glyphs, text.count );
    }

    enum text_align : int
    {
        align_left,
        align_center,
        align_right
    };

    void draw_text( ImDrawList* draw, int slot, ImVec2 position, ImU32 color, const char* text, int align = align_left );

    void draw_text_box( ImDrawList* draw, int slot, ImVec2 min, ImVec2 max, int horizontal, int vertical, ImU32 color, const char* text );
    void draw_text_line( ImDrawList* draw, int slot, ImVec2 anchor, int align, ImU32 color, const char* text );
    void draw_icon_glyph( ImDrawList* draw, ImVec2 position, ImU32 color, const char* glyph );

    ImFont* slot_font( int slot );
    float slot_font_size( int slot );
    float slot_font_ascent( int slot );

    namespace fonts
    {
        inline constexpr int manrope_extralight_600_12 = 0;
        inline constexpr int manrope_extralight_600_15 = 1;
        inline constexpr int manrope_extralight_500_11 = 2;
        inline constexpr int manrope_extralight_500_12 = 3;
        inline constexpr int manrope_extralight_500_18 = 4;
        inline constexpr int manrope_extralight_400_11 = 5;
        inline constexpr int manrope_extralight_400_12 = 6;
        inline constexpr int manrope_extralight_400_13 = 7;
    }

    namespace icons
    {
        extern const char syringe_glyph[];
        extern const char syringe_2_glyph[];
        extern const char settings_glyph[];
        extern const char info_glyph[];
        extern const char syringe_3_glyph[];
        extern const char search_glyph[];
        extern const char chevron_up_glyph[];
        extern const char chevron_down_glyph[];
        extern const char folder_open_glyph[];
        extern const char play_glyph[];
        extern const char list_glyph[];
        extern const char refresh_cw_glyph[];
        extern const char check_glyph[];
        extern const char activity_glyph[];
        extern const char scroll_text_glyph[];
        extern const char trash_glyph[];
        extern const char settings_2_glyph[];
        extern const char cpu_glyph[];
        extern const char bell_glyph[];
        extern const char palette_glyph[];
        extern const char info_2_glyph[];
        extern const char code_glyph[];
        extern const char heart_glyph[];
        extern const char scroll_text_2_glyph[];
        extern const char code_2_glyph[];
        extern const char bug_glyph[];
        extern const char discord_glyph[];
    }
}
