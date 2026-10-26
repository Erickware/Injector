#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#if __has_include( "lazyui_config.hpp" )
#include "lazyui_config.hpp"
#endif

#include <Windows.h>
#ifndef LAZYUI_NO_D3D11
#include <d3d11.h>
#include <dxgi.h>
#else
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
#endif

#include <imgui.h>
#include <imgui_internal.h>

#include "blur.hpp"
#include "image.hpp"

struct IDirect3DDevice9;

namespace lazyui
{
    struct app_context
    {
        HWND window = nullptr;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* device_context = nullptr;
        IDXGISwapChain* swap_chain = nullptr;
        IDirect3DDevice9* device9 = nullptr;
    };

    inline app_context app{ };
}
