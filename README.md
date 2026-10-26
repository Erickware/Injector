# Injector

![Injector](preview.png)

Injector is a Windows desktop application built with Dear ImGui and DirectX 11.

## Build and run

You need Windows 10 or 11 and Visual Studio 2022 (or newer) with the **Desktop development with C++** workload.

1. Open `Injector.sln`.
2. Pick **Release** and **x64** in the toolbar.
3. Build with **Ctrl+Shift+B**, then run with **F5**. The app is `build/Release/Injector.exe`.

Or with CMake 3.20 or newer, from this folder:

```text
cmake -S . -B build
cmake --build build --config Release
```

## What's where

```text
Injector.sln          open this in Visual Studio
Injector.vcxproj
CMakeLists.txt        the same project for CMake
src/
  main.cpp            the window and the frame loop; your own start-up code goes here
  ui/                 the design, as code
    ui.hpp            ui::vars, every setting the UI shows, and render( )
    ui.cpp            the window: its background, navigation, and pages
    pages.cpp         every page: child sections and the widgets in them
    widgets.cpp       the design's own widgets: checkbox( ), slider( ), ...
    theme.hpp         the design's colors and size
    fonts.hpp/.cpp    the design's fonts and icons, and where its texts' letters go
    assets.hpp/.cpp   the design's pictures, embedded
  injector/           shared rendering, window, and widget support
    injector.hpp      the app context (window, device, swap chain)
    window.hpp        the Win32 + DirectX 11 window
    runtime.hpp/.cpp  shapes, text, layout, widget state, motion, and shader backgrounds
    blur.hpp/.cpp     backdrop blur
    backdrop.hpp      frosted desktop under the window's own glass
    image.hpp/.cpp    pictures as GPU textures
vendor/imgui/         Dear ImGui
assets/               readable copies of the pictures, fonts, and icons the code embeds
build/                made by the first build (build/Release/Injector.exe)
```

## Read and change the settings

Application preferences are named fields in `ui::prefs` (`src/ui/ui.hpp`).
The remaining controls and the current process selection are stored in `ui::vars`.
Process names, PIDs and architecture come from Windows; log entries come from actual operations.

```cpp
#include "ui/ui.hpp"

ui::prefs.enable_animations = true;
ui::prefs.verbose_logging = true;
ui::vars.injection_delay = 150.f;
const unsigned int selected_pid = ui::vars.target_pid;
```

## Test tools

`test/injtest.c` is a test DLL that displays an `INJTEST-MARKER` window when executed.
It is not linked into the application and does not populate its process list or logs.
PowerShell test scripts resolve paths relative to their own directory.

Scripted UI input and frame capture are excluded from normal builds. To build a dedicated
test executable with these tools enabled, pass `/p:EnableTestHarness=true` to MSBuild,
or `-DINJECTOR_ENABLE_TEST_HARNESS=ON` to CMake. Test builds accept
`INJECTOR_CAPTURE` and `INJECTOR_CAPTURE_SCRIPT`; normal builds ignore these variables.

## Changing the design

Edit page layouts and controls in `src/ui`. Shared window and rendering helpers live in `src/injector`; application startup lives in `src/main.cpp`.

## Third-party notices

Component ownership and license notices are recorded in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
