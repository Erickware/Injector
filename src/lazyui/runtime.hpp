#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <cfloat>
#include <cstddef>
#include <cstdint>

#include "ui/fonts.hpp"

struct ID3D11Buffer;
struct ID3D11Device;
struct ID3D11PixelShader;
struct IDXGISwapChain;

namespace lazyui
{
#if IMGUI_VERSION_NUM >= 19200
    using texture_handle = ImTextureRef;
#else
    using texture_handle = ImTextureID;
#endif

    struct corner_radii
    {
        float top_left = 0.f;
        float top_right = 0.f;
        float bottom_right = 0.f;
        float bottom_left = 0.f;

        constexpr corner_radii( ) = default;
        constexpr explicit corner_radii( float all ) : top_left( all ), top_right( all ), bottom_right( all ), bottom_left( all ) { }
        constexpr corner_radii( float top_left, float top_right, float bottom_right, float bottom_left )
            : top_left( top_left ), top_right( top_right ), bottom_right( bottom_right ), bottom_left( bottom_left ) { }
    };

    bool image_texture( std::size_t index, texture_handle& texture );

    void framework_blur( ImDrawList* draw, ImVec2 min, ImVec2 max, const corner_radii& radii );

    void close_window( );
    void minimize_window( );

    void resize_window( ImVec2 size );

    void open_link( const char* url );

    void set_graphics( ID3D11Device* device, IDXGISwapChain* swap_chain );

    corner_radii fit_corner_radii( ImVec2 min, ImVec2 max, const corner_radii& radii );
    void rounded_path( ImDrawList* draw, ImVec2 min, ImVec2 max, const corner_radii& radii );
    void add_rounded_rect_filled( ImDrawList* draw, ImVec2 min, ImVec2 max, const corner_radii& radii, ImU32 color );
    void add_rounded_rect_stroke( ImDrawList* draw, ImVec2 min, ImVec2 max, const corner_radii& radii, ImU32 color, float thickness );
    void add_border_rects( ImDrawList* draw, ImVec2 min, ImVec2 max, float top, float right, float bottom, float left, int align, ImU32 color );
    void add_ellipse_filled( ImDrawList* draw, ImVec2 center, ImVec2 radius, ImU32 color );
    void add_ellipse_stroke( ImDrawList* draw, ImVec2 center, ImVec2 radius, ImU32 color, float thickness );
    void erase_outside_corners( ImDrawList* draw, ImVec2 min, ImVec2 max, const corner_radii& radii );

    enum blend_mode : int
    {
        blend_normal = 0,
        blend_linear_dodge = 1,
        blend_erase = 2,
        blend_multiply = 16,
        blend_screen = 17,
        blend_overlay = 18,
        blend_darken = 19,
        blend_lighten = 20,
        blend_color_dodge = 21,
        blend_color_burn = 22,
        blend_hard_light = 23,
        blend_soft_light = 24,
        blend_difference = 25,
        blend_exclusion = 26,
        blend_linear_burn = 27,
        blend_hue = 28,
        blend_saturation = 29,
        blend_color = 30,
        blend_luminosity = 31
    };

    void begin_blend_mode( const ImDrawList* list, const ImDrawCmd* command );
    inline void begin_blend( ImDrawList* draw, blend_mode mode )
    {
        draw->AddCallback( begin_blend_mode, reinterpret_cast< void* >( static_cast< std::intptr_t >( mode ) ) );
    }
    inline void reset_render_state( ImDrawList* draw )
    {
        draw->AddCallback( ImDrawCallback_ResetRenderState, nullptr );
    }
    void begin_layer( ImDrawList* draw );
    void end_layer( ImDrawList* draw, blend_mode mode );

    void draw_image( ImDrawList* draw, std::size_t index, ImVec2 min, ImVec2 max, ImVec2 uv_min, ImVec2 uv_max, const corner_radii& radii, ImU32 tint );
    class child_region
    {
    public:
        child_region( ImDrawList*& draw, const char* id, ImVec2 position, ImVec2 size );
        ~child_region( );
        child_region( const child_region& ) = delete;
        child_region& operator=( const child_region& ) = delete;

    private:
        ImDrawList*& draw_;
        ImDrawList* parent_;
    };

