#pragma once

#ifndef LAZYUI_ENABLE_TEST_HARNESS
#define LAZYUI_ENABLE_TEST_HARNESS 0
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "backdrop.hpp"

#pragma comment( lib, "d3d11.lib" )
#pragma comment( lib, "dwmapi.lib" )
#pragma comment( lib, "dxgi.lib" )
#pragma comment( lib, "windowscodecs.lib" )

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND window, UINT message, WPARAM w_param, LPARAM l_param );

namespace lazyui
{
    struct window_config
    {
        std::wstring class_name;
        std::wstring title;
        DWORD style = WS_POPUP;
        DWORD extended_style = 0;
        int x = CW_USEDEFAULT;
        int y = CW_USEDEFAULT;
        int width = 1280;
        int height = 720;
        bool dpi_aware = false;
    };

    class app_window final
    {
    public:
        app_window( ) = default;
        ~app_window( ) { shutdown( ); }

        app_window( const app_window& ) = delete;
        app_window& operator=( const app_window& ) = delete;
        app_window( app_window&& ) = delete;
        app_window& operator=( app_window&& ) = delete;

        [[nodiscard]] bool initialize( HINSTANCE instance, const window_config& config ) noexcept
        {
            if ( initialized_ || !instance || config.class_name.empty( ) )
                return false;

            if ( config.dpi_aware )
                ::SetProcessDPIAware( );

            instance_ = instance;
            class_name_ = config.class_name;

            WNDCLASSEXW window_class{ };
            window_class.cbSize = sizeof( window_class );
            window_class.style = CS_CLASSDC;
            window_class.lpfnWndProc = &window_proc;
            window_class.hInstance = instance_;
            window_class.lpszClassName = class_name_.c_str( );

            if ( !::RegisterClassExW( &window_class ) )
            {
                shutdown( );
                return false;
            }
            class_registered_ = true;

            window_ = ::CreateWindowExW(
                config.extended_style,
                class_name_.c_str( ),
                config.title.c_str( ),
                config.style,
                config.x,
                config.y,
                config.width,
                config.height,
                nullptr,
                nullptr,
                instance_,
                this );

            if ( !window_ || !create_device( ) )
            {
                shutdown( );
                return false;
            }

            IMGUI_CHECKVERSION( );
            ImGui::CreateContext( );
            imgui_context_created_ = true;

            ::SetLayeredWindowAttributes( window_, RGB( 0, 0, 0 ), 255, LWA_ALPHA );
            const MARGINS margins{ -1, -1, -1, -1 };
            ::DwmExtendFrameIntoClientArea( window_, &margins );

            if ( !ImGui_ImplWin32_Init( window_ ) )
            {
                shutdown( );
                return false;
            }
            win32_backend_initialized_ = true;

            if ( !ImGui_ImplDX11_Init( device_.Get( ), device_context_.Get( ) ) )
            {
                shutdown( );
                return false;
            }
            dx11_backend_initialized_ = true;

#if LAZYUI_ENABLE_TEST_HARNESS
            wchar_t capture_path[MAX_PATH]{ };
            if ( ::GetEnvironmentVariableW( L"LAZYUI_CAPTURE", capture_path, MAX_PATH ) > 0 )
                capture_path_ = capture_path;

            std::vector< wchar_t > script( 32768 );
            if ( ::GetEnvironmentVariableW( L"LAZYUI_CAPTURE_SCRIPT", script.data( ), static_cast< DWORD >( script.size( ) ) ) > 0 )
                parse_script( script.data( ) );
#endif

            initialized_ = true;
            return true;
        }

        void show( int command = SW_SHOWDEFAULT ) const noexcept
        {
            if ( !window_ )
                return;
#if LAZYUI_ENABLE_TEST_HARNESS
            if ( !capture_path_.empty( ) || scripted_ )
                return;
#endif
            ::ShowWindow( window_, command );
            ::UpdateWindow( window_ );
        }

        [[nodiscard]] bool pump_messages( ) const noexcept
        {
            MSG message{ };
            while ( ::PeekMessageW( &message, nullptr, 0U, 0U, PM_REMOVE ) )
            {
                if ( message.message == WM_QUIT )
                    return false;
                ::TranslateMessage( &message );
                ::DispatchMessageW( &message );
            }
            return true;
        }

