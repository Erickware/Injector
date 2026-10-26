#include "ui.hpp"
#include "pages.hpp"
#include "widgets.hpp"
#include "theme.hpp"

#include "lazyui/lazyui.hpp"   // lazyui::app.window, owner of the Browse dialog

#include "process.hpp"
#include "inject.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <unordered_map>
#include <vector>

#ifndef NOMINMAX
#   define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>

#pragma comment( lib, "comdlg32.lib" )   // GetOpenFileNameW for the Browse dialog

namespace ui::pages
{
    using namespace lazyui;
    using namespace widgets;

    namespace
    {
        const char* const method_items[] = { "LoadLibrary", "Manual Map", "Thread Hijack" };
        const char* const accent_color_items[] = { "Blue", "Cyan", "Violet" };

        // ---- process list state -------------------------------------------------

        std::vector< process::entry > shown;      // what the panel drew this frame
        double last_refresh = -1.0;
        bool refresh_requested = true;

        // Modal Win32 picker behind the Browse button. The app window owns the dialog
        // so it stays on top of the injector; OFN_NOCHANGEDIR keeps our working
        // directory from following the user's shell folder. Cancelling leaves the
        // current path untouched, and a path that cannot fit is never half-copied.
        void browse_for_dll( char* value, std::size_t size )
        {
            wchar_t file[ MAX_PATH * 2 ] = { };
            OPENFILENAMEW ofn { };
            ofn.lStructSize = sizeof( ofn );
            ofn.hwndOwner = lazyui::app.window;
            ofn.lpstrFile = file;
            ofn.nMaxFile = IM_ARRAYSIZE( file );
            ofn.lpstrFilter = L"DLL files (*.dll)\0*.dll\0All files (*.*)\0*.*\0\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrTitle = L"Select a DLL to inject";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;

            if ( !GetOpenFileNameW( &ofn ) )
                return;

            const int required = WideCharToMultiByte( CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr );
            if ( required <= 0 || static_cast< std::size_t >( required ) > size )
                return;
            WideCharToMultiByte( CP_UTF8, 0, file, -1, value, static_cast< int >( size ), nullptr, nullptr );
        }

        // ---- injection notifications -------------------------------------------
        //
        // The injection worker reports through inject::last_completion(); every
        // frame this drain turns each finished run into exactly one toast, gated
        // by Settings > Notifications (sounds fire inside the worker itself).

        notification_state< 8 > notifications{ };
        unsigned int seen_completion = 0;

        const ImVec4 toast_rects[2] = {
            ImVec4{ 616.f, 20.f, 264.f, 64.f },    // 0 = success
            ImVec4{ 616.f, 20.f, 264.f, 64.f }     // 1 = error
        };
        const ImVec4 toast_closes[2] = {
            ImVec4{ 852.f, 28.f, 16.f, 16.f },
            ImVec4{ 852.f, 28.f, 16.f, 16.f }
        };

        void drain_injection_events( )
        {
            const inject::completion done = inject::last_completion( );
            if ( done.epoch == seen_completion )
                return;
            seen_completion = done.epoch;
            if ( done.epoch == 0 )
                return;

            char message[192];
            if ( done.success )
            {
                if ( !bools[7] )
                    return;
                ImFormatString( message, IM_ARRAYSIZE( message ), "'%s' is now loaded in %s.",
                                inject::last_dll( ), inject::last_target( ) );
                push_notification( notifications, 0, "Injection Complete", message, 5.f );
            }
            else
            {
                if ( !bools[8] )
                    return;
                ImFormatString( message, IM_ARRAYSIZE( message ),
                                "See the Injection Log for the failing step." );
                push_notification( notifications, 1, "Injection Failed", message, 7.f );
            }
        }

        void draw_notifications( ImVec2 origin )
        {
            update_notifications( notifications, toast_rects, toast_closes, origin, 1.f );
            ImDrawList* fg = ImGui::GetForegroundDrawList( );
            for ( int index = 0; index < notifications.count; ++index )
            {
                const toast_state& toast = notifications.items[ index ];
                if ( !toast.open && toast.anim <= 0.f )
                    continue;
                const ImU32 led = toast.kind == 0 ? IM_COL32( 74, 222, 128, 255 ) : IM_COL32( 248, 113, 113, 255 );
                const ImVec2 min = origin + ImVec2{ toast_rects[ toast.kind ].x, toast_rects[ toast.kind ].y + toast.y };
                const ImVec2 max = min + ImVec2{ toast_rects[ toast.kind ].z, toast_rects[ toast.kind ].w };
                add_rounded_rect_filled( fg, min, max, corner_radii{ 10.f }, theme::surface_2 );
                add_rounded_rect_filled( fg, min, min + ImVec2{ 4.f, max.y - min.y }, corner_radii{ 2.f }, led );
                add_rounded_rect_stroke( fg, min + ImVec2{ 0.5f, 0.5f }, max - ImVec2{ 0.5f, 0.5f }, corner_radii{ 9.5f }, theme::accent_a60( ), 1.f );
                draw_text( fg, fonts::manrope_extralight_600_12, min + ImVec2{ 14.f, 9.f }, theme::text, toast.title.data( ) );
                fg->PushClipRect( min + ImVec2{ 10.f, 26.f }, max - ImVec2{ 28.f, 14.f }, false );
                draw_text( fg, fonts::manrope_extralight_400_11, min + ImVec2{ 14.f, 31.f }, theme::text_5, toast.message.data( ) );
                fg->PopClipRect( );
                if ( toast.left > 0.f )
                {
                    const float w = ( max.x - min.x - 28.f ) * toast.left;
                    add_rounded_rect_filled( fg, min + ImVec2{ 14.f, 56.f }, min + ImVec2{ 14.f + w, 58.f }, corner_radii{ 1.f }, fade( led, 0.55f ) );
                }
                draw_text( fg, fonts::manrope_extralight_600_12, min + ImVec2{ 240.f, 8.f }, theme::icon_muted, "x" );
            }
        }