    inline void draw_image( ImDrawList* draw, std::size_t index, ImVec2 min, ImVec2 max, ImU32 tint = IM_COL32_WHITE )
    {
        draw_image( draw, index, min, max, ImVec2{ 0.f, 0.f }, ImVec2{ 1.f, 1.f }, corner_radii{ }, tint );
    }
    void draw_image_sliced( ImDrawList* draw, std::size_t index, ImVec2 min, ImVec2 max, float source_width, float cap, ImU32 tint );
    void draw_background_blur( ImDrawList* draw, ImVec2 min, ImVec2 max, const corner_radii& radii, float sigma, float alpha = 1.f );

    struct glass_settings
    {
        float refraction = 0.8f;
        float depth = 20.f;
        float dispersion = 0.5f;
        float frost = 4.f;
        float light_angle = -45.f;
        float light_intensity = 0.8f;
        float splay = 0.f;
    };

    void draw_glass( ImDrawList* draw, ImVec2 min, ImVec2 max, const corner_radii& radii, const glass_settings& glass, float alpha = 1.f );

    void draw_window_backdrop( ImDrawList* draw, ImVec2 min, ImVec2 max, const corner_radii& radii, float alpha = 1.f );

    struct shader_parameters
    {
        float resolution[2]{ };
        float time = 0.f;
        float speed = 1.f;
        float color1[3]{ 1.f, 1.f, 1.f };
        float scale = 1.f;
        float color2[3]{ 1.f, 1.f, 1.f };
        float intensity = 1.f;
        float color3[3]{ 1.f, 1.f, 1.f };
        float amount = 1.f;
        float mouse[2]{ -1.f, -1.f };
        float detail = 1.f;
        float alpha = 1.f;
    };

    struct shader_background
    {
        const char* source = nullptr;
        shader_parameters parameters{ };
        bool interactive = false;
        ID3D11PixelShader* shader = nullptr;
        ID3D11Buffer* constants = nullptr;
        ID3D11Device* device = nullptr;
        bool failed = false;
        float clock = 0.f;
        int frame = -1;
    };

    void draw_shader( ImDrawList* draw, shader_background& background, ImVec2 min, ImVec2 max, const corner_radii& radii, float alpha = 1.f );

    float animate( float current, float target, float speed = 16.f );
    ImU32 mix_color( ImU32 from, ImU32 to, float t );
    ImU32 fade( ImU32 color, float alpha );

    enum motion_channel : int { motion_controls, motion_hover, motion_press, motion_lists, motion_navigation, motion_pages, motion_toasts, motion_channel_count };
    enum motion_curve : int { curve_smooth, curve_snappy, curve_linear, curve_ease, curve_spring, curve_custom };
    enum page_transition : int { transition_fade, transition_rise, transition_slide, transition_scale };
    enum intro_style : int { intro_none, intro_fade, intro_rise, intro_scale };

    struct motion_track
    {
        float duration = 0.f;
        int curve = curve_smooth;
        float bezier[4]{ 0.25f, 0.1f, 0.25f, 1.f };
    };

    struct motion_settings
    {
        motion_track channels[motion_channel_count]{ };
        int page_transition = transition_rise;
        float page_distance = 10.f;
        float toast_distance = 12.f;
        int intro = intro_none;
        float intro_duration = 0.42f;
    };

    extern motion_settings motion;

    float animate_to( float& value, float target, int channel );
    float motion_curve_at( int curve, float t );
    float cubic_bezier_at( const float ( &bezier )[4], float t );

    inline float page_fade( float amount, bool incoming ) { return incoming ? amount : amount * amount; }
    inline float page_rise( float amount, bool incoming ) { return incoming && motion.page_transition == transition_rise ? ( 1.f - amount ) * motion.page_distance : 0.f; }
    inline float page_slide( float amount, bool incoming ) { return incoming && motion.page_transition == transition_slide ? ( 1.f - amount ) * motion.page_distance : 0.f; }
    inline float page_scale( float amount, bool incoming ) { return incoming && motion.page_transition == transition_scale ? 1.f - ( 1.f - amount ) * motion.page_distance / 250.f : 1.f; }
    void rotate_vertices( ImDrawList* draw, int start, ImVec2 center, float angle );
    void fade_vertices( ImDrawList* draw, int start, float alpha );
    void scale_vertices( ImDrawList* draw, int start, ImVec2 center, float scale );
    bool play_intro( ImDrawList* draw, int start, ImVec2 center );

