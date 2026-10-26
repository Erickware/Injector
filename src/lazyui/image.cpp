#include "image.hpp"

#include "lazyui.hpp"

#include <wincodec.h>
#include <wrl/client.h>

#include <imgui_internal.h>

#if defined( LAZYUI_RENDERER_DX9 )
#include <d3d9.h>
#endif
#if defined( LAZYUI_RENDERER_OPENGL )
#include <GL/gl.h>
#pragma comment( lib, "opengl32.lib" )
#endif

#include <vector>

#pragma comment( lib, "windowscodecs.lib" )

using Microsoft::WRL::ComPtr;

namespace
{
    [[nodiscard]] bool decode( const void* data, std::size_t size, std::vector< std::uint8_t >& pixels, UINT& width, UINT& height )
    {
        const HRESULT com = ::CoInitializeEx( nullptr, COINIT_MULTITHREADED );
        bool decoded = false;
        {
            ComPtr< IWICImagingFactory > factory;
            ComPtr< IWICStream > stream;
            ComPtr< IWICBitmapDecoder > decoder;
            ComPtr< IWICBitmapFrameDecode > frame;
            ComPtr< IWICFormatConverter > converter;
            decoded = SUCCEEDED( ::CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( factory.GetAddressOf( ) ) ) ) &&
                      SUCCEEDED( factory->CreateStream( stream.GetAddressOf( ) ) ) &&
                      SUCCEEDED( stream->InitializeFromMemory( static_cast< BYTE* >( const_cast< void* >( data ) ), static_cast< DWORD >( size ) ) ) &&
                      SUCCEEDED( factory->CreateDecoderFromStream( stream.Get( ), nullptr, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf( ) ) ) &&
                      SUCCEEDED( decoder->GetFrame( 0, frame.GetAddressOf( ) ) ) &&
                      SUCCEEDED( factory->CreateFormatConverter( converter.GetAddressOf( ) ) ) &&
                      SUCCEEDED( converter->Initialize( frame.Get( ), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom ) ) &&
                      SUCCEEDED( converter->GetSize( &width, &height ) ) && width > 0 && height > 0;
            if ( decoded )
            {
                pixels.resize( static_cast< std::size_t >( width ) * height * 4 );
                decoded = SUCCEEDED( converter->CopyPixels( nullptr, width * 4, static_cast< UINT >( pixels.size( ) ), pixels.data( ) ) );
            }
        }
        if ( SUCCEEDED( com ) )
            ::CoUninitialize( );
        return decoded;
    }

#ifndef LAZYUI_NO_D3D11
    [[nodiscard]] ID3D11ShaderResourceView* upload_dx11( ID3D11Device* device, const std::vector< std::uint8_t >& pixels, UINT width, UINT height )
    {
        D3D11_TEXTURE2D_DESC description{ };
        description.Width = width;
        description.Height = height;
        description.MipLevels = 0;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        description.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        ComPtr< ID3D11Texture2D > texture;
        ComPtr< ID3D11ShaderResourceView > view;
        ComPtr< ID3D11DeviceContext > context;
        if ( FAILED( device->CreateTexture2D( &description, nullptr, texture.GetAddressOf( ) ) ) ||
             FAILED( device->CreateShaderResourceView( texture.Get( ), nullptr, view.GetAddressOf( ) ) ) )
            return nullptr;
        device->GetImmediateContext( context.GetAddressOf( ) );
        context->UpdateSubresource( texture.Get( ), 0, nullptr, pixels.data( ), width * 4, 0 );
        context->GenerateMips( view.Get( ) );
        return view.Detach( );
    }
#endif

#if IMGUI_VERSION_NUM >= 19200 && defined( IMGUI_HAS_TEXTURES )
    [[nodiscard]] bool imgui_textures( )
    {
        return ImGui::GetCurrentContext( ) && ( ImGui::GetIO( ).BackendFlags & ImGuiBackendFlags_RendererHasTextures ) != 0;
    }

    [[nodiscard]] ImTextureData* upload_imgui( const std::vector< std::uint8_t >& pixels, UINT width, UINT height )
    {
        ImTextureData* texture = IM_NEW( ImTextureData )( );
        texture->Create( ImTextureFormat_RGBA32, static_cast< int >( width ), static_cast< int >( height ) );
        std::memcpy( texture->GetPixels( ), pixels.data( ), pixels.size( ) );
        texture->UseColors = true;
        texture->UsedRect.x = texture->UsedRect.y = 0;
        texture->UsedRect.w = static_cast< unsigned short >( width );
        texture->UsedRect.h = static_cast< unsigned short >( height );
        ImGui::RegisterUserTexture( texture );
        return texture;
    }
#endif

#if defined( LAZYUI_RENDERER_DX9 )
    [[nodiscard]] IDirect3DTexture9* upload_dx9( IDirect3DDevice9* device, const std::vector< std::uint8_t >& pixels, UINT width, UINT height )
    {
        IDirect3DTexture9* texture = nullptr;
        if ( FAILED( device->CreateTexture( width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr ) ) &&
             FAILED( device->CreateTexture( width, height, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, nullptr ) ) )
            return nullptr;
        D3DLOCKED_RECT locked{ };
        if ( FAILED( texture->LockRect( 0, &locked, nullptr, 0 ) ) )
        {
            texture->Release( );
            return nullptr;
        }
        for ( UINT y = 0; y < height; ++y )
        {
            const std::uint8_t* source = pixels.data( ) + static_cast< std::size_t >( y ) * width * 4;
            std::uint8_t* target = static_cast< std::uint8_t* >( locked.pBits ) + static_cast< std::size_t >( y ) * locked.Pitch;
            for ( UINT x = 0; x < width; ++x )
            {
                target[x * 4 + 0] = source[x * 4 + 2];
                target[x * 4 + 1] = source[x * 4 + 1];
                target[x * 4 + 2] = source[x * 4 + 0];
                target[x * 4 + 3] = source[x * 4 + 3];
            }
        }
        texture->UnlockRect( 0 );
        return texture;
    }
