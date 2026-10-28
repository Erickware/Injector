#pragma once

// Asynchronous loading service, operation state and diagnostic snapshots.

#include <cstddef>
#include <vector>
#include <cstdint>
#include <string>

namespace inject
{
    // Stable values used by profiles and the UI.
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
        char text[2048] = { };
    };

    // Everything the Injector page hands over when Inject is pressed.
    struct options
    {
        method how = method::load_library;
        unsigned int pid = 0;
        char name[128] = { };       // target name, only for log lines
        std::uint64_t target_creation_time = 0;
        char dll_path[32768] = { };
        int delay_ms = 0;
        unsigned int timeout_ms = 15000;
        bool auto_retry = false;
        bool verbose = false;
        bool sound_effects = false;
    };

    // Completion counter for notification consumers.
    struct completion
    {
        unsigned int epoch = 0;     // increments once per finished run
        bool success = false;
    };

    bool running( );
    bool start( const options& o ); // false while another run is in flight
    void cancel( );
    void shutdown( );

    enum class phase { idle, validating, delay, executing, succeeded, failed, cancelled, indeterminate };
    struct operation
    {
        unsigned int id = 0;
        unsigned int pid = 0;
        method how = method::load_library;
        phase state = phase::idle;
        std::uint32_t error_code = 0;
        std::uint64_t elapsed_ms = 0;
        std::string message;
    };
    operation current_operation( );
    const char* phase_name( phase state );

    std::vector< entry > snapshot( );
    void clear_log( );

    // Live settings from the Settings page's Logging section.
    void configure_file_log( bool enabled, int retention_days );

    // Legacy status adapter:
    //   0 = Ready, 1 = Working..., 2 = Failed
    int status( );
    bool dll_loaded( );             // last run loaded the DLL into the target
    completion last_completion( );  // toast source
    std::string last_target( );
    std::string last_dll( );
}