        void begin_frame( ) noexcept
        {
            ImGui_ImplDX11_NewFrame( );
            ImGui_ImplWin32_NewFrame( );
#if LAZYUI_ENABLE_TEST_HARNESS
            if ( scripted_ )
                play_script( );
#endif
            ImGui::NewFrame( );
        }

        void end_frame( const std::array< float, 4 >& clear_color = { 0.f, 0.f, 0.f, 0.f } ) noexcept
        {
            ImGui::Render( );
            ID3D11RenderTargetView* target = render_target_.Get( );
            device_context_->OMSetRenderTargets( 1, &target, nullptr );
            device_context_->ClearRenderTargetView( target, clear_color.data( ) );
            ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData( ) );
#if LAZYUI_ENABLE_TEST_HARNESS
            if ( !capture_path_.empty( ) && !scripted_ && ++captured_frames_ == capture_frame_count )
            {
                const bool saved = save_back_buffer( capture_path_ );
                ::PostQuitMessage( saved ? EXIT_SUCCESS : EXIT_FAILURE );
            }
            if ( scripted_ )
            {
                if ( !pending_shot_.empty( ) && !save_back_buffer( pending_shot_ ) )
                    script_failed_ = true;
                pending_shot_.clear( );
                if ( script_step_ >= script_.size( ) )
                    ::PostQuitMessage( script_failed_ ? EXIT_FAILURE : EXIT_SUCCESS );
            }
            if ( capture_path_.empty( ) && !scripted_ )
                present_on_screen( );
            else
                swap_chain_->Present( 0, 0 );
#else
            present_on_screen( );
#endif
        }

        void shutdown( ) noexcept
        {
            initialized_ = false;
            backdrop_.destroy( );

            if ( dx11_backend_initialized_ )
            {
                ImGui_ImplDX11_Shutdown( );
                dx11_backend_initialized_ = false;
            }
            if ( win32_backend_initialized_ )
            {
                ImGui_ImplWin32_Shutdown( );
                win32_backend_initialized_ = false;
            }
            if ( imgui_context_created_ )
            {
                ImGui::DestroyContext( );
                imgui_context_created_ = false;
            }

            if ( device_context_ )
                device_context_->ClearState( );
            frame_done_.Reset( );
            render_target_.Reset( );
            swap_chain_.Reset( );
            device_context_.Reset( );
            device_.Reset( );

            if ( window_ )
            {
                if ( ::IsWindow( window_ ) )
                    ::DestroyWindow( window_ );
                window_ = nullptr;
            }
            if ( class_registered_ && instance_ && !class_name_.empty( ) )
            {
                ::UnregisterClassW( class_name_.c_str( ), instance_ );
                class_registered_ = false;
            }

            class_name_.clear( );
            instance_ = nullptr;
        }

        [[nodiscard]] HWND window( ) const noexcept { return window_; }
        [[nodiscard]] ID3D11Device* device( ) const noexcept { return device_.Get( ); }
        [[nodiscard]] ID3D11DeviceContext* device_context( ) const noexcept { return device_context_.Get( ); }
        [[nodiscard]] IDXGISwapChain* swap_chain( ) const noexcept { return swap_chain_.Get( ); }

    private:
#if LAZYUI_ENABLE_TEST_HARNESS
        struct script_step
        {
            std::wstring command;
            std::wstring text;
            float x = 0.f;
            float y = 0.f;
            int frames = 0;
        };
#endif

        void present_on_screen( ) noexcept
        {
            if ( !paced_ )
            {
                backdrop_.update( window_ );
                swap_chain_->Present( 1, 0 );
                return;
            }
            swap_chain_->Present( 0, 0 );
            wait_for_gpu( );
            backdrop_.update( window_ );
            LARGE_INTEGER frequency{ };
            LARGE_INTEGER before{ };
            LARGE_INTEGER after{ };
            ::QueryPerformanceFrequency( &frequency );
            ::QueryPerformanceCounter( &before );
            if ( FAILED( ::DwmFlush( ) ) )
            {
                paced_ = false;
                return;
            }
            ::QueryPerformanceCounter( &after );
            quick_flushes_ = ( after.QuadPart - before.QuadPart ) * 2000 < frequency.QuadPart ? quick_flushes_ + 1 : 0;
            if ( quick_flushes_ > 2 )
                ::Sleep( 8 );
        }

