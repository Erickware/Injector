#include "blur.hpp"

#ifndef INJECTOR_NO_D3D11

#include "compat.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

#pragma comment( lib, "d3dcompiler.lib" )

namespace
{
    struct blur_bounds
    {
        ImVec2 minimum{ };
        ImVec2 maximum{ };
    };

    constexpr std::string_view horizontal_shader_source = R"(
cbuffer BlurBounds : register(b0)
{
    float2 minimum_uv;
    float2 maximum_uv;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

sampler sampler0;
Texture2D texture0;

static const float offsets[6] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };
static const float weights[6] = { 0.227f, 0.1662f, 0.0943f, 0.0689f, 0.0476f, 0.0239f };

float4 main(PS_INPUT input) : SV_Target
{
    if (input.uv.x < minimum_uv.x || input.uv.x > maximum_uv.x ||
        input.uv.y < minimum_uv.y || input.uv.y > maximum_uv.y)
        return texture0.SampleLevel(sampler0, input.uv, 0);

    uint width;
    uint height;
    texture0.GetDimensions(width, height);
    const float step_size = 1.0f / max((float)width, 1.0f);
    float4 result = texture0.SampleLevel(sampler0, input.uv, 0);
    result.rgb *= weights[0];
    for (int index = 1; index < 6; ++index)
    {
        result.rgb += texture0.SampleLevel(sampler0, input.uv - float2(step_size * offsets[index], 0.0f), 0).rgb * weights[index];
        result.rgb += texture0.SampleLevel(sampler0, input.uv + float2(step_size * offsets[index], 0.0f), 0).rgb * weights[index];
    }
    result.rgb *= 0.98f;
    return result;
}
)";

    constexpr std::string_view vertical_shader_source = R"(
cbuffer BlurBounds : register(b0)
{
    float2 minimum_uv;
    float2 maximum_uv;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

sampler sampler0;
Texture2D texture0;

static const float offsets[6] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };
static const float weights[6] = { 0.227f, 0.1662f, 0.0943f, 0.0689f, 0.0476f, 0.0239f };

float4 main(PS_INPUT input) : SV_Target
{
    if (input.uv.x < minimum_uv.x || input.uv.x > maximum_uv.x ||
        input.uv.y < minimum_uv.y || input.uv.y > maximum_uv.y)
        return texture0.SampleLevel(sampler0, input.uv, 0);

    uint width;
    uint height;
    texture0.GetDimensions(width, height);
    const float step_size = 1.0f / max((float)height, 1.0f);
    float4 result = texture0.SampleLevel(sampler0, input.uv, 0);
    result.rgb *= weights[0];
    for (int index = 1; index < 6; ++index)
    {
        result.rgb += texture0.SampleLevel(sampler0, input.uv - float2(0.0f, step_size * offsets[index]), 0).rgb * weights[index];
        result.rgb += texture0.SampleLevel(sampler0, input.uv + float2(0.0f, step_size * offsets[index]), 0).rgb * weights[index];
    }
    result.rgb *= 0.98f;
    return result;
}
)";

    template < typename T >
    void release_com( T*& object ) noexcept
    {
        if ( object )
        {
            object->Release( );
            object = nullptr;
        }
    }

#if IMGUI_VERSION_NUM >= 19200
    ImTextureRef texture_ref( ID3D11ShaderResourceView* view ) noexcept
    {
        return ImTextureRef{ injector::texture_id( view ) };
    }
#else
    ImTextureID texture_ref( ID3D11ShaderResourceView* view ) noexcept
    {
        return injector::texture_id( view );
    }