    struct pointer_frame
    {
        ImVec2 position{ };
        bool hovered = false;
        bool clicked = false;
        bool down = false;
        bool released = false;
        bool popup_open = false;
        ImVec2 popup_min{ };
        ImVec2 popup_max{ };
        bool claimed = false;
        bool modal = false;
        ImVec2 modal_min{ };
        ImVec2 modal_max{ };
        bool owned = false;
        ImVec2 owner_min{ };
        ImVec2 owner_max{ };
        bool topmost = false;
        ImVec2 topmost_min{ };
        ImVec2 topmost_max{ };
        bool clipped = false;
        ImVec2 clip_min{ };
        ImVec2 clip_max{ };
    };

    extern pointer_frame pointer;

    void begin_pointer_frame( );
    bool pointer_inside( ImVec2 min, ImVec2 max );
    bool pointer_over( ImVec2 min, ImVec2 max );
    bool pointer_over_any( ImVec2 min, ImVec2 max );

    struct window_drag_state
    {
        ImVec2 offset{ };
        bool dragging = false;
        long anchor_x = 0;
        long anchor_y = 0;
        long start_x = 0;
        long start_y = 0;
    };

    extern window_drag_state window_drag;

    void update_window_drag( bool blocked, bool move_host, bool host_fixed = false );

    struct hud_state
    {
        ImVec2 offset{ };
        bool dragging = false;
    };

    bool begin_hud( const char* id, hud_state& state, ImVec2 anchor, ImVec2 offset, ImVec2 size );
    void end_hud( hud_state& state );

    struct popup_state
    {
        bool open = false;
        bool trigger_hit = false;
        float anim = 0.f;
    };

    struct switch_state
    {
        bool value = false;
        float anim = 0.f;
        float hover = 0.f;
    };

    struct slider_state
    {
        float value = 0.f;
        float shown = 0.f;
        float hover = 0.f;
        bool active = false;
        bool touched = false;
        float bound = 0.f;
    };

    struct range_slider_state
    {
        float low = 0.f;
        float high = 1.f;
        float shown_low = 0.f;
        float shown_high = 1.f;
        float hover = 0.f;
        int active = 0;
        bool touched = false;
        float bound_low = 0.f;
        float bound_high = 0.f;
    };

    struct combo_state
    {
        int selected = 0;
        bool open = false;
        float anim = 0.f;
        float hover = 0.f;
        int hot = -1;
    };

    struct search_state
    {
        std::array< char, 128 > buffer{ };
        float placeholder = 1.f;
        float focus = 0.f;
    };

    struct field_state
    {
        std::array< char, 256 > buffer{ };
        bool ready = false;
        bool touched = false;
        float focus = 0.f;
        float hover = 0.f;
        float placeholder = 1.f;
    };

    struct keybind_state
    {
        int key = 0;
        bool waiting = false;
        float anim = 0.f;
        float hover = 0.f;
    };

    struct button_state
    {
        bool pressed = false;
        bool clicked = false;
        float hover = 0.f;
        float press = 0.f;
    };

    struct color_state
    {
        ImVec4 value{ 1.f, 1.f, 1.f, 1.f };
        bool open = false;
        float anim = 0.f;
        float hover = 0.f;
        int dragging = 0;
        float hue = 0.f;
        bool over_popup = false;
    };

    template < int count >
    struct selection_state
    {
        int selected = 0;
        float slide = 0.f;
        float size = 0.f;
        std::array< float, count > amount{ };
        std::array< float, count > hover{ };
        bool ready = false;
        std::array< float, count > shift{ };
    };

    template < int count >
    ImU32 selection_tint( const selection_state< count >& selection, int index, ImU32 idle, ImU32 selected, ImU32 hovered )
    {
        return mix_color( mix_color( idle, selected, selection.amount[index] ), hovered, selection.hover[index] * ( 1.f - selection.amount[index] ) );
    }

