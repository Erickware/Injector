# cmake -S . -B build
# cmake --build build --config Release

cmake_minimum_required( VERSION 3.20 )
project( Injector LANGUAGES CXX )

option( INJECTOR_ENABLE_TEST_HARNESS "Enable scripted UI input and frame capture for tests" OFF )

if( NOT WIN32 )
    message( FATAL_ERROR "Injector apps are Windows programs (Win32 and DirectX 11)." )
endif( )

set( CMAKE_CXX_STANDARD 20 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" )

add_library( InjectorCore STATIC
    src/process.cpp
    src/inject.cpp
    src/core/diagnostics.cpp
    src/core/loaders.cpp
    src/core/modules.cpp
    src/core/pe_image.cpp
    src/core/profile.cpp
)
target_include_directories( InjectorCore PUBLIC src )
target_compile_definitions( InjectorCore PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE )
target_link_libraries( InjectorCore PUBLIC advapi32 shell32 ole32 user32 )
if( MSVC )
    target_compile_options( InjectorCore PRIVATE /W4 /sdl /permissive- /utf-8 /MP )
endif( )
if( NOT CMAKE_SIZEOF_VOID_P EQUAL 8 )
    message( FATAL_ERROR "Loading backends require an x64 build; x86 DLLs can be inspected only." )
endif( )

add_executable( Injector WIN32
    src/main.cpp
    src/injector/runtime.cpp
    src/injector/blur.cpp
    src/injector/image.cpp
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/imgui_impl_dx11.cpp
    vendor/imgui/imgui_impl_win32.cpp
    src/ui/settings.cpp
    src/ui/file_dialog.cpp
    src/ui/ui.cpp
    src/ui/pages.cpp
    src/ui/widgets.cpp
    src/ui/assets.cpp
    src/ui/fonts.cpp
)

target_include_directories( Injector PRIVATE src vendor/imgui )
target_compile_definitions( Injector PRIVATE _WINDOWS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE )
if( INJECTOR_ENABLE_TEST_HARNESS )
    target_compile_definitions( Injector PRIVATE INJECTOR_ENABLE_TEST_HARNESS=1 )
endif( )
if( MSVC )
    target_compile_options( Injector PRIVATE /W3 /sdl /permissive- /utf-8 /MP )
endif( )
target_link_libraries( Injector PRIVATE InjectorCore d3d11 d3dcompiler dxgi dwmapi windowscodecs comdlg32 )

option( INJECTOR_BUILD_TESTS "Build owned test processes, DLLs and domain regression tests" OFF )
if( INJECTOR_BUILD_TESTS )
    enable_testing( )
    add_subdirectory( tests/fixtures )
    add_subdirectory( tests/fixtures/dlls )
    add_executable( InjectorCoreTests tests/core_tests.cpp )
    target_link_libraries( InjectorCoreTests PRIVATE InjectorCore )
    target_compile_options( InjectorCoreTests PRIVATE /utf-8 )
    add_dependencies( InjectorCoreTests InjectorTestTarget InjectorFixtureGood InjectorFixtureSlow InjectorFixtureReject )
    add_test( NAME InjectorCoreTests COMMAND InjectorCoreTests $<TARGET_FILE:InjectorTestTarget>
        $<TARGET_FILE:InjectorFixtureGood> $<TARGET_FILE:InjectorFixtureSlow> $<TARGET_FILE:InjectorFixtureReject> )
    set_tests_properties( InjectorCoreTests PROPERTIES TIMEOUT 60 )
endif( )