#endif

#if defined( LAZYUI_RENDERER_OPENGL )
    [[nodiscard]] GLuint upload_opengl( const std::vector< std::uint8_t >& pixels, UINT width, UINT height )
    {
        GLint bound = 0;
        GLint row_length = 0;
        GLint alignment = 4;
        ::glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound );
        ::glGetIntegerv( GL_UNPACK_ROW_LENGTH, &row_length );
        ::glGetIntegerv( GL_UNPACK_ALIGNMENT, &alignment );
        GLuint name = 0;
        ::glGenTextures( 1, &name );
        if ( name == 0 )
            return 0;
        constexpr GLint clamp_to_edge = 0x812F;
        ::glBindTexture( GL_TEXTURE_2D, name );
        ::glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        ::glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        ::glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp_to_edge );
        ::glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp_to_edge );
        ::glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
        ::glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
        ::glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, static_cast< GLsizei >( width ), static_cast< GLsizei >( height ), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data( ) );
        ::glPixelStorei( GL_UNPACK_ROW_LENGTH, row_length );
        ::glPixelStorei( GL_UNPACK_ALIGNMENT, alignment );
        ::glBindTexture( GL_TEXTURE_2D, static_cast< GLuint >( bound ) );
        return name;
    }
#endif
}

bool c_image::load( ID3D11Device* device, const void* data, std::size_t size )
{
    if ( undecodable_ || !data || size == 0 )
        return false;

    texture_kind target = kind_none;
#ifndef LAZYUI_NO_D3D11
    if ( device )
        target = kind_dx11;
#else
    ( void )device;
#endif
#if IMGUI_VERSION_NUM >= 19200 && defined( IMGUI_HAS_TEXTURES )
    if ( target == kind_none && imgui_textures( ) )
        target = kind_imgui;
#endif
#if defined( LAZYUI_RENDERER_DX9 )
    if ( target == kind_none && lazyui::app.device9 )
        target = kind_dx9;
#endif
#if defined( LAZYUI_RENDERER_OPENGL )
    if ( target == kind_none && ::wglGetCurrentContext( ) )
        target = kind_opengl;
#endif
    if ( target == kind_none )
        return false;

    std::vector< std::uint8_t > pixels;
    UINT width = 0;
    UINT height = 0;
    if ( !decode( data, size, pixels, width, height ) )
    {
        undecodable_ = true;
        return false;
    }

    std::uintptr_t texture = 0;
    ImTextureData* texture_data = nullptr;
    switch ( target )
    {
#ifndef LAZYUI_NO_D3D11
    case kind_dx11:
        texture = reinterpret_cast< std::uintptr_t >( upload_dx11( device, pixels, width, height ) );
        break;
#endif
#if IMGUI_VERSION_NUM >= 19200 && defined( IMGUI_HAS_TEXTURES )
    case kind_imgui:
        texture_data = upload_imgui( pixels, width, height );
        break;
#endif
#if defined( LAZYUI_RENDERER_DX9 )
    case kind_dx9:
        texture = reinterpret_cast< std::uintptr_t >( upload_dx9( lazyui::app.device9, pixels, width, height ) );
        break;
#endif
#if defined( LAZYUI_RENDERER_OPENGL )
    case kind_opengl:
        texture = static_cast< std::uintptr_t >( upload_opengl( pixels, width, height ) );
        break;
#endif
    default:
        break;
    }
    if ( texture == 0 && !texture_data )
        return false;

    release( );
    texture_ = texture;
    data_ = texture_data;
    kind_ = target;
    size_ = ImVec2{ static_cast< float >( width ), static_cast< float >( height ) };
    return true;
}

void c_image::release( ) noexcept
{
    switch ( kind_ )
    {
#ifndef LAZYUI_NO_D3D11
    case kind_dx11:
        reinterpret_cast< ID3D11ShaderResourceView* >( texture_ )->Release( );
        break;
#endif
#if IMGUI_VERSION_NUM >= 19200 && defined( IMGUI_HAS_TEXTURES )
    case kind_imgui:
        if ( ImGui::GetCurrentContext( ) )
            ImGui::UnregisterUserTexture( data_ );
        IM_DELETE( data_ );
        break;
#endif
#if defined( LAZYUI_RENDERER_DX9 )
    case kind_dx9:
        reinterpret_cast< IDirect3DTexture9* >( texture_ )->Release( );
        break;
#endif
#if defined( LAZYUI_RENDERER_OPENGL )
    case kind_opengl:
        if ( ::wglGetCurrentContext( ) )
        {
            const GLuint name = static_cast< GLuint >( texture_ );
            ::glDeleteTextures( 1, &name );
        }
        break;
#endif
    default:
        break;
    }
    texture_ = 0;
    data_ = nullptr;
    kind_ = kind_none;
    size_ = ImVec2{ };
}

#if IMGUI_VERSION_NUM >= 19200
c_image::operator ImTextureRef( ) const noexcept
{
#ifdef IMGUI_HAS_TEXTURES
    if ( data_ )
        return data_->GetTexRef( );
#endif
    return ImTextureRef{ lazyui::texture_id( reinterpret_cast< const void* >( texture_ ) ) };
}
#else
c_image::operator ImTextureID( ) const noexcept
{
    return lazyui::texture_id( reinterpret_cast< const void* >( texture_ ) );
}
#endif