    template < int count >
    struct screen_state
    {
        int current = 0;
        std::array< int, 16 > history{ };
        int depth = 0;
        double entered = 0.0;
        std::array< float, count > amount{ };
        bool ready = false;
    };

    template < int count >
    void go_to_screen( screen_state< count >& state, int index, bool remember = true )
    {
        if ( index < 0 || index >= count || index == state.current )
            return;
        if ( remember )
        {
            if ( state.depth == static_cast< int >( state.history.size( ) ) )
            {
                for ( int slot = 1; slot < state.depth; ++slot )
                    state.history[slot - 1] = state.history[slot];
                --state.depth;
            }
            state.history[state.depth++] = state.current;
        }
        state.current = index;
        state.entered = ImGui::GetTime( );
        ImGui::ClearActiveID( );
    }

    template < int count >
    void go_back( screen_state< count >& state )
    {
        if ( state.depth > 0 )
            go_to_screen( state, state.history[--state.depth], false );
    }

    template < int count >
    void update_screens( screen_state< count >& state, const int ( &next )[count], float pause = 1.6f )
    {
        if ( !state.ready )
        {
            state.amount[state.current] = 1.f;
            state.entered = ImGui::GetTime( );
            state.ready = true;
        }
        if ( next[state.current] >= 0 && ImGui::GetTime( ) - state.entered >= pause )
            go_to_screen( state, next[state.current], false );
        for ( int index = 0; index < count; ++index )
            animate_to( state.amount[index], index == state.current ? 1.f : 0.f, motion_pages );
    }

    template < int count >
    float screen_alpha( const screen_state< count >& state, int index )
    {
        for ( int above = index + 1; above < count; ++above )
            if ( state.amount[above] > 0.001f )
                return 1.f;
        return state.amount[index];
    }

    template < int count >
    ImVec2 screen_window_size( const screen_state< count >& state, const ImVec2 ( &sizes )[count] )
    {
        ImVec2 size = sizes[state.current];
        for ( int index = 0; index < count; ++index )
            if ( state.amount[index] > 0.001f )
                size = ImMax( size, sizes[index] );
        return size;
    }

    void popup_trigger( popup_state& state, ImVec2 min, ImVec2 max );
    void update_popup( popup_state& state, ImVec2 min, ImVec2 max );
    void popup_close( popup_state& state, ImVec2 min, ImVec2 max );

    struct toast_state
    {
        int kind = 0;
        bool open = false;
        float anim = 0.f;
        float y = 0.f;
        float stack = 0.f;
        float left = 1.f;
        float elapsed = 0.f;
        float duration = 4.f;
        std::array< char, 96 > title{ };
        std::array< char, 192 > message{ };
    };

    template < int capacity >
    struct notification_state
    {
        std::array< toast_state, capacity > items{ };
        int count = 0;
    };

    void copy_text( char* target, std::size_t size, const char* text );
    bool same_name( const char* left, const char* right );

    template < int capacity >
    void push_notification( notification_state< capacity >& state, int kind, const char* title = nullptr, const char* message = nullptr, float seconds = 4.f )
    {
        if ( state.count == capacity )
        {
            for ( int index = 1; index < capacity; ++index )
                state.items[index - 1] = state.items[index];
            --state.count;
        }
        toast_state& toast = state.items[state.count++];
        toast = toast_state{ };
        toast.kind = kind;
        toast.open = true;
        toast.duration = seconds;
        copy_text( toast.title.data( ), toast.title.size( ), title );
        copy_text( toast.message.data( ), toast.message.size( ), message );
    }