        // Right-aligned columns (System Status) truncate from the left so the
        // visible tail stays the most informative part of a long name.
        void ellipsize_left( int font, const char* text, float max_w, char* out, std::size_t out_size )
        {
            out[0] = '\0';
            if ( !text || !text[0] )
                return;
            if ( text_width( font, text ) <= max_w )
            {
                ImStrncpy( out, text, out_size );
                return;
            }
            const float dots_w = text_width( font, "..." );
            std::size_t start = 0;
            const std::size_t len = std::strlen( text );
            while ( start < len && text_width( font, text + start ) + dots_w > max_w )
            {
                ++start;
                while ( start < len && ( static_cast< unsigned char >( text[start] ) & 0xC0 ) == 0x80 )
                    ++start;
            }
            ImFormatString( out, out_size, "...%s", start >= len ? text : text + start );
        }

        constexpr float process_row_height = 41.f;
        // The viewport used to stop at y=348 with 68px of dead panel below it; it
        // now spans the list area down to 10px above the panel's bottom edge
        // (177..406), so the rows fill the whole box like a real list.
        constexpr float process_viewport_height = 229.f;
        constexpr double process_refresh_interval = 2.0;

        // Row colouring. The pid carries the process family and the architecture chip
        // carries the bitness, so a long list is scannable instead of a wall of one tint.
        struct chip_look
        {
            ImU32 fill;
            ImU32 edge;
            ImU32 text;
        };

        // lazyui::draw_text takes the TOP of the line box, not a baseline, so every y here
        // is a box top. The font slots are 12px and 11px tall in the design.
        constexpr float chip_font_height = 11.f;
        constexpr float chip_height = 15.f;
        constexpr float chip_pad_x = 7.f;

        ImU32 pid_text_color( std::uint32_t pid )
        {
            if ( pid < 16 )
                return IM_COL32( 232, 121, 249, 255 );   // kernel and system processes
            if ( pid < 1000 )
                return IM_COL32( 45, 212, 191, 255 );    // early boot services
            if ( pid < 10000 )
                return IM_COL32( 56, 189, 248, 255 );    // ordinary user session
            return IM_COL32( 251, 191, 36, 255 );        // recycled, high-range pids
        }

        chip_look architecture_chip( process::architecture value )
        {
            switch ( value )
            {
            case process::architecture::x64:   return { IM_COL32( 23, 68, 168, 255 ),  IM_COL32( 59, 130, 246, 255 ),  IM_COL32( 191, 219, 254, 255 ) };
            case process::architecture::x86:   return { IM_COL32( 116, 78, 16, 255 ),  IM_COL32( 245, 158, 11, 255 ),  IM_COL32( 253, 230, 138, 255 ) };
            case process::architecture::arm64: return { IM_COL32( 24, 96, 80, 255 ),   IM_COL32( 45, 212, 191, 255 ),  IM_COL32( 153, 246, 228, 255 ) };
            default:                           return { IM_COL32( 36, 46, 62, 255 ),   IM_COL32( 74, 87, 108, 255 ),   IM_COL32( 116, 136, 166, 255 ) };
            }
        }

        // PID keys keep each row's visual state stable as rows scroll or refresh.
        std::unordered_map< std::uint32_t, switch_state >& row_states( )
        {
            static std::unordered_map< std::uint32_t, switch_state > states;
            return states;
        }

        // Called from tick() after each refresh: drop state of pids that no longer exist
        // so a reused pid never inherits a stale tick, and the map stays the size of the
        // live list. An empty snapshot leaves the state untouched (nothing is dead yet).
        void prune_row_states( )
        {
            const auto& procs = process::list( );
            if ( procs.empty( ) )
                return;
            auto& flags = row_states( );
            for ( auto it = flags.begin( ); it != flags.end( ); )
            {
                bool alive = false;
                for ( const auto& p : procs )
                    if ( p.pid == it->first )
                    {
                        alive = true;
                        break;
                    }
                it = alive ? std::next( it ) : flags.erase( it );
            }
        }

        // One row of the panel, laid out from a single top coordinate. Everything is
        // measured, so a short image name and a long one keep the pid and the chip aligned
        // and the chip text stays centred inside its own box.
        void draw_row( ImDrawList* draw, ImVec2 at, const process::entry& item, bool selected, bool hovered )
        {
            switch_state& state = row_states( )[item.pid];
            animate_to( state.anim, selected ? 1.f : 0.f, motion_controls );
            animate_to( state.hover, hovered ? 1.f : 0.f, motion_hover );
            const ImVec2 box_at = at + ImVec2{ 0.f, 1.f };
            add_rounded_rect_filled( draw, box_at + ImVec2{ 0.f, 1.f }, box_at + ImVec2{ 14.f, 15.f }, corner_radii{ 4.f }, mix_color( mix_color( theme::fill_blue_2( ), theme::icon_blue( ), state.anim ), theme::fill_blue_3( ), state.hover * ( 1.f - state.anim ) ) );
            add_rounded_rect_stroke( draw, box_at + ImVec2{ 0.5f, 1.5f }, box_at + ImVec2{ 13.5f, 14.5f }, corner_radii{ 3.5f }, mix_color( theme::accent( ), theme::icon_blue( ), state.anim ), 1.f );
            draw_icon_glyph( draw, box_at + ImVec2{ 1.5f, 2.5f }, fade( theme::icon, state.anim ), icons::check_glyph );

            const float name_y = 1.f;
            const float name_left = 24.f;

            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ name_left, name_y }, IM_COL32( 154, 167, 189, 255 ), item.name.c_str( ) );

            char pid_text[16];
            ImFormatString( pid_text, IM_ARRAYSIZE( pid_text ), "%u", item.pid );

