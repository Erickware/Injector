#pragma once

// Real injection engine: classic LoadLibrary, thread hijack and manual map,
// with a live log the Injection Log panel renders. Nothing here is simulated:
// every entry is produced by an actual Win32 call while an injection runs.

#include <cstddef>
#include <vector>

namespace inject
{
    // Indexes inside method_items[3] on the Settings page.
    enum class method : int
    {
        load_library = 0,
        manual_map = 1,
        thread_hijack = 2
    };

    enum level : int
    {
        info = 0,
        ok = 1,
        warn = 2,
        error = 3
    };

    struct entry
    {
        int level = 0;
        char time[16] = { };      // "HH:MM:SS", local clock
        char text[320] = { };
    };

    // Everything the Injector page hands over when Inject is pressed.
    struct options
    {
        method how = method::load_library;
        unsigned int pid = 0;
        char name[128] = { };       // target name, only for log lines
        bool target_x64 = false;
        char dll_path[256] = { };
        int delay_ms = 0;
        bool auto_retry = false;
        bool verbose = false;
        bool sound_effects = false;
    };

    // Set by every finished run; the UI polls the epoch to fire toasts.
    struct completion
    {
        unsigned int epoch = 0;     // increments once per finished run
        bool success = false;
    };

    bool running( );
    bool start( const options& o ); // false while another run is in flight

    std::vector< entry > snapshot( );
    void clear_log( );

    // Live settings from the Settings page's Logging section.
    void configure_file_log( bool enabled, int retention_days );

    // System Status values (always real, never hand-written):
    //   0 = Ready, 1 = Working..., 2 = Failed
    int status( );
    bool dll_loaded( );             // last run loaded the DLL into the target
    completion last_completion( );  // toast source
    const char* last_target( );     // target name from the last run
    const char* last_dll( );        // dll base name from the last run
}
