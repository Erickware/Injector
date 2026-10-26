#include "lazyui/lazyui.hpp"
#include "lazyui/window.hpp"

#include "ui/ui.hpp"

#include <cstdlib>

int WINAPI WinMain( HINSTANCE instance, HINSTANCE, LPSTR, int )
{
    lazyui::window_config config;
    config.class_name = L"LazyUILoader";
    config.title = L"LazyUI Loader";
    config.width = static_cast< int >( ui::frame_size( ).x );
    config.height = static_cast< int >( ui::frame_size( ).y );
    config.x = ( ::GetSystemMetrics( SM_CXSCREEN ) - config.width ) / 2;
    config.y = ( ::GetSystemMetrics( SM_CYSCREEN ) - config.height ) / 2;

    lazyui::app_window host;
    if ( !host.initialize( instance, config ) )
    {
        ::MessageBoxW( nullptr, L"DirectX 11 could not be initialized.", config.title.c_str( ), MB_OK | MB_ICONERROR );
        return EXIT_FAILURE;
    }
    lazyui::app = { host.window( ), host.device( ), host.device_context( ), host.swap_chain( ) };

    ( void )ui::load_fonts( );

    host.show( SW_SHOW );
    while ( host.pump_messages( ) )
    {
        host.begin_frame( );
        ui::render( );
        host.end_frame( );
    }
    return EXIT_SUCCESS;
}