#endif

    ImVec2 normalized( ImVec2 value, ImVec2 dimensions ) noexcept
    {
        return ImVec2{ value.x / dimensions.x, value.y / dimensions.y };
    }

    bool create_shader( ID3D11Device* device, std::string_view source, ID3D11PixelShader** shader )
    {
        if ( !device || !shader )
            return false;

        ID3DBlob* bytecode = nullptr;
        ID3DBlob* errors = nullptr;
        const HRESULT compile_result = ::D3DCompile(
            source.data( ), source.size( ), nullptr, nullptr, nullptr,
            "main", "ps_5_0", 0, 0, &bytecode, &errors );
        if ( FAILED( compile_result ) )
        {
            if ( errors )
                ::OutputDebugStringA( static_cast< const char* >( errors->GetBufferPointer( ) ) );
            release_com( errors );
            release_com( bytecode );
            return false;
        }

        const HRESULT create_result = device->CreatePixelShader(
            bytecode->GetBufferPointer( ), bytecode->GetBufferSize( ), nullptr, shader );
        release_com( errors );
        release_com( bytecode );
        return SUCCEEDED( create_result );
    }
}

bool c_blureffect::setup( ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swap_chain )
{
    release( );
    if ( !device || !context || !swap_chain )
        return false;

    display_size_ = ImGui::GetIO( ).DisplaySize;
    if ( display_size_.x < 1.f || display_size_.y < 1.f )
        return false;

    device_ = device;
    context_ = context;
    swap_chain_ = swap_chain;

    const float width = ( std::max )( std::floor( display_size_.x / 6.f ), 1.f );
    const float height = ( std::max )( std::floor( display_size_.y / 6.f ), 1.f );
    bool textures_created = horizontal_texture_.init( device_, context_, width, height );
    textures_created = vertical_texture_.init( device_, context_, width, height ) && textures_created;
    for ( texture_pair& pair : pairs_ )
    {
        textures_created = pair.horizontal.init( device_, context_, width, height ) && textures_created;
        textures_created = pair.vertical.init( device_, context_, width, height ) && textures_created;
    }

    ID3D11Texture2D* back_buffer = nullptr;
    if ( SUCCEEDED( swap_chain_->GetBuffer( 0, IID_PPV_ARGS( &back_buffer ) ) ) )
    {
        D3D11_TEXTURE2D_DESC description{ };
        back_buffer->GetDesc( &description );
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.CPUAccessFlags = 0;
        description.MiscFlags = 0;
        if ( SUCCEEDED( device_->CreateTexture2D( &description, nullptr, &copied_texture_ ) ) )
            device_->CreateShaderResourceView( copied_texture_, nullptr, &copied_view_ );
        back_buffer->Release( );
    }

    D3D11_BUFFER_DESC buffer_description{ };
    buffer_description.ByteWidth = sizeof( blur_bounds );
    buffer_description.Usage = D3D11_USAGE_DYNAMIC;
    buffer_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const bool buffer_created = SUCCEEDED( device_->CreateBuffer( &buffer_description, nullptr, &bounds_buffer_ ) );
    const bool shaders_created = create_shader( device_, horizontal_shader_source, &horizontal_shader_ ) &&
                                 create_shader( device_, vertical_shader_source, &vertical_shader_ );

    if ( !textures_created || !buffer_created || !shaders_created || !ready( ) )
    {
        release( );
        return false;
    }
    return true;
}

void c_blureffect::release( ) noexcept
{
    release_com( render_target_backup_ );
    release_com( pixel_shader_backup_ );
    release_com( copied_view_ );
    release_com( copied_texture_ );
    release_com( horizontal_shader_ );
    release_com( vertical_shader_ );
    release_com( bounds_buffer_ );
    horizontal_texture_.release( );
    vertical_texture_.release( );
    for ( texture_pair& pair : pairs_ )
    {
        pair.horizontal.release( );
        pair.vertical.release( );
    }
    display_size_ = ImVec2{ };
    swap_chain_ = nullptr;
    context_ = nullptr;
    device_ = nullptr;
}

bool c_blureffect::ready( ) const noexcept
{
    if ( !device_ || !context_ || !swap_chain_ || !copied_texture_ || !copied_view_ ||
         !horizontal_shader_ || !vertical_shader_ || !bounds_buffer_ ||
         !horizontal_texture_.ready( ) || !vertical_texture_.ready( ) )
        return false;
    for ( const texture_pair& pair : pairs_ )
        if ( !pair.horizontal.ready( ) || !pair.vertical.ready( ) )
            return false;
    return true;
}

