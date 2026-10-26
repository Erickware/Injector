#pragma once

// language: C++, file: src/process.hpp, target: Windows 10/11 x64, MSVC
// *process enumeration shared with the UI; snapshot of this machine's process list*

#include <cstdint>
#include <string>
#include <vector>

namespace process
{
    enum class architecture : std::uint8_t
    {
        unknown,
        x86,
        x64,
        arm64
    };

    struct entry
    {
        std::string name;                    // image name as reported by the toolhelp snapshot
        std::uint32_t pid = 0;
        std::uint32_t parent_pid = 0;
        std::uint32_t threads = 0;
        architecture arch = architecture::unknown;
        bool elevated = false;               // best-effort; true only when proven
        bool system = false;                 // pid 0/4 or session 0 style protected process
    };

    // Rebuilds the list from a fresh CreateToolhelp32Snapshot. Safe to call on a timer.
    // Returns false when the snapshot failed, in which case the previous list is kept
    // so the panel never blanks out on a transient failure.
    bool refresh( );

    const std::vector< entry >& list( );

    // Milliseconds since the process started at the last successful refresh.
    std::uint64_t last_refresh_ms( );

    // Case-insensitive substring match against the image name or the pid digits.
    bool matches( const entry& item, const char* query );

    // True when the window's title contains the query; used as a second search pass.
    bool title_matches( std::uint32_t pid, const char* query );

    const char* architecture_name( architecture value );
}