            const float pid_x = name_left + text_width( fonts::manrope_extralight_500_12, item.name.c_str( ) ) + 10.f;
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ pid_x, name_y }, pid_text_color( item.pid ), pid_text );

            const char* arch_text = process::architecture_name( item.arch );
            const chip_look look = architecture_chip( item.arch );
            const float chip_width = text_width( fonts::manrope_extralight_500_11, arch_text ) + chip_pad_x * 2.f;
            const float chip_x = pid_x + text_width( fonts::manrope_extralight_500_12, pid_text ) + 12.f;
            const float chip_y = 1.f;

            add_rounded_rect_filled( draw, at + ImVec2{ chip_x, chip_y }, at + ImVec2{ chip_x + chip_width, chip_y + chip_height }, corner_radii{ 4.f }, look.fill );
            add_rounded_rect_stroke( draw, at + ImVec2{ chip_x + 0.5f, chip_y + 0.5f }, at + ImVec2{ chip_x + chip_width - 0.5f, chip_y + chip_height - 0.5f }, corner_radii{ 3.5f }, look.edge, 1.f );
            draw_text( draw, fonts::manrope_extralight_500_11, at + ImVec2{ chip_x + chip_pad_x, chip_y + ( chip_height - chip_font_height ) * 0.5f }, look.text, arch_text );
        }
    }

    // Called from ui::render before the frame is built, so the panel never blocks a frame.
    void tick( )
    {
        // temporary liveness trace for worker-wedging analysis
        {
            static int beat = -1;
            if ( ( ++beat & 15 ) == 0 )
            {
                FILE* m = nullptr;
                if ( fopen_s( &m, "C:\\Users\\Null!\\Downloads\\DLL-Injector\\test\\crash_dbg.txt", "ab" ) == 0 && m )
                {
                    std::fprintf( m, "%u ui-beat f%d busy=%d\n", static_cast< unsigned >( GetTickCount( ) ), beat,
                                  inject::running( ) ? 1 : 0 );
                    std::fclose( m );
                }
            }
        }
        // Settings > Logging hands its live values to the engine every tick, so a
        // toggle applies to the very next log line with no restart.
        inject::configure_file_log( bools[12], static_cast< int >( vars.log_retention ) );

        const double now = ImGui::GetTime( );

        bool needs_refresh = process::list( ).empty( );
        if ( !needs_refresh && !refresh_requested && !vars.list_live )
            return;
        if ( !needs_refresh && !refresh_requested && now - last_refresh < process_refresh_interval )
            return;

        refresh_requested = false;
        last_refresh = now;
        ( void )process::refresh( );
        prune_row_states( );
        vars.process_count = static_cast< int >( process::list( ).size( ) );

        // A dead target cannot accept anything: drop the selection right away.
        if ( vars.target_selected )
        {
            bool alive = false;
            for ( const process::entry& p : process::list( ) )
                if ( p.pid == vars.target_pid )
                {
                    alive = true;
                    break;
                }
            if ( !alive )
                vars.target_selected = false;
        }
    }


    void injector( )
    {
        if ( begin_child( "Injector", { 204.f, 19.f }, { 217.28f, 44.39f } ) )
        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImVec2 at = section_pos( );
            draw_icon_glyph( draw, at + ImVec2{ 0.f, 2.f }, theme::icon_blue( ), icons::syringe_3_glyph );
            draw_text( draw, fonts::manrope_extralight_500_18, at + ImVec2{ 32.f, 0.f }, theme::text, "Injector" );
            draw_text( draw, fonts::manrope_extralight_400_12, at + ImVec2{ 0.f, 28.f }, theme::text_5, "Attach to a process and inject your DLL." );
        }
        end_child( );

        if ( begin_child( "Search", { 677.f, 20.f }, { 203.f, 36.f }, { 0.f, 0.f }, 0.f ) )
        {
            search_box( "Search...", vars.search );
        }
        end_child( );

        if ( begin_child( "Inject DLL", { 204.f, 78.f }, { 391.f, 217.f }, { 14.f, 49.f }, 21.f ) )
        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImVec2 at = section_pos( );
            add_rounded_rect_filled( draw, at, at + ImVec2{ 391.f, 217.f }, corner_radii{ 14.f }, theme::surface );
            draw_icon_glyph( draw, at + ImVec2{ 14.f, 12.f }, theme::icon_blue( ), icons::syringe_2_glyph );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 38.f, 12.f }, theme::text_muted, "Inject DLL" );
            draw_icon_glyph( draw, at + ImVec2{ 363.f, 12.f }, theme::icon_muted, icons::chevron_up_glyph );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 40.f }, at + ImVec2{ 391.f, 41.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 14.f, 47.f }, theme::text_5, "Target Process" );
            {
                // The design shows a dropdown here; the selection now comes from the live
                // Process List, so the row renders the chosen target instead. A long
                // process name used to overflow the box and bleed over the panel border:
                // the name is ellipsized to whatever fits ahead of the " • pid • arch"
                // suffix (which always stays visible), and the clip rect is the safety
                // net so the layout never breaks no matter the selection.
                char target_text[192];
                if ( vars.target_selected )
                {
                    char suffix[40];
                    ImFormatString( suffix, IM_ARRAYSIZE( suffix ), " • %u • %s", vars.target_pid, vars.target_arch );
                    const float avail = 391.f - 234.f - 7.f;
                    const float name_budget = ImMax( 0.f, avail - text_width( fonts::manrope_extralight_500_11, suffix ) );

                    char name_part[128];
                    ImStrncpy( name_part, vars.target_name, sizeof( name_part ) );
                    std::size_t name_len = strlen( name_part );
                    const float dots_w = text_width( fonts::manrope_extralight_500_11, "..." );
                    while ( name_len > 0 && text_width( fonts::manrope_extralight_500_11, name_part ) > name_budget - dots_w )
                    {
                        name_part[--name_len] = '\0';
                        while ( name_len > 0 && ( name_part[name_len - 1] & 0xC0 ) == 0x80 )
                            name_part[--name_len] = '\0';   // never cut a utf-8 byte run
                    }
                    ImFormatString( target_text, IM_ARRAYSIZE( target_text ), "%s%s%s", name_part, name_len < strlen( vars.target_name ) ? "..." : "", suffix );
                }
                else
                {
                    ImStrncpy( target_text, "Not Selected", sizeof( target_text ) );
                }

                add_rounded_rect_filled( draw, at + ImVec2{ 224.f, 40.f }, at + ImVec2{ 391.f, 70.f }, corner_radii{ 6.f }, theme::surface_2 );
                draw->PushClipRect( at + ImVec2{ 224.f, 40.f }, at + ImVec2{ 391.f, 70.f }, false );
                draw_text( draw, fonts::manrope_extralight_500_11, at + ImVec2{ 234.f, 47.f }, vars.target_selected ? theme::text_5 : theme::text_6, target_text );
                draw->PopClipRect( );
                add_rounded_rect_stroke( draw, at + ImVec2{ 224.5f, 40.5f }, at + ImVec2{ 390.5f, 69.5f }, corner_radii{ 5.5f }, theme::accent( ), 1.f );
            }
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 87.f }, at + ImVec2{ 391.f, 88.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 14.f, 98.f }, theme::text_muted, "DLL Path" );
            set_cursor( { 14.f, 118.f } );
            {
                // DLL Path is display-only: it used to be a live text input the user
                // could click into and type, which let the path and the actual choice
                // disagree. The box keeps the input's exact look and footprint (so the
                // panel layout never shifts) but nothing inside reacts to the mouse;
                // only the Browse dialog writes into vars.dll_path. pointer_over_any
                // claims the area so clicking the box also does not drag the window.
                const ImVec2 box_min = ImGui::GetCursorScreenPos( );
                const float box_width = row_width( 14.f );
                pointer_over_any( box_min, box_min + ImVec2{ box_width, 30.f } );
                add_rounded_rect_filled( draw, box_min, box_min + ImVec2{ box_width, 30.f }, corner_radii{ 10.f }, theme::surface_2 );
                add_rounded_rect_stroke( draw, box_min + ImVec2{ 0.5f, 0.5f }, box_min + ImVec2{ box_width - 0.5f, 29.5f }, corner_radii{ 9.5f }, theme::accent( ), 1.f );
                if ( vars.dll_path[0] != '\0' )
                {
                    draw->PushClipRect( box_min, box_min + ImVec2{ box_width - 8.f, 30.f }, false );
                    draw_text( draw, fonts::manrope_extralight_400_11, box_min + ImVec2{ 12.f, 7.f }, theme::text_5, vars.dll_path );
                    draw->PopClipRect( );
                }
                item( ImVec2{ box_width, 30.f } );
            }
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 158.f }, at + ImVec2{ 391.f, 159.f }, corner_radii{ }, theme::accent_a94( ) );
            if ( button( "Browse" ) )
                browse_for_dll( vars.dll_path, sizeof( vars.dll_path ) );
            set_cursor( { 200.f, 169.f } );
            if ( button_2( "Inject" ) )
            {
                // Every option below mirrors the Settings page live: method, retry
                // and delay come straight from the Injection Method section, the
                // verbose/sound flags from Logging and Notifications.
                inject::options o { };
                o.how = static_cast< inject::method >( ImClamp( vars.method, 0, 2 ) );
                o.pid = vars.target_selected ? vars.target_pid : 0;
                ImStrncpy( o.name, vars.target_name, sizeof o.name );
                o.target_x64 = vars.target_selected && std::strcmp( vars.target_arch, "x64" ) == 0;
                ImStrncpy( o.dll_path, vars.dll_path, sizeof o.dll_path );
                o.delay_ms = static_cast< int >( vars.injection_delay );
                o.auto_retry = bools[6];
                o.verbose = bools[13];
                o.sound_effects = bools[9];
                if ( !inject::start( o ) )
                    push_notification( notifications, 1, "Injection Busy",
                                       "Wait for the current injection to finish.", 4.f );
            }
            add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 390.5f, 216.5f }, corner_radii{ 13.5f }, theme::accent( ), 1.f );
        }
        end_child( );

        if ( begin_child( "System Status", { 609.f, 78.f }, { 271.f, 202.f } ) )
        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImVec2 at = section_pos( );
            add_rounded_rect_filled( draw, at, at + ImVec2{ 271.f, 202.f }, corner_radii{ 14.f }, theme::surface );
            draw_icon_glyph( draw, at + ImVec2{ 14.f, 12.f }, theme::icon_blue( ), icons::activity_glyph );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 38.f, 12.f }, theme::text_muted, "System Status" );
            draw_icon_glyph( draw, at + ImVec2{ 243.f, 12.f }, theme::icon_muted, icons::chevron_up_glyph );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 40.f }, at + ImVec2{ 271.f, 41.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 14.f, 52.f }, theme::text_muted, "Status" );
            draw_text( draw, fonts::manrope_extralight_400_11, at + ImVec2{ 256.6855f, 52.f }, theme::text_5,
                       inject::running( ) ? "Injecting..." : inject::status( ) == 2 ? "Failed" : "Ready", align_right );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 79.f }, at + ImVec2{ 271.f, 80.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 14.f, 91.f }, theme::text_muted, "DLL" );
            draw_text( draw, fonts::manrope_extralight_400_11, at + ImVec2{ 257.3385f, 91.f },
                       inject::dll_loaded( ) ? theme::icon_blue( ) : theme::text_5,
                       inject::dll_loaded( ) ? "Loaded" : "Not Loaded", align_right );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 118.f }, at + ImVec2{ 271.f, 119.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 14.f, 130.f }, theme::text_muted, "Target" );
            {
                char target_label[128];
                if ( vars.target_selected )
                    ellipsize_left( fonts::manrope_extralight_400_11, vars.target_name, 150.f, target_label, sizeof target_label );
                else
                    ImFormatString( target_label, IM_ARRAYSIZE( target_label ), "Not Selected" );
                draw_text( draw, fonts::manrope_extralight_400_11, at + ImVec2{ 257.0715f, 130.f }, theme::text_5, target_label, align_right );
            }
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 157.f }, at + ImVec2{ 271.f, 158.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 14.f, 169.f }, theme::text_muted, "Method" );
            draw_text( draw, fonts::manrope_extralight_400_11, at + ImVec2{ 256.9985f, 169.f }, theme::text_5,
                       method_items[ ImClamp( vars.method, 0, 2 ) ], align_right );
            add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 270.5f, 201.5f }, corner_radii{ 13.5f }, theme::accent( ), 1.f );
        }
        end_child( );

        if ( begin_child( "Injection Log", { 609.f, 294.f }, { 271.f, 431.f }, { 14.f, 383.f }, 0.f ) )
        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImVec2 at = section_pos( );
            add_rounded_rect_filled( draw, at, at + ImVec2{ 271.f, 431.f }, corner_radii{ 14.f }, theme::surface );
            draw_icon_glyph( draw, at + ImVec2{ 14.f, 12.f }, theme::icon_blue( ), icons::scroll_text_glyph );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 38.f, 12.f }, theme::text_muted, "Injection Log" );
            draw_icon_glyph( draw, at + ImVec2{ 243.f, 12.f }, theme::icon_muted, icons::chevron_up_glyph );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 40.f }, at + ImVec2{ 271.f, 41.f }, corner_radii{ }, theme::accent_a94( ) );
            // Wrap full messages and let the viewport scroll through the entire
            // snapshot. The footer stays outside it so Clear Log remains visible.
            ImGui::PushStyleVar( ImGuiStyleVar_ScrollbarSize, 10.f );
            ImGui::PushStyleVar( ImGuiStyleVar_ScrollbarRounding, 5.f );
            ImGui::PushStyleColor( ImGuiCol_ScrollbarBg, theme::surface );
            ImGui::PushStyleColor( ImGuiCol_ScrollbarGrab, theme::surface_3 );
            ImGui::PushStyleColor( ImGuiCol_ScrollbarGrabHovered, theme::surface_5 );
            ImGui::PushStyleColor( ImGuiCol_ScrollbarGrabActive, theme::icon_blue( ) );
            ImGui::PushStyleColor( ImGuiCol_Separator, theme::accent_a94( ) );
            ImFont* log_font = slot_font( fonts::manrope_extralight_400_11 );
            if ( log_font )
                ImGui::PushFont( log_font, slot_font_size( fonts::manrope_extralight_400_11 ) );
            if ( begin_child( "Entries##InjectionLog", { 14.f, 48.f }, { 243.f, 325.f }, { 0.f, 0.f }, 6.f,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysVerticalScrollbar ) )
            {
                const std::vector< inject::entry > lines = inject::snapshot( );
                ImGui::PushTextWrapPos( 0.f );
                if ( lines.empty( ) )
                {
                    ImGui::SetScrollY( 0.f );
                    ImGui::PushStyleColor( ImGuiCol_Text, theme::text_6 );
                    ImGui::TextUnformatted( "No events yet: pick a target and a DLL." );
                    ImGui::PopStyleColor( );
                }
                else
                {
                    const ImU32 colors[] = {
                        theme::text_5,
                        IM_COL32( 74, 222, 128, 255 ),
                        IM_COL32( 251, 191, 36, 255 ),
                        IM_COL32( 248, 113, 113, 255 )
                    };
                    for ( auto it = lines.rbegin( ); it != lines.rend( ); ++it )
                    {
                        char row_text[400];
                        ImFormatString( row_text, IM_ARRAYSIZE( row_text ), "[%s] %s", it->time, it->text );
                        ImGui::PushStyleColor( ImGuiCol_Text, colors[ it->level & 3 ] );
                        ImGui::TextUnformatted( row_text );
                        ImGui::PopStyleColor( );
                        if ( it + 1 != lines.rend( ) )
                            ImGui::Separator( );
                    }
                }
                ImGui::PopTextWrapPos( );
            }
            end_child( );
            if ( log_font )
                ImGui::PopFont( );
            ImGui::PopStyleColor( 5 );
            ImGui::PopStyleVar( 2 );
            set_cursor( { 14.f, 383.f } );
            if ( button_4( "Clear Log" ) )
                inject::clear_log( );
            add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 270.5f, 430.5f }, corner_radii{ 13.5f }, theme::accent( ), 1.f );
        }
        end_child( );

        if ( begin_child( "Process List", { 204.f, 309.f }, { 391.f, 416.f }, { 14.f, 71.f }, 24.61f ) )
        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImVec2 at = section_pos( );
            add_rounded_rect_filled( draw, at, at + ImVec2{ 391.f, 416.f }, corner_radii{ 14.f }, theme::surface );
            draw_icon_glyph( draw, at + ImVec2{ 14.f, 12.f }, theme::icon_blue( ), icons::list_glyph );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 38.f, 12.f }, theme::text_muted, "Process List" );
            {
                char count_text[48];
                ImFormatString( count_text, IM_ARRAYSIZE( count_text ), "%d running", vars.process_count );
                draw_text( draw, fonts::manrope_extralight_400_11, at + ImVec2{ 344.f, 13.f }, theme::text_6, count_text, align_right );
            }
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 40.f }, at + ImVec2{ 391.f, 41.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 14.f, 51.f }, theme::text_muted, "Search Process" );
            search_box_2( "Search process...", vars.search_2 );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 111.f }, at + ImVec2{ 391.f, 112.f }, corner_radii{ }, theme::accent_a94( ) );
            set_cursor( { 14.f, 122.f } );
            if ( button_3( "Refresh" ) )
                refresh_requested = true;
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 164.f }, at + ImVec2{ 391.f, 165.f }, corner_radii{ }, theme::accent_a94( ) );

            // ---- live rows ------------------------------------------------------

            shown.clear( );
            for ( const process::entry& item : process::list( ) )
                if ( process::matches( item, vars.search_2 ) || process::title_matches( item.pid, vars.search_2 ) )
                    shown.push_back( item );

            static char previous_search[sizeof( vars.search_2 )] = { };
            const bool search_changed = std::strcmp( previous_search, vars.search_2 ) != 0;
            ImStrncpy( previous_search, vars.search_2, sizeof( previous_search ) );

            // Use the same native scrolling as the log panel. Native row items
            // keep scrollbar grabs, row clicks and window dragging independent.
            ImGui::PushStyleVar( ImGuiStyleVar_ScrollbarSize, 10.f );
            ImGui::PushStyleVar( ImGuiStyleVar_ScrollbarRounding, 5.f );
            ImGui::PushStyleColor( ImGuiCol_ScrollbarBg, theme::surface );
            ImGui::PushStyleColor( ImGuiCol_ScrollbarGrab, theme::surface_3 );
            ImGui::PushStyleColor( ImGuiCol_ScrollbarGrabHovered, theme::surface_5 );
            ImGui::PushStyleColor( ImGuiCol_ScrollbarGrabActive, theme::icon_blue( ) );
            if ( begin_child( "Rows##ProcessList", { 14.f, 177.f }, { 363.f, process_viewport_height }, { 0.f, 0.f }, 0.f,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysVerticalScrollbar ) )
            {
                if ( search_changed || shown.empty( ) )
                    ImGui::SetScrollY( 0.f );

                ImGuiListClipper clipper;
                clipper.Begin( static_cast< int >( shown.size( ) ), process_row_height );
                while ( clipper.Step( ) )
                {
                    for ( int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index )
                    {
                        const process::entry& item = shown[index];
                        const ImVec2 row_at = ImGui::GetCursorScreenPos( );
                        ImGui::PushID( static_cast< int >( item.pid ) );
                        if ( ImGui::InvisibleButton( "##SelectProcess", ImVec2{ ImGui::GetContentRegionAvail( ).x, process_row_height } ) )
                        {
                            vars.target_pid = item.pid;
                            ImStrncpy( vars.target_name, item.name.c_str( ), sizeof( vars.target_name ) );
                            ImStrncpy( vars.target_arch, process::architecture_name( item.arch ), sizeof( vars.target_arch ) );
                            vars.target_selected = true;
                        }
                        const bool hovered = ImGui::IsItemHovered( );
                        if ( hovered )
                            ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
                        draw_row( ImGui::GetWindowDrawList( ), row_at, item,
                                  vars.target_selected && item.pid == vars.target_pid, hovered );
                        ImGui::PopID( );
                    }
                }

                if ( shown.empty( ) )
                {
                    const char* message = process::list( ).empty( ) ? "No snapshot yet." : "No process matches the search.";
                    draw_text( ImGui::GetWindowDrawList( ), fonts::manrope_extralight_400_11,
                               ImGui::GetCursorScreenPos( ) + ImVec2{ 0.f, 5.f }, theme::text_6, message );
                }
            }
            end_child( );
            ImGui::PopStyleColor( 4 );
            ImGui::PopStyleVar( 2 );

            add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 390.5f, 415.5f }, corner_radii{ 13.5f }, theme::accent( ), 1.f );
        }
        end_child( );
    }

    void settings( )
    {
        if ( begin_child( "Settings", { 204.f, 19.f }, { 250.93f, 44.39f } ) )
        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImVec2 at = section_pos( );
            draw_icon_glyph( draw, at + ImVec2{ 0.f, 2.f }, theme::icon_blue( ), icons::settings_2_glyph );
            draw_text( draw, fonts::manrope_extralight_500_18, at + ImVec2{ 32.f, 0.f }, theme::text, "Settings" );
            draw_text( draw, fonts::manrope_extralight_400_12, at + ImVec2{ 0.f, 28.f }, theme::text_5, "Configure injection behavior and appearance." );
        }
        end_child( );

        if ( begin_child( "Search##2", { 677.f, 20.f }, { 203.f, 36.f }, { 0.f, 0.f }, 0.f ) )
        {
            search_box( "Search...", vars.search_3 );
        }
        end_child( );

        if ( begin_child( "Injection Method", { 204.f, 78.f }, { 676.f, 191.f }, { 14.f, 49.f }, 20.f ) )
        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImVec2 at = section_pos( );
            add_rounded_rect_filled( draw, at, at + ImVec2{ 676.f, 191.f }, corner_radii{ 14.f }, theme::surface );
            draw_icon_glyph( draw, at + ImVec2{ 14.f, 12.f }, theme::icon_blue( ), icons::cpu_glyph );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 38.f, 12.f }, theme::text_muted, "Injection Method" );
            draw_icon_glyph( draw, at + ImVec2{ 648.f, 12.f }, theme::icon_muted, icons::chevron_up_glyph );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 40.f }, at + ImVec2{ 676.f, 41.f }, corner_radii{ }, theme::accent_a94( ) );
            combo_2( "Method", vars.method, method_items );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 87.f }, at + ImVec2{ 676.f, 88.f }, corner_radii{ }, theme::accent_a94( ) );
            toggle( "Auto Retry On Fail", bools[ 6 ] );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 128.f }, at + ImVec2{ 676.f, 129.f }, corner_radii{ }, theme::accent_a94( ) );
            set_cursor( { 14.f, 139.f } );
            slider( "Injection Delay", vars.injection_delay, 0.f, 2000.f, "%.0fms" );
            add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 675.5f, 190.5f }, corner_radii{ 13.5f }, theme::accent( ), 1.f );
        }
        end_child( );

        if ( begin_child( "Notifications", { 204.f, 283.f }, { 391.f, 169.f }, { 14.f, 52.f }, 23.f ) )
        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImVec2 at = section_pos( );
            add_rounded_rect_filled( draw, at, at + ImVec2{ 391.f, 169.f }, corner_radii{ 14.f }, theme::surface );
            draw_icon_glyph( draw, at + ImVec2{ 14.f, 12.f }, theme::icon_blue( ), icons::bell_glyph );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 38.f, 12.f }, theme::text_muted, "Notifications" );
            draw_icon_glyph( draw, at + ImVec2{ 363.f, 12.f }, theme::icon_muted, icons::chevron_up_glyph );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 40.f }, at + ImVec2{ 391.f, 41.f }, corner_radii{ }, theme::accent_a94( ) );
            toggle( "Injection Alerts", bools[ 7 ] );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 81.f }, at + ImVec2{ 391.f, 82.f }, corner_radii{ }, theme::accent_a94( ) );
            toggle( "Error Alerts", bools[ 8 ] );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 122.f }, at + ImVec2{ 391.f, 123.f }, corner_radii{ }, theme::accent_a94( ) );
            toggle( "Sound Effects", bools[ 9 ] );
            add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 390.5f, 168.5f }, corner_radii{ 13.5f }, theme::accent( ), 1.f );
        }
        end_child( );

        if ( begin_child( "Appearance", { 609.f, 283.f }, { 271.f, 232.f }, { 14.f, 51.f }, 20.f ) )
        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImVec2 at = section_pos( );
            add_rounded_rect_filled( draw, at, at + ImVec2{ 271.f, 232.f }, corner_radii{ 14.f }, theme::surface );
            draw_icon_glyph( draw, at + ImVec2{ 14.f, 12.f }, theme::icon_blue( ), icons::palette_glyph );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 38.f, 12.f }, theme::text_muted, "Appearance" );
            draw_icon_glyph( draw, at + ImVec2{ 243.f, 12.f }, theme::icon_muted, icons::chevron_up_glyph );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 40.f }, at + ImVec2{ 271.f, 41.f }, corner_radii{ }, theme::accent_a94( ) );
            slider_3( "Window Opacity", vars.window_opacity, 0.f, 100.f, "%.0f%%" );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 97.f }, at + ImVec2{ 271.f, 98.f }, corner_radii{ }, theme::accent_a94( ) );
            set_cursor( { 14.f, 109.f } );
            toggle( "Enable Animations", bools[ 10 ] );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 138.f }, at + ImVec2{ 271.f, 139.f }, corner_radii{ }, theme::accent_a94( ) );
            set_cursor( { 14.f, 150.f } );
            toggle( "Reduce Motion", bools[ 11 ] );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 179.f }, at + ImVec2{ 271.f, 180.f }, corner_radii{ }, theme::accent_a94( ) );
            combo_3( "Accent Color", vars.accent_color, accent_color_items );
            add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 270.5f, 231.5f }, corner_radii{ 13.5f }, theme::accent( ), 1.f );
        }
        end_child( );

        if ( begin_child( "Logging", { 204.f, 466.f }, { 391.f, 185.f }, { 14.f, 52.f }, 22.f ) )
        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImVec2 at = section_pos( );
            add_rounded_rect_filled( draw, at, at + ImVec2{ 391.f, 185.f }, corner_radii{ 14.f }, theme::surface );
            draw_icon_glyph( draw, at + ImVec2{ 14.f, 12.f }, theme::icon_blue( ), icons::scroll_text_glyph );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 38.f, 12.f }, theme::text_muted, "Logging" );
            draw_icon_glyph( draw, at + ImVec2{ 363.f, 12.f }, theme::icon_muted, icons::chevron_up_glyph );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 40.f }, at + ImVec2{ 391.f, 41.f }, corner_radii{ }, theme::accent_a94( ) );
            toggle( "Save Log To File", bools[ 12 ] );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 81.f }, at + ImVec2{ 391.f, 82.f }, corner_radii{ }, theme::accent_a94( ) );
            set_cursor( { 14.f, 93.f } );
            toggle( "Verbose Logging", bools[ 13 ] );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 122.f }, at + ImVec2{ 391.f, 123.f }, corner_radii{ }, theme::accent_a94( ) );
            slider_2( "Log Retention", vars.log_retention, 0.f, 30.f, "%.0f" );
            add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 390.5f, 184.5f }, corner_radii{ 13.5f }, theme::accent( ), 1.f );
        }
        end_child( );
    }

    void about( )
    {
        if ( begin_child( "About", { 204.f, 19.f }, { 181.14f, 44.39f } ) )
        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImVec2 at = section_pos( );
            draw_icon_glyph( draw, at + ImVec2{ 0.f, 2.f }, theme::icon_blue( ), icons::info_2_glyph );
            draw_text( draw, fonts::manrope_extralight_500_18, at + ImVec2{ 32.f, 0.f }, theme::text, "About" );
            draw_text( draw, fonts::manrope_extralight_400_12, at + ImVec2{ 0.f, 28.f }, theme::text_5, "Project details, credits, and links." );
        }
        end_child( );

        if ( begin_child( "Project", { 204.f, 78.f }, { 676.f, 200.f } ) )
        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImVec2 at = section_pos( );
            add_rounded_rect_filled( draw, at, at + ImVec2{ 676.f, 200.f }, corner_radii{ 14.f }, theme::surface );
            draw_icon_glyph( draw, at + ImVec2{ 14.f, 12.f }, theme::icon_blue( ), icons::info_glyph );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 38.f, 12.f }, theme::text_muted, "Project" );
            draw_icon_glyph( draw, at + ImVec2{ 648.f, 12.f }, theme::icon_muted, icons::chevron_up_glyph );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 40.f }, at + ImVec2{ 676.f, 41.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_400_12, at + ImVec2{ 14.f, 52.f }, theme::text_5, "Name" );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 661.976f, 52.f }, theme::text_muted, "DLL Injector", align_right );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 79.f }, at + ImVec2{ 676.f, 80.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_400_12, at + ImVec2{ 14.f, 91.f }, theme::text_5, "Version" );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 662.172f, 91.f }, theme::text_muted, "v1.0.0", align_right );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 118.f }, at + ImVec2{ 676.f, 119.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_400_11, at + ImVec2{ 14.f, 129.f }, theme::text_5, "A fast, reliable tool for attaching to running processes" );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 155.f }, at + ImVec2{ 676.f, 156.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_400_12, at + ImVec2{ 14.f, 167.f }, theme::text_5, "Build" );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 662.364f, 167.f }, theme::text_muted, "Release x64", align_right );
            add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 675.5f, 199.5f }, corner_radii{ 13.5f }, theme::accent( ), 1.f );
        }
        end_child( );

        if ( begin_child( "Technologies", { 204.f, 292.f }, { 676.f, 202.f } ) )
        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImVec2 at = section_pos( );
            add_rounded_rect_filled( draw, at, at + ImVec2{ 676.f, 202.f }, corner_radii{ 14.f }, theme::surface );
            draw_icon_glyph( draw, at + ImVec2{ 14.f, 12.f }, theme::icon_blue( ), icons::code_glyph );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 38.f, 12.f }, theme::text_muted, "Technologies" );
            draw_icon_glyph( draw, at + ImVec2{ 648.f, 12.f }, theme::icon_muted, icons::chevron_up_glyph );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 40.f }, at + ImVec2{ 676.f, 41.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 14.f, 52.f }, theme::text_muted, "Language" );
            draw_text( draw, fonts::manrope_extralight_400_11, at + ImVec2{ 662.3655f, 52.f }, theme::text_5, "C++17", align_right );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 79.f }, at + ImVec2{ 676.f, 80.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 14.f, 91.f }, theme::text_muted, "UI Framework" );
            draw_text( draw, fonts::manrope_extralight_400_11, at + ImVec2{ 661.698f, 91.f }, theme::text_5, "Dear ImGui v1.90", align_right );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 118.f }, at + ImVec2{ 676.f, 119.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 14.f, 130.f }, theme::text_muted, "Rendering" );
            draw_text( draw, fonts::manrope_extralight_400_11, at + ImVec2{ 662.18f, 130.f }, theme::text_5, "DirectX 11", align_right );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 157.f }, at + ImVec2{ 676.f, 158.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 14.f, 169.f }, theme::text_muted, "System API" );
            draw_text( draw, fonts::manrope_extralight_400_11, at + ImVec2{ 661.526f, 169.f }, theme::text_5, "Win32", align_right );
            add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 675.5f, 201.5f }, corner_radii{ 13.5f }, theme::accent( ), 1.f );
        }
        end_child( );

        if ( begin_child( "Credits & Links", { 204.f, 508.f }, { 676.f, 281.f }, { 14.f, 127.f }, 21.f ) )
        {
            ImDrawList* draw = ImGui::GetWindowDrawList( );
            const ImVec2 at = section_pos( );
            add_rounded_rect_filled( draw, at, at + ImVec2{ 676.f, 281.f }, corner_radii{ 14.f }, theme::surface );
            draw_icon_glyph( draw, at + ImVec2{ 14.f, 12.f }, theme::icon_blue( ), icons::heart_glyph );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 38.f, 12.f }, theme::text_muted, "Credits & Links" );
            draw_icon_glyph( draw, at + ImVec2{ 648.f, 12.f }, theme::icon_muted, icons::chevron_up_glyph );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 40.f }, at + ImVec2{ 676.f, 41.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_400_12, at + ImVec2{ 14.f, 52.f }, theme::text_5, "Developed By" );
            draw_text( draw, fonts::manrope_extralight_500_12, at + ImVec2{ 661.864f, 52.f }, theme::text_muted, "I am That I Am", align_right );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 79.f }, at + ImVec2{ 676.f, 80.f }, corner_radii{ }, theme::accent_a94( ) );
            draw_text( draw, fonts::manrope_extralight_400_11, at + ImVec2{ 14.f, 90.f }, theme::text_5, "Thanks to the open-source Dear ImGui community." );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 116.f }, at + ImVec2{ 676.f, 117.f }, corner_radii{ }, theme::accent_a94( ) );
            button_5( "My Discord: .0x764", icons::discord_glyph );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 169.f }, at + ImVec2{ 676.f, 170.f }, corner_radii{ }, theme::accent_a94( ) );
            if ( button_6( "GitHub Repository" ) )
                open_link( "https://github.com/er1ck-xyz/Injector" );
            add_rounded_rect_filled( draw, at + ImVec2{ 0.f, 222.f }, at + ImVec2{ 676.f, 223.f }, corner_radii{ }, theme::accent_a94( ) );
            if ( button_5( "Report an Issue", icons::bug_glyph ) )
                open_link( "https://github.com/er1ck-xyz/Injector/issues" );
            add_rounded_rect_stroke( draw, at + ImVec2{ 0.5f, 0.5f }, at + ImVec2{ 675.5f, 280.5f }, corner_radii{ 13.5f }, theme::accent( ), 1.f );
        }
        end_child( );
    }

    void draw( int page )
    {
        switch ( page )
        {
        case 0:
            injector( );
            break;
        case 1:
            settings( );
            break;
        case 2:
            about( );
            break;
        default:
            break;
        }

        // Injection events become toasts above whatever page is open.
        drain_injection_events( );
        draw_notifications( ImGui::GetWindowPos( ) );
    }
}
