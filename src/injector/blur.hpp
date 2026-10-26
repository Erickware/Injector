#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#if __has_include( "injector_config.hpp" )
#include "injector_config.hpp"
#endif

#include <Windows.h>
#include <imgui.h>

#include <cmath>

#ifdef INJECTOR_NO_D3D11
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;

class c_blureffect final
{
public:
    [[nodiscard]] bool setup( ID3D11Device*, ID3D11DeviceContext*, IDXGISwapChain* ) { return false; }
    void release( ) noexcept { }
    [[nodiscard]] bool ready( ) const noexcept { return false; }
    [[nodiscard]] bool matches_display( const ImVec2& ) const noexcept { return false; }
    void draw( ImDrawList*, ImVec2, ImVec2, float, int = 0 ) { }
    void draw( ImDrawList*, ImVec2, ImVec2, float, float, float, float ) { }
};
#else
#include <d3d11.h>

struct render_texture
{
    ~render_texture( ) { release( ); }

    render_texture( ) = default;
    render_texture( const render_texture& ) = delete;
    render_texture& operator=( const render_texture& ) = delete;
    render_texture( render_texture&& ) = delete;
    render_texture& operator=( render_texture&& ) = delete;

    [[nodiscard]] bool init( ID3D11Device* device, ID3D11DeviceContext* context, float width = -1.f, float height = -1.f )
    {
        if ( ready( ) )
            return true;
        if ( !device || !context )
            return false;

        const ImVec2 display_size = ImGui::GetIO( ).DisplaySize;
        const float requested_width = width < 0.f ? display_size.x : width;
        const float requested_height = height < 0.f ? display_size.y : height;
        if ( requested_width < 1.f || requested_height < 1.f )
            return false;

        D3D11_TEXTURE2D_DESC description{ };
        description.Width = static_cast< UINT >( std::floor( requested_width ) );
        description.Height = static_cast< UINT >( std::floor( requested_height ) );
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        if ( FAILED( device->CreateTexture2D( &description, nullptr, &texture ) ) ||
             FAILED( device->CreateRenderTargetView( texture, nullptr, &rtv ) ) ||
             FAILED( device->CreateShaderResourceView( texture, nullptr, &srv ) ) )
        {
            release( );
            return false;
        }

        const float clear_color[4]{ 0.f, 0.f, 0.f, 0.f };
        context->ClearRenderTargetView( rtv, clear_color );
        size = ImVec2{ static_cast< float >( description.Width ), static_cast< float >( description.Height ) };
        return true;
    }

    void release( ) noexcept
    {
        if ( srv )
        {
            srv->Release( );
            srv = nullptr;
        }
        if ( rtv )
        {
            rtv->Release( );
            rtv = nullptr;
        }
        if ( texture )
        {
            texture->Release( );
            texture = nullptr;
        }
        size = ImVec2{ };
    }

    [[nodiscard]] bool ready( ) const noexcept { return texture && rtv && srv; }

    ID3D11Texture2D* texture = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ImVec2 size{ };
};

class c_blureffect final
{
public:
    c_blureffect( ) = default;
    ~c_blureffect( ) { release( ); }

    c_blureffect( const c_blureffect& ) = delete;
    c_blureffect& operator=( const c_blureffect& ) = delete;
    c_blureffect( c_blureffect&& ) = delete;
    c_blureffect& operator=( c_blureffect&& ) = delete;

    [[nodiscard]] bool setup( ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swap_chain );
    void release( ) noexcept;
    [[nodiscard]] bool ready( ) const noexcept;
    [[nodiscard]] bool matches_display( const ImVec2& display_size ) const noexcept;
    void draw( ImDrawList* draw, ImVec2 start, ImVec2 end, float rounding, int flags = 0 );
    void draw( ImDrawList* draw, ImVec2 start, ImVec2 end, float top_left, float top_right, float bottom_right, float bottom_left );

private:
    struct pass_data
    {
        c_blureffect* instance = nullptr;
        ID3D11RenderTargetView* render_target = nullptr;
        ImVec2 start{ };
        ImVec2 end{ };
    };

    struct texture_pair
    {
        render_texture horizontal;
        render_texture vertical;
    };

    static constexpr int pair_count = 3;

    static void begin( const ImDrawList*, const ImDrawCmd* command );
    static void first_pass( const ImDrawList*, const ImDrawCmd* command );
    static void second_pass( const ImDrawList*, const ImDrawCmd* command );
    static void end( const ImDrawList*, const ImDrawCmd* command );

    void begin_frame( );
    void apply_pass( ID3D11RenderTargetView* render_target, ID3D11PixelShader* shader, ImVec2 start, ImVec2 end );
    void end_frame( );
    [[nodiscard]] bool enqueue( ImDrawList* draw, ImVec2 start, ImVec2 end );

    ID3D11RenderTargetView* render_target_backup_ = nullptr;
    ID3D11PixelShader* pixel_shader_backup_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swap_chain_ = nullptr;
    ImVec2 display_size_{ };

    render_texture horizontal_texture_;
    render_texture vertical_texture_;
    texture_pair pairs_[pair_count];

    ID3D11PixelShader* horizontal_shader_ = nullptr;
    ID3D11PixelShader* vertical_shader_ = nullptr;
    ID3D11Texture2D* copied_texture_ = nullptr;
    ID3D11ShaderResourceView* copied_view_ = nullptr;
    ID3D11Buffer* bounds_buffer_ = nullptr;
};
#endif
