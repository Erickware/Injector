#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <imgui.h>

#pragma comment( lib, "dwmapi.lib" )

namespace lazyui
{
    struct backdrop_shape
    {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        int radii[4]{ };

        [[nodiscard]] bool operator==( const backdrop_shape& other ) const noexcept
        {
            return left == other.left && top == other.top && right == other.right && bottom == other.bottom &&
                   std::equal( std::begin( radii ), std::end( radii ), std::begin( other.radii ) );
        }
    };

    struct backdrop_registry
    {
        std::vector< backdrop_shape > shapes;
        int frame = -1;
    };

    inline backdrop_registry& window_backdrops( )
    {
        static backdrop_registry registry;
        return registry;
    }

    inline void add_window_backdrop( ImVec2 min, ImVec2 max, const float ( &radii )[4] )
    {
        backdrop_registry& registry = window_backdrops( );
        const int frame = ImGui::GetFrameCount( );
        if ( registry.frame != frame )
        {
            registry.shapes.clear( );
            registry.frame = frame;
        }
        if ( max.x - min.x < 1.f || max.y - min.y < 1.f )
            return;
        backdrop_shape shape{ static_cast< int >( std::floor( min.x ) ), static_cast< int >( std::floor( min.y ) ),
                              static_cast< int >( std::ceil( max.x ) ), static_cast< int >( std::ceil( max.y ) ), { } };
        for ( int corner = 0; corner < 4; ++corner )
            shape.radii[corner] = static_cast< int >( std::lround( ( std::max )( 0.f, radii[corner] ) ) );
        registry.shapes.push_back( shape );
    }

    class backdrop_window final
    {
    public:
        backdrop_window( ) = default;
        ~backdrop_window( ) { destroy( ); }

        backdrop_window( const backdrop_window& ) = delete;
        backdrop_window& operator=( const backdrop_window& ) = delete;

        void update( HWND owner, const std::vector< backdrop_shape >& shapes ) noexcept
        {
            if ( shapes.empty( ) || !owner || ::IsIconic( owner ) || !::IsWindowVisible( owner ) )
            {
                hide( );
                return;
            }

            RECT client{ };
            ::GetClientRect( owner, &client );
            RECT box{ client.right, client.bottom, client.left, client.top };
            for ( const backdrop_shape& shape : shapes )
            {
                box.left = ( std::min )( box.left, static_cast< LONG >( shape.left ) );
                box.top = ( std::min )( box.top, static_cast< LONG >( shape.top ) );
                box.right = ( std::max )( box.right, static_cast< LONG >( shape.right ) );
                box.bottom = ( std::max )( box.bottom, static_cast< LONG >( shape.bottom ) );
            }
            box.left = ( std::max )( box.left, client.left );
            box.top = ( std::max )( box.top, client.top );
            box.right = ( std::min )( box.right, client.right );
            box.bottom = ( std::min )( box.bottom, client.bottom );
            if ( box.right <= box.left || box.bottom <= box.top )
            {
                hide( );
                return;
            }
            if ( !window_ && !create( owner ) )
                return;

            POINT origin{ box.left, box.top };
            ::ClientToScreen( owner, &origin );
            const int width = box.right - box.left;
            const int height = box.bottom - box.top;

            std::vector< backdrop_shape > local = shapes;
            for ( backdrop_shape& shape : local )
            {
                shape.left -= box.left;
                shape.right -= box.left;
                shape.top -= box.top;
                shape.bottom -= box.top;
            }
            if ( local != shapes_ )
            {
                shapes_ = std::move( local );
                HRGN region = ::CreateRectRgn( 0, 0, 0, 0 );
                for ( const backdrop_shape& shape : shapes_ )
                {
                    HRGN part = rounded_region( shape );
                    if ( part )
                    {
                        ::CombineRgn( region, region, part, RGN_OR );
                        ::DeleteObject( part );
                    }
                }
                if ( !::SetWindowRgn( window_, region, visible_ ) )
                    ::DeleteObject( region );
                round_corners( width, height );
            }

            const bool placed = ::GetWindow( owner, GW_HWNDNEXT ) == window_;
            corner_ = POINT{ box.left, box.top };
            if ( !visible_ || !placed || origin.x != origin_.x || origin.y != origin_.y || width != width_ || height != height_ )
            {
                ::SetWindowPos( window_, owner, origin.x, origin.y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER );
                origin_ = origin;
                width_ = width;
                height_ = height;
                visible_ = true;
            }
        }

        void follow( HWND owner ) noexcept
        {
            if ( !window_ || !visible_ || !owner || !::IsWindowVisible( owner ) )
                return;
            POINT origin = corner_;
            ::ClientToScreen( owner, &origin );
            if ( origin.x == origin_.x && origin.y == origin_.y )
                return;
            ::SetWindowPos( window_, nullptr, origin.x, origin.y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER );
            origin_ = origin;
        }

