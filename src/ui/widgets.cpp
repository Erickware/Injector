#include "widgets.hpp"

#include "fonts.hpp"
#include "theme.hpp"
#include "lazyui/runtime.hpp"

#include <cstdio>
#include <string>
#include <unordered_map>

namespace ui::widgets
{
    using namespace lazyui;

    namespace
    {
        // Custom focus-safe replacement for the runtime's edit_search/edit_field.
        // The runtime helpers queue keyboard focus from inside the input helper
        // itself, which on this ImGui build never activates the widget (measured:
        // click arrives at the field, anyItemActive stays 0, typed characters die
        // unconsumed). Editing the runtime is not allowed, so the text fields that
        // need to work own their state here and talk to ImGui directly, exactly
        // the way a bare InputText with a scoped SetKeyboardFocusHere does — that
        // path was verified to activate and to accept characters.
        struct text_edit
        {
            char buffer[128]{ };
            float focus{ };
            bool seeded{ };
        };

        std::unordered_map< void*, text_edit >& edit_states( )
        {
            static std::unordered_map< void*, text_edit > states;
            return states;
        }

        // Self-contained search/field editor; the InputText is instantiated every
        // frame (so typed text renders even when unfocused) and only the focus
        // request is click-gated. Returns true when the text changed this frame.
        bool text_field( void* key, char* value, std::size_t size, ImVec2 at, ImVec2 max, int font_slot, ImVec2 text_pos )
        {
            text_edit& state = edit_states( )[key];
            if ( !state.seeded )
            {
                if ( value && size > 0 )
                {
                    strncpy_s( state.buffer, sizeof( state.buffer ), value, sizeof( state.buffer ) - 1 );
                    state.buffer[sizeof( state.buffer ) - 1] = '\0';
                }
                state.seeded = true;
                // Focus the editor on its first frame of life. Click-time
                // SetKeyboardFocusHere queues never take effect on this ImGui
                // build (measured: gate passes, anyItemActive stays 0), while
                // creation-time focus activates reliably. The result matches the
                // UX goal anyway: a freshly opened page has its search ready.
                ImGui::SetKeyboardFocusHere( );
            }
            char field_id[160];
            std::snprintf( field_id, sizeof field_id, "##ownfield_%p", key );

            const bool hovered = pointer_over( at, max );
            if ( hovered )
                ImGui::SetMouseCursor( ImGuiMouseCursor_TextInput );
            if ( hovered && pointer.clicked && !ImGui::IsAnyItemActive( ) )
                ImGui::SetKeyboardFocusHere( );

            ImGui::SetCursorScreenPos( text_pos );
            ImGui::SetNextItemWidth( max.x - at.x - 18.f );
            ImGui::PushStyleColor( ImGuiCol_FrameBg, 0x00000000 );
            ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, 0x00000000 );
            ImGui::PushStyleColor( ImGuiCol_FrameBgActive, 0x00000000 );
            ImGui::PushStyleColor( ImGuiCol_Text, theme::text_subtle );
            ImFont* picked = ui::slot_font( font_slot );
            if ( picked )
                ImGui::PushFont( picked, ui::slot_font_size( font_slot ) );
            const bool changed = ImGui::InputText( field_id, state.buffer, sizeof( state.buffer ) - 1 );
            const bool active = ImGui::IsItemActive( );
            if ( picked )
                ImGui::PopFont( );
            ImGui::PopStyleColor( 4 );

            if ( changed && value && size > 0 )
            {
                strncpy_s( value, size, state.buffer, size - 1 );
                value[size - 1] = '\0';
            }
            animate_to( state.focus, active ? 1.f : 0.f, motion_controls );
            return changed;
        }