    template < int capacity, int kinds >
    void update_notifications( notification_state< capacity >& state, const ImVec4 ( &rects )[kinds], const ImVec4 ( &closes )[kinds], ImVec2 origin, float direction, float gap = 10.f )
    {
        const float delta = ImGui::GetIO( ).DeltaTime;
        bool over = false;
        float room = 0.f;
        for ( int index = state.count - 1; index >= 0; --index )
        {
            toast_state& toast = state.items[index];
            const ImVec4& rect = rects[toast.kind];
            if ( toast.anim <= 0.f )
                toast.stack = room;
            else
                animate_to( toast.stack, room, motion_toasts );
            toast.y = toast.stack * direction - ( 1.f - toast.anim ) * motion.toast_distance * direction;
            const ImVec2 shift{ 0.f, toast.y };
            const ImVec2 min = origin + ImVec2{ rect.x, rect.y } + shift;
            const bool hovered = pointer.hovered && toast.open && pointer_inside( min, min + ImVec2{ rect.z, rect.w } );
            over |= hovered;
            const ImVec4& close = closes[toast.kind];
            if ( hovered && close.z > 0.f && pointer_inside( origin + ImVec2{ close.x, close.y } + shift, origin + ImVec2{ close.x + close.z, close.y + close.w } + shift ) )
            {
                ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
                if ( pointer.clicked )
                    toast.open = false;
            }
            if ( toast.open && toast.duration > 0.f && !hovered )
            {
                toast.elapsed += delta;
                if ( toast.elapsed >= toast.duration )
                    toast.open = false;
            }
            toast.left = toast.duration > 0.f ? ImSaturate( 1.f - toast.elapsed / toast.duration ) : 1.f;
            animate_to( toast.anim, toast.open ? 1.f : 0.f, motion_toasts );
            room += ( rect.w + gap ) * toast.anim;
        }
        int kept = 0;
        for ( int index = 0; index < state.count; ++index )
            if ( state.items[index].open || state.items[index].anim > 0.f )
                state.items[kept++] = state.items[index];
        state.count = kept;
        if ( over )
        {
            pointer.claimed = true;
            pointer.hovered = false;
        }
    }

    template < int capacity >
    void notification_trigger( notification_state< capacity >& state, int kind, ImVec2 min, ImVec2 max )
    {
        if ( !pointer_over( min, max ) )
            return;
        ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
        if ( pointer.clicked )
            push_notification( state, kind );
    }

    void update_switch( switch_state& state, ImVec2 min, ImVec2 max );
    void update_slider( slider_state& state, ImVec2 track_min, ImVec2 track_max, ImVec2 hit_min, ImVec2 hit_max );
    void update_range_slider( range_slider_state& state, ImVec2 track_min, ImVec2 track_max, ImVec2 hit_min, ImVec2 hit_max );
    void update_combo( combo_state& state, ImVec2 min, ImVec2 max, int options );

    bool update_switch( switch_state& state, bool& value, ImVec2 min, ImVec2 max );
    bool update_slider( slider_state& state, float& value, float min, float max, ImVec2 track_min, ImVec2 track_max, ImVec2 hit_min, ImVec2 hit_max );
    bool update_range_slider( range_slider_state& state, float& low, float& high, float min, float max, ImVec2 track_min, ImVec2 track_max, ImVec2 hit_min, ImVec2 hit_max );
    bool update_combo( combo_state& state, int& value, ImVec2 min, ImVec2 max, int options );
    bool update_color( color_state& state, ImVec4& value, ImVec2 min, ImVec2 max );
    bool draw_color_popup( ImDrawList* draw, color_state& state, ImVec4& value, ImVec2 swatch_min, ImVec2 swatch_max, ImU32 fill, ImU32 stroke );
    bool update_keybind( keybind_state& state, int& key, ImVec2 min, ImVec2 max );
    bool edit_field( field_state& state, char* buffer, std::size_t size, int slot, ImVec2 baseline, float width, ImVec2 field_min, ImVec2 field_max, ImU32 text, bool password );
    bool edit_search( search_state& state, char* buffer, std::size_t size, int slot, ImVec2 baseline, float width, ImVec2 field_min, ImVec2 field_max, ImU32 text, ImU32 placeholder );

    switch_state& switch_for( const bool& value );
    slider_state& slider_for( const float& value, float min, float max );
    range_slider_state& range_slider_for( const float& low, const float& high, float min, float max );
    combo_state& combo_for( const int& value );
    combo_state& multi_combo_for( const bool* values );
    color_state& color_for( const ImVec4& value );
    keybind_state& keybind_for( const int& key );
    field_state& field_for( const char* buffer );
    search_state& search_for( const char* buffer );
    button_state& button_for( const char* id );