        void destroy( ) noexcept
        {
            if ( window_ && ::IsWindow( window_ ) )
                ::DestroyWindow( window_ );
            window_ = nullptr;
            visible_ = false;
            shapes_.clear( );
        }

    private:
        struct accent_policy
        {
            int state;
            int flags;
            DWORD gradient_color;
            int animation_id;
        };

        struct composition_attribute
        {
            int attribute;
            void* data;
            SIZE_T size;
        };

        static LRESULT CALLBACK window_proc( HWND window, UINT message, WPARAM w_param, LPARAM l_param ) noexcept
        {
            if ( message == WM_NCHITTEST )
                return HTTRANSPARENT;
            if ( message == WM_MOUSEACTIVATE )
                return MA_NOACTIVATE;
            return ::DefWindowProcW( window, message, w_param, l_param );
        }

        bool create( HWND owner ) noexcept
        {
            const HINSTANCE instance = ::GetModuleHandleW( nullptr );
            WNDCLASSEXW window_class{ };
            window_class.cbSize = sizeof( window_class );
            window_class.lpfnWndProc = &window_proc;
            window_class.hInstance = instance;
            window_class.hbrBackground = static_cast< HBRUSH >( ::GetStockObject( BLACK_BRUSH ) );
            window_class.lpszClassName = L"LazyUIBackdrop";
            if ( !::RegisterClassExW( &window_class ) && ::GetLastError( ) != ERROR_CLASS_ALREADY_EXISTS )
                return false;

            DWORD extended = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
            if ( ::GetWindowLongPtrW( owner, GWL_EXSTYLE ) & WS_EX_TOPMOST )
                extended |= WS_EX_TOPMOST;
            window_ = ::CreateWindowExW( extended, window_class.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr );
            if ( !window_ )
                return false;

            const MARGINS margins{ -1, -1, -1, -1 };
            ::DwmExtendFrameIntoClientArea( window_, &margins );
            using set_composition = BOOL( WINAPI* )( HWND, composition_attribute* );
            const auto set = reinterpret_cast< set_composition >( ::GetProcAddress( ::GetModuleHandleW( L"user32.dll" ), "SetWindowCompositionAttribute" ) );
            if ( set )
            {
                accent_policy accent{ 3 , 0, 0, 0 };
                composition_attribute data{ 19 , &accent, sizeof( accent ) };
                set( window_, &data );
            }
            return true;
        }

        void hide( ) noexcept
        {
            if ( window_ && visible_ )
                ::ShowWindow( window_, SW_HIDE );
            visible_ = false;
        }

        void round_corners( int width, int height ) noexcept
        {
            int smallest = INT_MAX;
            for ( const backdrop_shape& shape : shapes_ )
            {
                const bool left = shape.left <= 0;
                const bool top = shape.top <= 0;
                const bool right = shape.right >= width;
                const bool bottom = shape.bottom >= height;
                if ( left && top )
                    smallest = ( std::min )( smallest, shape.radii[0] );
                if ( right && top )
                    smallest = ( std::min )( smallest, shape.radii[1] );
                if ( right && bottom )
                    smallest = ( std::min )( smallest, shape.radii[2] );
                if ( left && bottom )
                    smallest = ( std::min )( smallest, shape.radii[3] );
            }
            HDC screen = ::GetDC( nullptr );
            const float scale = screen ? static_cast< float >( ::GetDeviceCaps( screen, LOGPIXELSX ) ) / 96.f : 1.f;
            if ( screen )
                ::ReleaseDC( nullptr, screen );
            constexpr DWORD corner_preference = 33;
            const int preference = smallest == INT_MAX || static_cast< float >( smallest ) >= 8.f * scale ? 2 : static_cast< float >( smallest ) >= 4.f * scale ? 3 : 1;
            ::DwmSetWindowAttribute( window_, corner_preference, &preference, sizeof( preference ) );
        }

