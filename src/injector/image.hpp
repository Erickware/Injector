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

#ifndef INJECTOR_NO_D3D11
#include <d3d11.h>
#else
struct ID3D11Device;
#endif
#include <imgui.h>

#include <cstddef>
#include <cstdint>

#include "compat.hpp"

struct ImTextureData;

class c_image final
{
public:
    c_image( ) = default;
    ~c_image( ) { release( ); }

    c_image( const c_image& ) = delete;
    c_image& operator=( const c_image& ) = delete;
    c_image( c_image&& ) = delete;
    c_image& operator=( c_image&& ) = delete;

    [[nodiscard]] bool load( ID3D11Device* device, const void* data, std::size_t size );
    void release( ) noexcept;

    [[nodiscard]] bool ready( ) const noexcept { return kind_ != kind_none; }
    [[nodiscard]] ImVec2 size( ) const noexcept { return size_; }

#if IMGUI_VERSION_NUM >= 19200
    operator ImTextureRef( ) const noexcept;
#else
    operator ImTextureID( ) const noexcept;
#endif

private:
    enum texture_kind : int { kind_none, kind_dx11, kind_imgui, kind_dx9, kind_opengl };

    std::uintptr_t texture_ = 0;
    ImTextureData* data_ = nullptr;
    texture_kind kind_ = kind_none;
    bool undecodable_ = false;
    ImVec2 size_{ };
};