    bool begin_child( const char* id, ImVec2 position, ImVec2 size, ImVec2 padding = ImVec2{ }, float spacing = 0.f );
    void end_child( );

    using child_look = void ( * )( ImDrawList* draw, ImVec2 at, ImVec2 size, const char* title, const char* icon, float open );

    struct child_style
    {
        child_look look = nullptr;
        ImVec2 padding{ };
        float spacing = 0.f;
        float header = 0.f;
    };

    bool begin_child( const char* id, ImVec2 position, ImVec2 size, const child_style& style, bool open = true );
    bool begin_child( const char* id, ImVec2 position, ImVec2 size, const child_style& style, ImVec2 padding, float spacing, bool open = true );
    bool begin_child( const char* id, const char* icon, ImVec2 position, ImVec2 size, const child_style& style, bool open = true );
    bool begin_child( const char* id, const char* icon, ImVec2 position, ImVec2 size, const child_style& style, ImVec2 padding, float spacing, bool open = true );
    void begin_group( const char* id, ImVec2 position, ImVec2 size, ImVec2 padding = ImVec2{ }, float spacing = 0.f );
    void begin_group( const char* id, ImVec2 position, ImVec2 size, const child_style& style );
    void begin_group( const char* id, ImVec2 position, ImVec2 size, const child_style& style, ImVec2 padding, float spacing );
    void begin_group( const char* id, const char* icon, ImVec2 position, ImVec2 size, const child_style& style );
    void begin_group( const char* id, const char* icon, ImVec2 position, ImVec2 size, const child_style& style, ImVec2 padding, float spacing );
    void end_group( );
    ImVec2 section_pos( );
    ImVec2 section_size( );
    float row_width( float inset );
    void set_cursor( ImVec2 position );
    void item( ImVec2 size );

    bool begin_page( float amount, bool selected );
    void end_page( );
    void update_keybind( keybind_state& state, ImVec2 min, ImVec2 max );
    bool update_button( button_state& state, ImVec2 min, ImVec2 max );
    void update_color( color_state& state, ImVec2 min, ImVec2 max );
    inline ImU32 color_value( const color_state& state ) { return ImGui::ColorConvertFloat4ToU32( state.value ); }
    void draw_color_popup( ImDrawList* draw, color_state& state, ImVec2 swatch_min, ImVec2 swatch_max, ImU32 fill, ImU32 stroke );

    void edit_search( search_state& state, int slot, ImVec2 baseline, float width, ImVec2 field_min, ImVec2 field_max, ImU32 text, ImU32 placeholder );
    void edit_field( field_state& state, const char* initial, int slot, ImVec2 baseline, float width, ImVec2 field_min, ImVec2 field_max, ImU32 text, bool password );

    const char* format_value( const char* format, float value );
    const char* format_range( const char* format, float low, float high );
    float text_width( int slot, const char* text );
    const char* short_key_name( int key );

    template < int count >
    void update_selection( selection_state< count >& state, const ImVec4 ( &slots )[count], ImVec2 origin, bool vertical, float anchor, float size )
    {
        if ( !state.ready )
        {
            for ( int index = 0; index < count; ++index )
                state.amount[index] = index == state.selected ? 1.f : 0.f;
            state.size = size;
            state.ready = true;
        }
        int hot = -1;
        float nearest = FLT_MAX;
        for ( int index = 0; index < count; ++index )
        {
            const ImVec2 min = origin + ImVec2{ slots[index].x, slots[index].y };
            const ImVec2 max = min + ImVec2{ slots[index].z, slots[index].w };
            const ImVec2 delta = pointer.position - ( min + max ) * 0.5f;
            const float distance = delta.x * delta.x + delta.y * delta.y;
            if ( pointer_over_any( min, max ) && distance < nearest )
            {
                hot = index;
                nearest = distance;
            }
        }
        for ( int index = 0; index < count; ++index )
        {
            const bool hovered = index == hot;
            if ( hovered && index != state.selected )
                ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
            if ( hovered && pointer.clicked )
                state.selected = index;
            animate_to( state.amount[index], index == state.selected ? 1.f : 0.f, motion_navigation );
            animate_to( state.hover[index], hovered && index != state.selected ? 1.f : 0.f, motion_hover );
        }
        if ( state.selected < 0 || state.selected >= count )
            return;
        const ImVec4& target = slots[state.selected];
        animate_to( state.slide, ( vertical ? target.y : target.x ) - anchor, motion_navigation );
        animate_to( state.size, vertical ? size : target.z, motion_navigation );
    }

