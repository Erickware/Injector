#include "lazyui/lazyui.hpp"

#include "ui.hpp"
#include "pages.hpp"
#include "assets.hpp"
#include "theme.hpp"
#include "lazyui/runtime.hpp"

#include <array>
#include <cstddef>
#include <iterator>

namespace ui
{
    namespace
    {
        using namespace lazyui;

        selection_state< 3 > nav_main{ 0, 0.f, 164.f };
        const ImVec4 nav_main_slots[3] = {
            ImVec4{ 10.f, 88.f, 164.f, 38.f },
            ImVec4{ 10.f, 130.f, 164.f, 38.f },
            ImVec4{ 10.f, 172.f, 164.f, 38.f }
        };

        constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;

        struct embedded_image
        {
            const unsigned char* data;
            std::size_t size;
        };

        const embedded_image image_sources[] = {
            { assets::glow_gradient_bytes, assets::glow_gradient_bytes_size },
            { assets::fill_gradient_bytes, assets::fill_gradient_bytes_size },
            { assets::fill_gradient_2_bytes, assets::fill_gradient_2_bytes_size },
            { assets::fill_gradient_3_bytes, assets::fill_gradient_3_bytes_size }
        };

        std::array< c_image, std::size( image_sources ) > images;

        c_blureffect framework_blur_effect;

        void load_images( )
        {
            for ( std::size_t index = 0; index < images.size( ); ++index )
                if ( !images[index].ready( ) )
                    ( void )images[index].load( lazyui::app.device, image_sources[index].data, image_sources[index].size );
        }

        bool ensure_framework_blur( )
        {
            if ( framework_blur_effect.ready( ) && framework_blur_effect.matches_display( ImGui::GetIO( ).DisplaySize ) )
                return true;
            return framework_blur_effect.setup( lazyui::app.device, lazyui::app.device_context, lazyui::app.swap_chain );
        }

        void update_controls( ImVec2 origin )
        {
            update_selection( nav_main, vars.page, nav_main_slots, origin, true, 88.f, 164.f );
        }

        void draw_nav_main_item( ImDrawList* draw, ImVec2 at, int index, const char* label, const char* icon )
        {
            draw_icon_glyph( draw, at + ImVec2{ 14.f, 11.f }, selection_tint( nav_main, index, theme::icon_muted, theme::icon_blue( ), theme::icon_blue_2( ) ), icon );
            draw_text( draw, fonts::manrope_extralight_400_13, at + ImVec2{ 42.f, 10.f }, selection_tint( nav_main, index, theme::icon_muted, theme::text, theme::text_faint ), label );
        }

        void draw_sidebar( ImDrawList* draw, ImVec2 origin )
        {
            add_rounded_rect_filled( draw, origin, origin + ImVec2{ 184.f, 812.f }, corner_radii{ 16.f, 0.f, 0.f, 16.f }, theme::surface_6 );
            draw_image( draw, 0, origin + ImVec2{ -64.f, -85.f }, origin + ImVec2{ 184.f, 125.f } );
            draw_icon_glyph( draw, origin + ImVec2{ 24.f, 23.f }, theme::icon_blue( ), icons::syringe_glyph );
            draw_text( draw, fonts::manrope_extralight_600_15, origin + ImVec2{ 66.f, 19.f }, theme::text, "DLL Injector" );
            draw_text( draw, fonts::manrope_extralight_500_11, origin + ImVec2{ 66.f, 40.f }, theme::text_5, "Simple. Fast. Reli…" );
            add_rounded_rect_filled( draw, origin + ImVec2{ 10.f, 88.f + nav_main.slide }, origin + ImVec2{ 174.f, 126.f + nav_main.slide }, corner_radii{ 12.f }, theme::surface_2 );
            draw_icon_glyph( draw, origin + ImVec2{ 24.f, 99.f }, mix_color( theme::icon_muted, theme::icon_blue( ), nav_main.amount[0] ), icons::syringe_2_glyph );
            draw_text( draw, fonts::manrope_extralight_400_13, origin + ImVec2{ 52.f, 98.f }, mix_color( theme::icon_muted, theme::text, nav_main.amount[0] ), "Injector" );
            draw_nav_main_item( draw, origin + ImVec2{ 10.f, 130.f }, 1, "Settings", icons::settings_glyph );
            draw_nav_main_item( draw, origin + ImVec2{ 10.f, 172.f }, 2, "About", icons::info_glyph );
            add_rounded_rect_filled( draw, origin + ImVec2{ 183.f, 0.f }, origin + ImVec2{ 184.f, 812.f }, corner_radii{ }, theme::accent( ) );
        }