bool c_blureffect::matches_display( const ImVec2& display_size ) const noexcept
{
    return std::abs( display_size_.x - display_size.x ) < 0.5f &&
           std::abs( display_size_.y - display_size.y ) < 0.5f;
}

void c_blureffect::begin_frame( )
{
    if ( !ready( ) )
        return;

    context_->OMGetRenderTargets( 1, &render_target_backup_, nullptr );
    context_->PSGetShader( &pixel_shader_backup_, nullptr, nullptr );

    ID3D11Texture2D* back_buffer = nullptr;
    if ( SUCCEEDED( swap_chain_->GetBuffer( 0, IID_PPV_ARGS( &back_buffer ) ) ) )
    {
        context_->CopyResource( copied_texture_, back_buffer );
        back_buffer->Release( );
    }
}

void c_blureffect::apply_pass( ID3D11RenderTargetView* render_target, ID3D11PixelShader* shader, ImVec2 start, ImVec2 end )
{
    if ( !render_target || !shader || !bounds_buffer_ )
        return;

    context_->OMSetRenderTargets( 1, &render_target, nullptr );
    const float clear_color[4]{ 0.f, 0.f, 0.f, 0.f };
    context_->ClearRenderTargetView( render_target, clear_color );

    const blur_bounds bounds{ normalized( start, display_size_ ), normalized( end, display_size_ ) };
    D3D11_MAPPED_SUBRESOURCE mapped{ };
    if ( FAILED( context_->Map( bounds_buffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
        return;
    std::memcpy( mapped.pData, &bounds, sizeof( bounds ) );
    context_->Unmap( bounds_buffer_, 0 );
    context_->PSSetConstantBuffers( 0, 1, &bounds_buffer_ );
    context_->PSSetShader( shader, nullptr, 0 );
}

void c_blureffect::end_frame( )
{
    context_->OMSetRenderTargets( 1, &render_target_backup_, nullptr );
    context_->PSSetShader( pixel_shader_backup_, nullptr, 0 );
    release_com( render_target_backup_ );
    release_com( pixel_shader_backup_ );
}

void c_blureffect::begin( const ImDrawList*, const ImDrawCmd* command )
{
    static_cast< c_blureffect* >( command->UserCallbackData )->begin_frame( );
}

void c_blureffect::first_pass( const ImDrawList*, const ImDrawCmd* command )
{
    const auto* data = static_cast< const pass_data* >( command->UserCallbackData );
    data->instance->apply_pass( data->render_target, data->instance->horizontal_shader_, data->start, data->end );
}

void c_blureffect::second_pass( const ImDrawList*, const ImDrawCmd* command )
{
    const auto* data = static_cast< const pass_data* >( command->UserCallbackData );
    data->instance->apply_pass( data->render_target, data->instance->vertical_shader_, data->start, data->end );
}

void c_blureffect::end( const ImDrawList*, const ImDrawCmd* command )
{
    static_cast< c_blureffect* >( command->UserCallbackData )->end_frame( );
}

bool c_blureffect::enqueue( ImDrawList* draw, ImVec2 start, ImVec2 end_position )
{
    if ( !draw || !ready( ) )
        return false;

    const auto add_pass = [draw, start, end_position, this]( ImDrawCallback callback, ID3D11RenderTargetView* target )
    {
        pass_data data{ this, target, start, end_position };
        injector::add_callback( draw, callback, &data, sizeof( data ) );
    };

    draw->PushClipRectFullScreen( );
    draw->AddCallback( begin, this );
    add_pass( first_pass, horizontal_texture_.rtv );
    draw->AddImage( texture_ref( copied_view_ ), ImVec2{ }, horizontal_texture_.size );
    add_pass( second_pass, vertical_texture_.rtv );
    draw->AddImage( texture_ref( horizontal_texture_.srv ), ImVec2{ }, vertical_texture_.size );

    for ( int index = 0; index < pair_count; ++index )
    {
        ID3D11ShaderResourceView* source = index == 0 ? vertical_texture_.srv : pairs_[index - 1].vertical.srv;
        add_pass( first_pass, pairs_[index].horizontal.rtv );
        draw->AddImage( texture_ref( source ), ImVec2{ }, pairs_[index].horizontal.size );
        add_pass( second_pass, pairs_[index].vertical.rtv );
        draw->AddImage( texture_ref( pairs_[index].horizontal.srv ), ImVec2{ }, pairs_[index].vertical.size );
    }

    draw->AddCallback( end, this );
    draw->AddCallback( ImDrawCallback_ResetRenderState, nullptr );
    draw->PopClipRect( );
    return true;
}

void c_blureffect::draw( ImDrawList* draw, ImVec2 start, ImVec2 end_position, float rounding, int flags )
{
    if ( !enqueue( draw, start, end_position ) )
        return;
    draw->AddImageRounded(
        texture_ref( pairs_[pair_count - 1].vertical.srv ),
        start,
        end_position,
        normalized( start, display_size_ ),
        normalized( end_position, display_size_ ),
        ImColor{ 1.f, 1.f, 1.f, ImGui::GetStyle( ).Alpha },
        rounding,
        flags );
}

void c_blureffect::draw( ImDrawList* draw, ImVec2 start, ImVec2 end_position, float top_left, float top_right, float bottom_right, float bottom_left )
{
    if ( !enqueue( draw, start, end_position ) )
        return;

    const float width = std::max( 0.f, end_position.x - start.x );
    const float height = std::max( 0.f, end_position.y - start.y );
    top_left = std::max( 0.f, top_left );
    top_right = std::max( 0.f, top_right );
    bottom_right = std::max( 0.f, bottom_right );
    bottom_left = std::max( 0.f, bottom_left );
    float scale = 1.f;
    if ( top_left + top_right > width ) scale = std::min( scale, width / ( top_left + top_right ) );
    if ( bottom_left + bottom_right > width ) scale = std::min( scale, width / ( bottom_left + bottom_right ) );
    if ( top_left + bottom_left > height ) scale = std::min( scale, height / ( top_left + bottom_left ) );
    if ( top_right + bottom_right > height ) scale = std::min( scale, height / ( top_right + bottom_right ) );
    top_left *= scale;
    top_right *= scale;
    bottom_right *= scale;
    bottom_left *= scale;

    const ImVec2 center{ ( start.x + end_position.x ) * 0.5f, ( start.y + end_position.y ) * 0.5f };
    const ImVec2 uv_min = normalized( start, display_size_ );
    const ImVec2 uv_max = normalized( end_position, display_size_ );
    const auto texture = texture_ref( pairs_[pair_count - 1].vertical.srv );
    const auto draw_corner = [&]( ImVec2 clip_min, ImVec2 clip_max, float rounding, ImDrawFlags flags )
    {
        draw->PushClipRect( clip_min, clip_max, true );
        if ( rounding > 0.f )
            draw->AddImageRounded( texture, start, end_position, uv_min, uv_max, ImColor{ 1.f, 1.f, 1.f, ImGui::GetStyle( ).Alpha }, rounding, flags );
        else
            draw->AddImage( texture, start, end_position, uv_min, uv_max, ImColor{ 1.f, 1.f, 1.f, ImGui::GetStyle( ).Alpha } );
        draw->PopClipRect( );
    };
    draw_corner( start, center, top_left, ImDrawFlags_RoundCornersTopLeft );
    draw_corner( ImVec2{ center.x, start.y }, ImVec2{ end_position.x, center.y }, top_right, ImDrawFlags_RoundCornersTopRight );
    draw_corner( center, end_position, bottom_right, ImDrawFlags_RoundCornersBottomRight );
    draw_corner( ImVec2{ start.x, center.y }, ImVec2{ center.x, end_position.y }, bottom_left, ImDrawFlags_RoundCornersBottomLeft );
}

#endif