        static HRGN rounded_region( const backdrop_shape& shape ) noexcept
        {
            const float width = static_cast< float >( shape.right - shape.left );
            const float height = static_cast< float >( shape.bottom - shape.top );
            if ( width <= 0.f || height <= 0.f )
                return nullptr;
            float radii[4];
            float scale = 1.f;
            for ( int corner = 0; corner < 4; ++corner )
                radii[corner] = static_cast< float >( shape.radii[corner] );
            if ( radii[0] + radii[1] > width ) scale = ( std::min )( scale, width / ( radii[0] + radii[1] ) );
            if ( radii[3] + radii[2] > width ) scale = ( std::min )( scale, width / ( radii[3] + radii[2] ) );
            if ( radii[0] + radii[3] > height ) scale = ( std::min )( scale, height / ( radii[0] + radii[3] ) );
            if ( radii[1] + radii[2] > height ) scale = ( std::min )( scale, height / ( radii[1] + radii[2] ) );
            const float left = static_cast< float >( shape.left );
            const float top = static_cast< float >( shape.top );
            const float right = static_cast< float >( shape.right );
            const float bottom = static_cast< float >( shape.bottom );
            const float centers[4][2]{
                { left + radii[0] * scale, top + radii[0] * scale },
                { right - radii[1] * scale, top + radii[1] * scale },
                { right - radii[2] * scale, bottom - radii[2] * scale },
                { left + radii[3] * scale, bottom - radii[3] * scale }
            };
            constexpr float pi = 3.14159265f;
            std::vector< POINT > points;
            for ( int corner = 0; corner < 4; ++corner )
            {
                const float radius = radii[corner] * scale;
                const int steps = radius < 1.f ? 0 : ( std::clamp )( static_cast< int >( radius / 2.f ), 4, 24 );
                const float start = pi + corner * pi * 0.5f;
                for ( int step = 0; step <= steps; ++step )
                {
                    const float angle = start + ( steps == 0 ? 0.f : pi * 0.5f * static_cast< float >( step ) / static_cast< float >( steps ) );
                    points.push_back( POINT{ static_cast< LONG >( std::lround( centers[corner][0] + std::cos( angle ) * radius ) ),
                                             static_cast< LONG >( std::lround( centers[corner][1] + std::sin( angle ) * radius ) ) } );
                }
            }
            return ::CreatePolygonRgn( points.data( ), static_cast< int >( points.size( ) ), WINDING );
        }

        HWND window_ = nullptr;
        bool visible_ = false;
        POINT corner_{ };
        POINT origin_{ };
        int width_ = 0;
        int height_ = 0;
        std::vector< backdrop_shape > shapes_;
    };

    class backdrop_windows final
    {
    public:
        void update( HWND owner ) noexcept
        {
            const backdrop_registry& registry = window_backdrops( );
            const std::vector< std::vector< backdrop_shape > > groups = registry.frame == ImGui::GetFrameCount( ) ? grouped( registry.shapes ) : std::vector< std::vector< backdrop_shape > >{ };
            while ( windows_.size( ) < groups.size( ) )
                windows_.push_back( std::make_unique< backdrop_window >( ) );
            const std::vector< backdrop_shape > none;
            for ( std::size_t index = 0; index < windows_.size( ); ++index )
                windows_[index]->update( owner, index < groups.size( ) ? groups[index] : none );
        }

        void follow( HWND owner ) noexcept
        {
            for ( const std::unique_ptr< backdrop_window >& window : windows_ )
                window->follow( owner );
        }

        void destroy( ) noexcept
        {
            for ( const std::unique_ptr< backdrop_window >& window : windows_ )
                window->destroy( );
            windows_.clear( );
        }

    private:
        static std::vector< std::vector< backdrop_shape > > grouped( const std::vector< backdrop_shape >& shapes )
        {
            struct group
            {
                RECT box;
                std::size_t first;
                std::vector< backdrop_shape > shapes;
            };
            const auto touch = []( const RECT& left, const RECT& right ) noexcept
            {
                return left.left <= right.right + 1 && right.left <= left.right + 1 && left.top <= right.bottom + 1 && right.top <= left.bottom + 1;
            };
            std::vector< group > groups;
            for ( std::size_t index = 0; index < shapes.size( ); ++index )
            {
                const backdrop_shape& shape = shapes[index];
                group next{ RECT{ shape.left, shape.top, shape.right, shape.bottom }, index, { shape } };
                for ( bool merged = true; merged; )
                {
                    merged = false;
                    for ( auto other = groups.begin( ); other != groups.end( ); ++other )
                    {
                        if ( !touch( other->box, next.box ) )
                            continue;
                        next.box = RECT{ ( std::min )( next.box.left, other->box.left ), ( std::min )( next.box.top, other->box.top ),
                                         ( std::max )( next.box.right, other->box.right ), ( std::max )( next.box.bottom, other->box.bottom ) };
                        next.first = ( std::min )( next.first, other->first );
                        next.shapes.insert( next.shapes.begin( ), other->shapes.begin( ), other->shapes.end( ) );
                        groups.erase( other );
                        merged = true;
                        break;
                    }
                }
                groups.push_back( std::move( next ) );
            }
            std::sort( groups.begin( ), groups.end( ), []( const group& left, const group& right ) { return left.first < right.first; } );
            constexpr std::size_t most = 8;
            std::vector< std::vector< backdrop_shape > > result;
            for ( group& item : groups )
            {
                if ( result.size( ) < most )
                    result.push_back( std::move( item.shapes ) );
                else
                    result.back( ).insert( result.back( ).end( ), item.shapes.begin( ), item.shapes.end( ) );
            }
            return result;
        }

        std::vector< std::unique_ptr< backdrop_window > > windows_;
    };
}