        void wait_for_gpu( ) noexcept
        {
            if ( !frame_done_ )
            {
                D3D11_QUERY_DESC description{ };
                description.Query = D3D11_QUERY_EVENT;
                if ( FAILED( device_->CreateQuery( &description, frame_done_.GetAddressOf( ) ) ) )
                    return;
            }
            device_context_->End( frame_done_.Get( ) );
            const ULONGLONG started = ::GetTickCount64( );
            BOOL done = FALSE;
            while ( device_context_->GetData( frame_done_.Get( ), &done, sizeof( done ), 0 ) == S_FALSE && ::GetTickCount64( ) - started < 50 )
                ::SwitchToThread( );
        }

#if LAZYUI_ENABLE_TEST_HARNESS
        void parse_script( const std::wstring& source )
        {
            std::size_t start = 0;
            while ( start < source.size( ) )
            {
                std::size_t end = source.find( L';', start );
                if ( end == std::wstring::npos )
                    end = source.size( );
                std::wstring item = source.substr( start, end - start );
                start = end + 1;
                const std::size_t first = item.find_first_not_of( L" \t" );
                const std::size_t last = item.find_last_not_of( L" \t" );
                if ( first == std::wstring::npos )
                    continue;
                item = item.substr( first, last - first + 1 );
                const std::size_t space = item.find( L' ' );
                script_step step{ };
                step.command = item.substr( 0, space );
                step.text = space == std::wstring::npos ? L"" : item.substr( space + 1 );
                if ( step.command == L"move" )
                    ::swscanf_s( step.text.c_str( ), L"%f %f", &step.x, &step.y );
                else if ( step.command == L"wait" )
                    step.frames = ::_wtoi( step.text.c_str( ) );
                if ( step.command == L"key" )
                {
                    script_step release = step;
                    step.command = L"keydown";
                    release.command = L"keyup";
                    script_.push_back( std::move( step ) );
                    script_.push_back( std::move( release ) );
                    continue;
                }
                script_.push_back( std::move( step ) );
            }
            scripted_ = !script_.empty( );
        }

        void play_script( )
        {
            ImGuiIO& io = ImGui::GetIO( );
            io.DeltaTime = 1.f / 60.f;
            while ( script_step_ < script_.size( ) )
            {
                script_step& step = script_[script_step_];
                if ( step.command == L"wait" )
                {
                    if ( step.frames-- > 0 )
                        return;
                    ++script_step_;
                    continue;
                }
                ++script_step_;
                if ( step.command == L"move" )
                    io.AddMousePosEvent( step.x, step.y );
                else if ( step.command == L"down" || step.command == L"up" )
                    io.AddMouseButtonEvent( ImGuiMouseButton_Left, step.command == L"down" );
                else if ( step.command == L"type" )
                {
                    for ( const wchar_t character : step.text )
                        io.AddInputCharacterUTF16( static_cast< ImWchar16 >( character ) );
                }
                else if ( step.command == L"keydown" || step.command == L"keyup" )
                {
                    std::string name;
                    for ( const wchar_t character : step.text )
                        name.push_back( static_cast< char >( character ) );
                    for ( int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key )
                    {
                        if ( name == ImGui::GetKeyName( static_cast< ImGuiKey >( key ) ) )
                        {
                            io.AddKeyEvent( static_cast< ImGuiKey >( key ), step.command == L"keydown" );
                            break;
                        }
                    }
                }
                else if ( step.command == L"shot" )
                    pending_shot_ = step.text;
                return;
            }
        }
#endif

        [[nodiscard]] bool create_device( ) noexcept
        {
            DXGI_SWAP_CHAIN_DESC description{ };
            description.BufferCount = 2;
            description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            description.BufferDesc.RefreshRate.Numerator = 60;
            description.BufferDesc.RefreshRate.Denominator = 1;
            description.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
            description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            description.OutputWindow = window_;
            description.SampleDesc.Count = 1;
            description.Windowed = TRUE;
            description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

            constexpr D3D_FEATURE_LEVEL feature_levels[] = {
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_0
            };
            D3D_FEATURE_LEVEL selected_feature_level{ };

            const HRESULT result = ::D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                0,
                feature_levels,
                static_cast< UINT >( std::size( feature_levels ) ),
                D3D11_SDK_VERSION,
                &description,
                swap_chain_.GetAddressOf( ),
                device_.GetAddressOf( ),
                &selected_feature_level,
                device_context_.GetAddressOf( ) );