        // A row that must keep the runtime's switch state across frames needs a stable
        // address behind the bool. These live for the process, so a caller that cannot
        // own one can still hand a persistent flag to checkbox_2.
        bool& persistent_flag( const char* id )
        {
            static std::unordered_map< std::string, bool > flags;
            return flags[std::string( id ? id : "" )];
        }
    }

    bool search_box( const char* label, char* value, std::size_t size )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        const float width = row_width( 0.f );
        text_edit& state = edit_states( )[value];
        add_rounded_rect_filled( draw, at, at + ImVec2{ width, 36.f }, corner_radii{ 10.f }, mix_color( theme::surface, theme::surface_7, state.focus ) );
        const bool changed = text_field( value, value, size, at, at + ImVec2{ width, 36.f }, fonts::manrope_extralight_400_12, at + ImVec2{ 40.f, 8.f } );
        draw_icon_glyph( draw, at + ImVec2{ 15.f, 10.f }, theme::icon_muted, icons::search_glyph );
        if ( state.buffer[0] == '\0' )
            draw_text( draw, fonts::manrope_extralight_400_12, at + ImVec2{ 40.f, 10.f }, theme::icon_muted, label );
        add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ width - 0.5f, 35.5f }, corner_radii{ 9.5f }, theme::accent_a60( ), 1.f );
        item( ImVec2{ width, 36.f } );
        return changed;
    }

    bool combo( const char* label, int& value, const char* const* items, int count )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        combo_state& state = combo_for( value );
        const float width = row_width( 14.f );
        update_combo( state, value, at + ImVec2{ width - 167.f, 0.f }, at + ImVec2{ width, 30.f }, count );
        draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 0.f, 7.f }, theme::text_5, label );
        add_rounded_rect_filled( draw, at + ImVec2{ width - 167.f, 0.f }, at + ImVec2{ width, 30.f }, corner_radii{ 6.f }, theme::surface_2 );
        draw_text( draw, fonts::manrope_extralight_500_11, at + ImVec2{ width - 157.f, 7.f }, theme::text_5, items[value] );
        {
            const int first_vertex = draw->VtxBuffer.Size;
            draw_icon_glyph( draw, at + ImVec2{ width - 24.f, 8.f }, theme::icon_muted, icons::chevron_down_glyph );
            rotate_vertices( draw, first_vertex, at + ImVec2{ width - 17.f, 15.f }, state.anim * IM_PI );
        }
        add_rounded_rect_stroke( draw, at + ImVec2{ width - 166.5f, 0.5f }, at + ImVec2{ width - 0.5f, 29.5f }, corner_radii{ 5.5f }, theme::accent( ), 1.f );
        const bool changed = draw_combo_popup( ImGui::GetForegroundDrawList( ), state, value, at + ImVec2{ width - 167.f, 0.f }, at + ImVec2{ width, 30.f }, items, count, fonts::manrope_extralight_500_11, 10.f, 18.726f, 6.f, theme::surface_2, theme::accent( ), theme::text_5, theme::text_5 );
        item( ImVec2{ width, 30.f } );
        return changed;
    }

    bool input_text( char* value, std::size_t size, const char* text )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        field_state& state = field_for( value );
        const float width = row_width( 14.f );
        add_rounded_rect_filled( draw, at, at + ImVec2{ width, 30.f }, corner_radii{ 10.f }, mix_color( theme::surface_2, theme::fill_blue( ), ImMax( state.focus, state.hover * 0.5f ) ) );
        const bool changed = edit_field( state, value, size, fonts::manrope_extralight_400_11, at + ImVec2{ 12.f, 18.726f }, width - 18.f, at, at + ImVec2{ width, 30.f }, theme::icon_muted, false );
        if ( !state.touched ) draw_text( draw, fonts::manrope_extralight_400_11, at + ImVec2{ 12.f, 7.f }, theme::icon_muted, text );
        add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ width - 0.5f, 29.5f }, corner_radii{ 9.5f }, theme::accent( ), 1.f );
        item( ImVec2{ width, 30.f } );
        return changed;
    }

    bool button( const char* label )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        button_state& state = button_for( label );
        const bool changed = update_button( state, at, at + ImVec2{ 178.f, 32.f } );
        add_rounded_rect_filled( draw, at, at + ImVec2{ 178.f, 32.f }, corner_radii{ 10.f }, mix_color( mix_color( theme::surface_2, theme::surface_3, state.hover ), theme::surface_4, state.press ) );
        draw_icon_glyph( draw, at + ImVec2{ 57.f, 8.5f }, theme::text, icons::folder_open_glyph );
        draw_text( draw, fonts::manrope_extralight_600_12, at + ImVec2{ 80.f, 8.f }, theme::text, label );
        add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 177.5f, 31.5f }, corner_radii{ 9.5f }, theme::accent( ), 1.f );
        item( ImVec2{ 178.f, 32.f } );
        return changed;
    }

    bool button_2( const char* label )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        button_state& state = button_for( label );
        const bool changed = update_button( state, at, at + ImVec2{ 177.f, 32.f } );
        add_rounded_rect_filled( draw, at, at + ImVec2{ 177.f, 32.f }, corner_radii{ 10.f }, mix_color( mix_color( theme::icon_blue( ), theme::fill_blue_4( ), state.hover ), theme::fill_blue_5( ), state.press ) );
        draw_icon_glyph( draw, at + ImVec2{ 61.f, 8.5f }, theme::icon, icons::play_glyph );
        draw_text( draw, fonts::manrope_extralight_600_12, at + ImVec2{ 84.f, 8.f }, theme::icon, label );
        item( ImVec2{ 177.f, 32.f } );
        return changed;
    }

    bool search_box_2( const char* label, char* value, std::size_t size )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        const float width = row_width( 14.f );
        text_edit& state = edit_states( )[value];
        add_rounded_rect_filled( draw, at, at + ImVec2{ width, 30.f }, corner_radii{ 10.f }, mix_color( theme::surface_2, theme::fill_blue( ), state.focus ) );
        const bool changed = text_field( value, value, size, at, at + ImVec2{ width, 30.f }, fonts::manrope_extralight_400_11, at + ImVec2{ 12.f, 6.f } );
        if ( state.buffer[0] == '\0' )
            draw_text( draw, fonts::manrope_extralight_400_11, at + ImVec2{ 12.f, 7.f }, theme::icon_muted, label );
        add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ width - 0.5f, 29.5f }, corner_radii{ 9.5f }, theme::accent( ), 1.f );
        item( ImVec2{ width, 30.f } );
        return changed;
    }

    bool button_3( const char* label )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        button_state& state = button_for( label );
        const bool changed = update_button( state, at, at + ImVec2{ 363.f, 32.f } );
        add_rounded_rect_filled( draw, at, at + ImVec2{ 363.f, 32.f }, corner_radii{ 10.f }, mix_color( mix_color( theme::surface_2, theme::surface_3, state.hover ), theme::surface_4, state.press ) );
        draw_icon_glyph( draw, at + ImVec2{ 148.f, 8.5f }, theme::text, icons::refresh_cw_glyph );
        draw_text( draw, fonts::manrope_extralight_600_12, at + ImVec2{ 171.f, 8.f }, theme::text, label );
        add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 362.5f, 31.5f }, corner_radii{ 9.5f }, theme::accent( ), 1.f );
        item( ImVec2{ 363.f, 32.f } );
        return changed;
    }

    bool checkbox( const char* label, bool& value )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        switch_state& state = switch_for( value );
        const bool changed = update_switch( state, value, at + ImVec2{ -4.f, -3.805f }, at + ImVec2{ 28.f + text_width( fonts::manrope_extralight_500_12, label ), 20.195f } );
        add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 1.f }, at + ImVec2{ 14.f, 15.f }, corner_radii{ 4.f }, mix_color( mix_color( theme::fill_blue_2( ), theme::icon_blue( ), state.anim ), theme::fill_blue_3( ), state.hover * ( 1.f - state.anim ) ) );
        draw_icon_glyph( draw, at + ImVec2{ 1.5f, 2.5f }, fade( theme::icon, state.anim ), icons::check_glyph );
        add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 1.5f }, at + ImVec2{ 13.5f, 14.5f }, corner_radii{ 3.5f }, mix_color( theme::accent( ), IM_COL32( 30, 117, 252, 0 ), state.anim ), 1.f );
        draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 24.f, 0.f }, mix_color( theme::text_6, theme::text, ImMax( state.anim, state.hover * 0.35f ) ), label );
        item( ImVec2{ 143.4f, 16.39f } );
        return changed;
    }

    bool checkbox_2( const char* label, bool& value )
    {
        return checkbox_2( label, value, ImGui::GetCursorScreenPos( ), ImVec2{ 0.f, 0.f } );
    }

    // hit_min/hit_size widen the click target beyond the 14px box; a zero size keeps the
    // design's own measurement, which follows the label width. box_at places the 14px box
    // itself; when it is zero the cursor position is used, which is what the design wants
    // for a checkbox that flows with the layout.
    bool checkbox_2( const char* label, bool& value, ImVec2 hit_min, ImVec2 hit_size )
    {
        return checkbox_2( label, value, hit_min, hit_size, ImVec2{ 0.f, 0.f } );
    }

    bool checkbox_2( const char* label, bool& value, ImVec2 hit_min, ImVec2 hit_size, ImVec2 box_at )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 cursor = ImGui::GetCursorScreenPos( );
        const bool placed = box_at.x > 0.f || box_at.y > 0.f;
        const ImVec2 at = placed ? box_at : cursor;
        switch_state& state = switch_for( value );
        const ImVec2 box_min = hit_size.x > 0.f ? hit_min : at + ImVec2{ -4.f, -3.805f };
        const ImVec2 box_max = hit_size.x > 0.f ? hit_min + hit_size : at + ImVec2{ 28.f + text_width( fonts::manrope_extralight_500_12, label ), 20.195f };
        const bool changed = update_switch( state, value, box_min, box_max );
        add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 1.f }, at + ImVec2{ 14.f, 15.f }, corner_radii{ 4.f }, mix_color( mix_color( theme::fill_blue_2( ), theme::icon_blue( ), state.anim ), theme::fill_blue_3( ), state.hover * ( 1.f - state.anim ) ) );
        add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 1.5f }, at + ImVec2{ 13.5f, 14.5f }, corner_radii{ 3.5f }, mix_color( theme::accent( ), theme::icon_blue( ), state.anim ), 1.f );
        draw_icon_glyph( draw, at + ImVec2{ 1.5f, 2.5f }, fade( theme::icon, state.anim ), icons::check_glyph );

        // A caller that supplies its own hit box paints the row itself, so the label is
        // only drawn when the widget also owns the hit area. Otherwise the text would land
        // on top of whatever the row already drew at the same coordinates.
        if ( hit_size.x <= 0.f && label[0] != '\0' )
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 24.f, 0.f }, mix_color( theme::text_6, theme::text, ImMax( state.anim, state.hover * 0.35f ) ), label );

        // The layout item is always submitted, pinned rows included: ImGui 1.92 errors
        // ("Code uses SetCursorPos() to extend window/parent boundaries") when the last
        // operation in a window is SetCursorScreenPos without an item afterwards, and the
        // per-row set_cursor keeps the cursor where the row loop wants it regardless of
        // the reset this item performs.
        item( ImVec2{ 170.35f, 16.39f } );
        return changed;
    }

    bool button_4( const char* label )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        button_state& state = button_for( label );
        const bool changed = update_button( state, at, at + ImVec2{ 243.f, 32.f } );
        add_rounded_rect_filled( draw, at, at + ImVec2{ 243.f, 32.f }, corner_radii{ 10.f }, mix_color( mix_color( theme::surface_2, theme::surface_3, state.hover ), theme::surface_4, state.press ) );
        draw_icon_glyph( draw, at + ImVec2{ 83.f, 8.5f }, theme::text, icons::trash_glyph );
        draw_text( draw, fonts::manrope_extralight_600_12, at + ImVec2{ 106.f, 8.f }, theme::text, label );
        add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 242.5f, 31.5f }, corner_radii{ 9.5f }, theme::accent( ), 1.f );
        item( ImVec2{ 243.f, 32.f } );
        return changed;
    }

    bool combo_2( const char* label, int& value, const char* const* items, int count )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        combo_state& state = combo_for( value );
        const float width = row_width( 14.f );
        update_combo( state, value, at + ImVec2{ width - 170.f, 0.f }, at + ImVec2{ width, 30.f }, count );
        draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 0.f, 7.f }, theme::text_5, label );
        add_rounded_rect_filled( draw, at + ImVec2{ width - 170.f, 0.f }, at + ImVec2{ width, 30.f }, corner_radii{ 6.f }, theme::surface_2 );
        draw_text( draw, fonts::manrope_extralight_500_11, at + ImVec2{ width - 160.f, 7.f }, theme::text_5, items[value] );
        {
            const int first_vertex = draw->VtxBuffer.Size;
            draw_icon_glyph( draw, at + ImVec2{ width - 24.f, 8.f }, theme::icon_muted, icons::chevron_down_glyph );
            rotate_vertices( draw, first_vertex, at + ImVec2{ width - 17.f, 15.f }, state.anim * IM_PI );
        }
        add_rounded_rect_stroke( draw, at + ImVec2{ width - 169.5f, 0.5f }, at + ImVec2{ width - 0.5f, 29.5f }, corner_radii{ 5.5f }, theme::accent( ), 1.f );
        const bool changed = draw_combo_popup( ImGui::GetForegroundDrawList( ), state, value, at + ImVec2{ width - 170.f, 0.f }, at + ImVec2{ width, 30.f }, items, count, fonts::manrope_extralight_500_11, 10.f, 18.726f, 6.f, theme::surface_2, theme::accent( ), theme::text_5, theme::text_5 );
        item( ImVec2{ width, 30.f } );
        return changed;
    }

    bool toggle( const char* label, bool& value )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        switch_state& state = switch_for( value );
        const float width = row_width( 14.f );
        const bool changed = update_switch( state, value, at + ImVec2{ -4.f, -4.f }, at + ImVec2{ width + 4.f, 22.f } );
        draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 0.f, 1.f }, mix_color( theme::text_6, theme::text, ImMax( state.anim, state.hover * 0.35f ) ), label );
        add_rounded_rect_filled( draw, at + ImVec2{ width - 30.f, 0.f }, at + ImVec2{ width, 18.f }, corner_radii{ 9.f }, mix_color( theme::fill_blue_a40( ), theme::fill_blue_a40_2( ), state.anim ) );
        add_ellipse_filled( draw, at + ImVec2{ width - 21.f + 12.f * state.anim, 9.f }, ImVec2{ 5.f, 5.f }, mix_color( theme::fill_blue_6( ), theme::icon_blue( ), state.anim ) );
        item( ImVec2{ width, 18.f } );
        return changed;
    }

    bool slider( const char* label, float& value, float min, float max, const char* format )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        slider_state& state = slider_for( value, min, max );
        const float width = row_width( 14.f );
        const bool changed = update_slider( state, value, min, max, at + ImVec2{ 0.f, 18.f }, at + ImVec2{ width, 38.f }, at + ImVec2{ -6.f, 18.f }, at + ImVec2{ width + 6.f, 38.f } );
        draw_text( draw, fonts::manrope_extralight_500_12, at, theme::text_muted, label );
        draw_text( draw, fonts::manrope_extralight_500_11, at + ImVec2{ width + 0.088f, 0.f }, theme::text_muted, format_value( format, value ), align_right );
        add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 25.f }, at + ImVec2{ width, 31.f }, corner_radii{ 3.f }, theme::surface_5 );
        draw_image_sliced( draw, 1, at + ImVec2{ 0.f, 25.f }, at + ImVec2{ 0.4f + ImMax( 0.f, width * state.shown ), 31.f }, 49.f, 5.f, IM_COL32_WHITE );
        add_ellipse_filled( draw, at + ImVec2{ width * state.shown, 28.f }, ImVec2{ 6.f, 6.f }, theme::text_muted );
        add_ellipse_stroke( draw, at + ImVec2{ width * state.shown, 28.f }, ImVec2{ 5.5f, 5.5f }, theme::surface, 1.f );
        item( ImVec2{ width, 34.f } );
        return changed;
    }

    bool slider_2( const char* label, float& value, float min, float max, const char* format )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        slider_state& state = slider_for( value, min, max );
        const float width = row_width( 14.f );
        const bool changed = update_slider( state, value, min, max, at + ImVec2{ 0.f, 18.f }, at + ImVec2{ width, 38.f }, at + ImVec2{ -6.f, 18.f }, at + ImVec2{ width + 6.f, 38.f } );
        draw_text( draw, fonts::manrope_extralight_500_12, at, theme::text_muted, label );
        draw_text( draw, fonts::manrope_extralight_500_11, at + ImVec2{ width + 0.022f, 0.f }, theme::text_muted, format_value( format, value ), align_right );
        add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 25.f }, at + ImVec2{ width, 31.f }, corner_radii{ 3.f }, theme::surface_5 );
        draw_image_sliced( draw, 2, at + ImVec2{ 0.f, 25.f }, at + ImVec2{ 0.6f + ImMax( 0.f, width * state.shown ), 31.f }, 170.f, 5.f, IM_COL32_WHITE );
        add_ellipse_filled( draw, at + ImVec2{ width * state.shown, 28.f }, ImVec2{ 6.f, 6.f }, theme::text_muted );
        add_ellipse_stroke( draw, at + ImVec2{ width * state.shown, 28.f }, ImVec2{ 5.5f, 5.5f }, theme::surface, 1.f );
        item( ImVec2{ width, 34.f } );
        return changed;
    }

    bool slider_3( const char* label, float& value, float min, float max, const char* format )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        slider_state& state = slider_for( value, min, max );
        const float width = row_width( 14.f );
        const bool changed = update_slider( state, value, min, max, at + ImVec2{ 0.f, 18.f }, at + ImVec2{ width, 38.f }, at + ImVec2{ -6.f, 18.f }, at + ImVec2{ width + 6.f, 38.f } );
        draw_text( draw, fonts::manrope_extralight_500_12, at, theme::text_muted, label );
        draw_text( draw, fonts::manrope_extralight_500_11, at + ImVec2{ width + 0.1055f, 0.f }, theme::text_muted, format_value( format, value ), align_right );
        add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 25.f }, at + ImVec2{ width, 31.f }, corner_radii{ 3.f }, theme::surface_5 );
        draw_image_sliced( draw, 3, at + ImVec2{ 0.f, 25.f }, at + ImVec2{ 0.44f + ImMax( 0.f, width * state.shown ), 31.f }, 224.f, 5.f, IM_COL32_WHITE );
        add_ellipse_filled( draw, at + ImVec2{ width * state.shown, 28.f }, ImVec2{ 6.f, 6.f }, theme::text_muted );
        add_ellipse_stroke( draw, at + ImVec2{ width * state.shown, 28.f }, ImVec2{ 5.5f, 5.5f }, theme::surface, 1.f );
        item( ImVec2{ width, 34.f } );
        return changed;
    }

    bool combo_3( const char* label, int& value, const char* const* items, int count )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        combo_state& state = combo_for( value );
        const float width = row_width( 14.f );
        update_combo( state, value, at + ImVec2{ width - 112.f, 0.f }, at + ImVec2{ width, 30.f }, count );
        draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 0.f, 7.f }, theme::text_5, label );
        add_rounded_rect_filled( draw, at + ImVec2{ width - 112.f, 0.f }, at + ImVec2{ width, 30.f }, corner_radii{ 6.f }, theme::surface_2 );
        draw_text( draw, fonts::manrope_extralight_500_11, at + ImVec2{ width - 102.f, 7.f }, theme::text_5, items[value] );
        {
            const int first_vertex = draw->VtxBuffer.Size;
            draw_icon_glyph( draw, at + ImVec2{ width - 24.f, 8.f }, theme::icon_muted, icons::chevron_down_glyph );
            rotate_vertices( draw, first_vertex, at + ImVec2{ width - 17.f, 15.f }, state.anim * IM_PI );
        }
        add_rounded_rect_stroke( draw, at + ImVec2{ width - 111.5f, 0.5f }, at + ImVec2{ width - 0.5f, 29.5f }, corner_radii{ 5.5f }, theme::accent( ), 1.f );
        const bool changed = draw_combo_popup( ImGui::GetForegroundDrawList( ), state, value, at + ImVec2{ width - 112.f, 0.f }, at + ImVec2{ width, 30.f }, items, count, fonts::manrope_extralight_500_11, 10.f, 18.726f, 6.f, theme::surface_2, theme::accent( ), theme::text_5, theme::text_5 );
        item( ImVec2{ width, 30.f } );
        return changed;
    }

    bool button_5( const char* label, const char* icon )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        button_state& state = button_for( label );
        const bool changed = update_button( state, at, at + ImVec2{ 648.f, 32.f } );
        add_rounded_rect_filled( draw, at, at + ImVec2{ 648.f, 32.f }, corner_radii{ 10.f }, mix_color( mix_color( theme::surface_2, theme::surface_3, state.hover ), theme::surface_4, state.press ) );
        draw_icon_glyph( draw, at + ImVec2{ 268.f, 8.5f }, theme::text, icon );
        draw_text( draw, fonts::manrope_extralight_600_12, at + ImVec2{ 291.f, 8.f }, theme::text, label );
        add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 647.5f, 31.5f }, corner_radii{ 9.5f }, theme::accent( ), 1.f );
        item( ImVec2{ 648.f, 32.f } );
        return changed;
    }

    bool button_6( const char* label )
    {
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 at = ImGui::GetCursorScreenPos( );
        button_state& state = button_for( label );
        const bool changed = update_button( state, at, at + ImVec2{ 648.f, 32.f } );
        add_rounded_rect_filled( draw, at, at + ImVec2{ 648.f, 32.f }, corner_radii{ 10.f }, mix_color( mix_color( theme::surface_2, theme::surface_3, state.hover ), theme::surface_4, state.press ) );
        draw_icon_glyph( draw, at + ImVec2{ 260.f, 8.5f }, theme::text, icons::code_2_glyph );
        draw_text( draw, fonts::manrope_extralight_600_12, at + ImVec2{ 283.f, 8.f }, theme::text, label );
        add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 647.5f, 31.5f }, corner_radii{ 9.5f }, theme::accent( ), 1.f );
        item( ImVec2{ 648.f, 32.f } );
        return changed;
    }
}
