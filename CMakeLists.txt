# cmake -S . -B build
# cmake --build build --config Release

cmake_minimum_required( VERSION 3.20 )
project( LazyUI LANGUAGES CXX )

if( NOT WIN32 )
    message( FATAL_ERROR "LazyUI apps are Windows programs (Win32 and DirectX 11)." )
endif( )

set( CMAKE_CXX_STANDARD 20 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" )

add_executable( LazyUI WIN32
    src/main.cpp
    src/process.cpp
    src/lazyui/runtime.cpp
    src/lazyui/blur.cpp
    src/lazyui/image.cpp
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/imgui_impl_dx11.cpp
    vendor/imgui/imgui_impl_win32.cpp
    src/ui/ui.cpp
    src/ui/pages.cpp
    src/ui/widgets.cpp
    src/ui/assets.cpp
    src/ui/fonts.cpp
)

target_include_directories( LazyUI PRIVATE src vendor/imgui )
target_compile_definitions( LazyUI PRIVATE _WINDOWS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE )
if( MSVC )
    target_compile_options( LazyUI PRIVATE /W3 /sdl /permissive- /MP )
endif( )
target_link_libraries( LazyUI PRIVATE d3d11 d3dcompiler dxgi dwmapi windowscodecs )