    template < int count >
    bool update_selection( selection_state< count >& state, int& selected, const ImVec4 ( &slots )[count], ImVec2 origin, bool vertical, float anchor, float size )
    {
        state.selected = selected;
        update_selection( state, slots, origin, vertical, anchor, size );
        const bool changed = selected != state.selected;
        selected = state.selected;
        return changed;
    }

    template < int count >
    bool begin_page( const selection_state< count >& navigation, int index )
    {
        return begin_page( navigation.amount[index], navigation.selected == index );
    }

    template < int count >
    void flow_selection( selection_state< count >& state, ImVec4 ( &slots )[count], const ImVec4 ( &items )[count], float pad_start, float pad_end, float anchor )
    {
        float push = 0.f;
        float x = 0.f;
        float width = 0.f;
        for ( int index = 0; index < count; ++index )
        {
            const float amount = state.amount[index];
            const float left = items[index].x + push + pad_start * amount;
            state.shift[index] = left - items[index].z;
            slots[index].x = left - pad_start;
            x += amount * ( left - pad_start );
            width += amount * ( items[index].y + pad_start + pad_end );
            push += ( pad_start + pad_end ) * amount;
        }
        state.slide = x - anchor;
        state.size = width;
    }

    void draw_combo_popup( ImDrawList* draw, combo_state& state, ImVec2 box_min, ImVec2 box_max, const char* const* options, int count, int slot, float text_x, float baseline, float radius, ImU32 fill, ImU32 stroke, ImU32 text, ImU32 text_active );
    bool draw_combo_popup( ImDrawList* draw, combo_state& state, int& value, ImVec2 box_min, ImVec2 box_max, const char* const* options, int count, int slot, float text_x, float baseline, float radius, ImU32 fill, ImU32 stroke, ImU32 text, ImU32 text_active );

    template < int count >
    void draw_combo_popup( ImDrawList* draw, combo_state& state, ImVec2 box_min, ImVec2 box_max, const char* const ( &options )[count], int slot, float text_x, float baseline, float radius, ImU32 fill, ImU32 stroke, ImU32 text, ImU32 text_active )
    {
        draw_combo_popup( draw, state, box_min, box_max, options, count, slot, text_x, baseline, radius, fill, stroke, text, text_active );
    }

    template < int count >
    bool draw_combo_popup( ImDrawList* draw, combo_state& state, int& value, ImVec2 box_min, ImVec2 box_max, const char* const ( &options )[count], int slot, float text_x, float baseline, float radius, ImU32 fill, ImU32 stroke, ImU32 text, ImU32 text_active )
    {
        return draw_combo_popup( draw, state, value, box_min, box_max, options, count, slot, text_x, baseline, radius, fill, stroke, text, text_active );
    }

    bool draw_multi_combo_popup( ImDrawList* draw, combo_state& state, bool* values, ImVec2 box_min, ImVec2 box_max, const char* const* options, int count, int slot, float text_x, float baseline, float radius, ImU32 fill, ImU32 stroke, ImU32 text, ImU32 text_active );
    const char* multi_combo_text( const bool* values, const char* const* options, int count, int slot, float width, const char* summary = nullptr );

    template < int count >
    bool draw_multi_combo_popup( ImDrawList* draw, combo_state& state, bool ( &values )[count], ImVec2 box_min, ImVec2 box_max, const char* const ( &options )[count], int slot, float text_x, float baseline, float radius, ImU32 fill, ImU32 stroke, ImU32 text, ImU32 text_active )
    {
        return draw_multi_combo_popup( draw, state, values, box_min, box_max, options, count, slot, text_x, baseline, radius, fill, stroke, text, text_active );
    }

    template < int count >
    const char* multi_combo_text( const bool ( &values )[count], const char* const ( &options )[count], int slot, float width, const char* summary = nullptr )
    {
        return multi_combo_text( values, options, count, slot, width, summary );
    }
}