            return SUCCEEDED( result ) && create_render_target( );
        }

        [[nodiscard]] bool create_render_target( ) noexcept
        {
            if ( !swap_chain_ || !device_ )
                return false;

            Microsoft::WRL::ComPtr< ID3D11Texture2D > back_buffer;
            if ( FAILED( swap_chain_->GetBuffer( 0, IID_PPV_ARGS( back_buffer.GetAddressOf( ) ) ) ) )
                return false;

            return SUCCEEDED( device_->CreateRenderTargetView( back_buffer.Get( ), nullptr, render_target_.GetAddressOf( ) ) );
        }

#if LAZYUI_ENABLE_TEST_HARNESS
        [[nodiscard]] bool save_back_buffer( const std::wstring& path ) noexcept
        {
            Microsoft::WRL::ComPtr< ID3D11Texture2D > back_buffer;
            if ( FAILED( swap_chain_->GetBuffer( 0, IID_PPV_ARGS( back_buffer.GetAddressOf( ) ) ) ) )
                return false;
            D3D11_TEXTURE2D_DESC description{ };
            back_buffer->GetDesc( &description );
            description.Usage = D3D11_USAGE_STAGING;
            description.BindFlags = 0;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            description.MiscFlags = 0;
            Microsoft::WRL::ComPtr< ID3D11Texture2D > staging;
            if ( FAILED( device_->CreateTexture2D( &description, nullptr, staging.GetAddressOf( ) ) ) )
                return false;
            device_context_->CopyResource( staging.Get( ), back_buffer.Get( ) );

            D3D11_MAPPED_SUBRESOURCE mapped{ };
            if ( FAILED( device_context_->Map( staging.Get( ), 0, D3D11_MAP_READ, 0, &mapped ) ) )
                return false;
            const UINT stride = description.Width * 4;
            std::vector< std::uint8_t > pixels( static_cast< std::size_t >( stride ) * description.Height );
            for ( UINT y = 0; y < description.Height; ++y )
            {
                const auto* source = static_cast< const std::uint8_t* >( mapped.pData ) + static_cast< std::size_t >( y ) * mapped.RowPitch;
                std::uint8_t* destination = pixels.data( ) + static_cast< std::size_t >( y ) * stride;
                for ( UINT x = 0; x < description.Width; ++x )
                {
                    const std::uint8_t alpha = source[x * 4 + 3];
                    const auto straight = [alpha]( std::uint8_t value ) -> std::uint8_t {
                        return alpha == 0 ? 0 : static_cast< std::uint8_t >( ( std::min )( 255, ( value * 255 + alpha / 2 ) / alpha ) );
                    };
                    destination[x * 4 + 0] = straight( source[x * 4 + 2] );
                    destination[x * 4 + 1] = straight( source[x * 4 + 1] );
                    destination[x * 4 + 2] = straight( source[x * 4 + 0] );
                    destination[x * 4 + 3] = alpha;
                }
            }
            device_context_->Unmap( staging.Get( ), 0 );

            const HRESULT com = ::CoInitializeEx( nullptr, COINIT_MULTITHREADED );
            bool saved = false;
            {
                Microsoft::WRL::ComPtr< IWICImagingFactory > factory;
                Microsoft::WRL::ComPtr< IWICStream > stream;
                Microsoft::WRL::ComPtr< IWICBitmapEncoder > encoder;
                Microsoft::WRL::ComPtr< IWICBitmapFrameEncode > frame;
                WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
                saved = SUCCEEDED( ::CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( factory.GetAddressOf( ) ) ) ) &&
                        SUCCEEDED( factory->CreateStream( stream.GetAddressOf( ) ) ) &&
                        SUCCEEDED( stream->InitializeFromFilename( path.c_str( ), GENERIC_WRITE ) ) &&
                        SUCCEEDED( factory->CreateEncoder( GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf( ) ) ) &&
                        SUCCEEDED( encoder->Initialize( stream.Get( ), WICBitmapEncoderNoCache ) ) &&
                        SUCCEEDED( encoder->CreateNewFrame( frame.GetAddressOf( ), nullptr ) ) &&
                        SUCCEEDED( frame->Initialize( nullptr ) ) &&
                        SUCCEEDED( frame->SetSize( description.Width, description.Height ) ) &&
                        SUCCEEDED( frame->SetPixelFormat( &format ) ) &&
                        SUCCEEDED( frame->WritePixels( description.Height, stride, static_cast< UINT >( pixels.size( ) ), pixels.data( ) ) ) &&
                        SUCCEEDED( frame->Commit( ) ) &&
                        SUCCEEDED( encoder->Commit( ) );
            }
            if ( SUCCEEDED( com ) )
                ::CoUninitialize( );
            return saved;
        }