        void draw_frame( ImDrawList* draw, ImVec2 origin )
        {
            add_rounded_rect_filled( draw, origin, origin + ImVec2{ 900.f, 812.f }, corner_radii{ 16.f }, theme::background );
            draw->PushClipRect( origin, origin + ImVec2{ 900.f, 812.f }, true );
                draw_sidebar( draw, origin );
                for ( int page = 0; page < 3; ++page )
                    if ( begin_page( nav_main, page ) )
                    {
                        pages::draw( page );
                        end_page( );
                    }
                erase_outside_corners( draw, origin, origin + ImVec2{ 900.f, 812.f }, corner_radii{ 16.f } );
            draw->PopClipRect( );
            add_rounded_rect_stroke( draw, origin + ImVec2{ 0.5f, 0.5f }, origin + ImVec2{ 899.5f, 811.5f }, corner_radii{ 15.5f }, theme::accent( ), 1.f );
        }
    }

    ImVec2 frame_size( )
    {
        return ImVec2{ theme::frame_width, theme::frame_height };
    }

    void render( )
    {
        load_images( );
        pages::tick( );
        set_graphics( lazyui::app.device, lazyui::app.swap_chain );

        // Settings > Appearance applies at frame scope so every page and widget
        // drawn below already sees the live values:
        //   Accent Color    -> theme palette swap (rethemes the whole UI)
        //   Window Opacity  -> real window translucency via WS_EX_LAYERED
        //   Animations      -> animation durations on/off, Reduce Motion scales them
        theme::accent_choice = vars.accent_color;

        {
            static bool layered = false;
            static int applied_alpha = -1;
            if ( !layered )
            {
                const LONG_PTR ex = GetWindowLongPtrW( lazyui::app.window, GWL_EXSTYLE );
                SetWindowLongPtrW( lazyui::app.window, GWL_EXSTYLE, ex | WS_EX_LAYERED );
                layered = true;
            }
            const int wanted = ImClamp( static_cast< int >( vars.window_opacity * 255.f / 100.f + 0.5f ), 30, 255 );
            if ( wanted != applied_alpha )
            {
                SetLayeredWindowAttributes( lazyui::app.window, 0, static_cast< BYTE >( wanted ), LWA_ALPHA );
                applied_alpha = wanted;
            }
        }

        {
            static bool seeded = false;
            static float base_durations[ motion_channel_count ];
            if ( !seeded )
            {
                for ( int channel = 0; channel < motion_channel_count; ++channel )
                    base_durations[channel] = motion.channels[channel].duration;
                // The design ships with instant channels; give each one a duration so
                // the two toggles have a real effect. Reduce Motion scales these.
                base_durations[motion_controls]   = ImMax( base_durations[motion_controls], 0.24f );
                base_durations[motion_hover]      = ImMax( base_durations[motion_hover], 0.20f );
                base_durations[motion_press]      = ImMax( base_durations[motion_press], 0.12f );
                base_durations[motion_lists]      = ImMax( base_durations[motion_lists], 0.30f );
                base_durations[motion_navigation] = ImMax( base_durations[motion_navigation], 0.28f );
                base_durations[motion_pages]      = ImMax( base_durations[motion_pages], 0.33f );
                base_durations[motion_toasts]     = ImMax( base_durations[motion_toasts], 0.28f );
                seeded = true;
            }
            const float scale = !prefs.enable_animations ? 0.f : ( prefs.reduce_motion ? 0.45f : 1.f );
            for ( int channel = 0; channel < motion_channel_count; ++channel )
                motion.channels[channel].duration = base_durations[channel] * scale;
        }

        ImGui::SetNextWindowPos( ImVec2{ 0.f, 0.f } );
        ImGui::SetNextWindowSize( frame_size( ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2{ 0.f, 0.f } );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.f );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.f );
        ImGui::Begin( "DLL Injector", nullptr, window_flags );
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        const ImVec2 origin = ImGui::GetWindowPos( );
        begin_pointer_frame( );
        update_controls( origin );
        draw_frame( draw, origin );
        update_window_drag( pointer.claimed || pointer.popup_open, true );
        ImGui::End( );
        ImGui::PopStyleVar( 3 );
    }
}

bool lazyui::image_texture( std::size_t index, texture_handle& texture )
{
    if ( index >= ui::images.size( ) || !ui::images[index].ready( ) )
        return false;
    texture = ui::images[index];
    return true;
}

void lazyui::framework_blur( ImDrawList* draw, ImVec2 min, ImVec2 max, const corner_radii& radii )
{
    if ( ui::ensure_framework_blur( ) )
        ui::framework_blur_effect.draw( draw, min, max, radii.top_left, radii.top_right, radii.bottom_right, radii.bottom_left );
}

void lazyui::close_window( )
{
    ::PostMessageW( lazyui::app.window, WM_CLOSE, 0, 0 );
}

void lazyui::minimize_window( )
{
    ::ShowWindow( lazyui::app.window, SW_MINIMIZE );
}

void lazyui::resize_window( ImVec2 )
{
}