#endif

        void resize( UINT width, UINT height ) noexcept
        {
            if ( !swap_chain_ || width == 0 || height == 0 )
                return;

            device_context_->OMSetRenderTargets( 0, nullptr, nullptr );
            render_target_.Reset( );
            if ( FAILED( swap_chain_->ResizeBuffers( 0, width, height, DXGI_FORMAT_UNKNOWN, 0 ) ) || !create_render_target( ) )
                ::PostQuitMessage( EXIT_FAILURE );
        }

        static LRESULT CALLBACK window_proc( HWND window, UINT message, WPARAM w_param, LPARAM l_param ) noexcept
        {
            auto* app = reinterpret_cast< app_window* >( ::GetWindowLongPtrW( window, GWLP_USERDATA ) );
            if ( message == WM_NCCREATE )
            {
                const auto* create = reinterpret_cast< const CREATESTRUCTW* >( l_param );
                app = static_cast< app_window* >( create->lpCreateParams );
                ::SetWindowLongPtrW( window, GWLP_USERDATA, reinterpret_cast< LONG_PTR >( app ) );
                if ( app )
                    app->window_ = window;
            }

            if ( ImGui::GetCurrentContext( ) && ImGui_ImplWin32_WndProcHandler( window, message, w_param, l_param ) )
                return 1;

            if ( !app )
                return ::DefWindowProcW( window, message, w_param, l_param );

            switch ( message )
            {
            case WM_SIZE:
                if ( w_param != SIZE_MINIMIZED && app->device_ )
                    app->resize( LOWORD( l_param ), HIWORD( l_param ) );
                return 0;
            case WM_WINDOWPOSCHANGED:
                app->backdrop_.follow( window );
                break;
            case WM_SYSCOMMAND:
                if ( ( w_param & 0xFFF0 ) == SC_KEYMENU )
                    return 0;
                break;
            case WM_DESTROY:
                ::PostQuitMessage( EXIT_SUCCESS );
                return 0;
            default:
                break;
            }
            return ::DefWindowProcW( window, message, w_param, l_param );
        }

        HINSTANCE instance_ = nullptr;
        HWND window_ = nullptr;
        std::wstring class_name_;
        Microsoft::WRL::ComPtr< ID3D11Device > device_;
        Microsoft::WRL::ComPtr< ID3D11DeviceContext > device_context_;
        Microsoft::WRL::ComPtr< IDXGISwapChain > swap_chain_;
        Microsoft::WRL::ComPtr< ID3D11RenderTargetView > render_target_;
        Microsoft::WRL::ComPtr< ID3D11Query > frame_done_;
        backdrop_windows backdrop_;
        bool paced_ = true;
        int quick_flushes_ = 0;
#if LAZYUI_ENABLE_TEST_HARNESS
        static constexpr int capture_frame_count = 12;
        std::wstring capture_path_;
        int captured_frames_ = 0;
        std::vector< script_step > script_;
        std::size_t script_step_ = 0;
        std::wstring pending_shot_;
        bool scripted_ = false;
        bool script_failed_ = false;
#endif

        bool class_registered_ = false;
        bool imgui_context_created_ = false;
        bool win32_backend_initialized_ = false;
        bool dx11_backend_initialized_ = false;
        bool initialized_ = false;
    };
}
