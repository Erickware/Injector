#pragma once
#include "win32.hpp"
#include <vector>

namespace modules
{
    struct entry
    {
        std::string name;
        std::string path;
        std::uintptr_t base = 0;
        std::uint32_t size = 0;
    };
    struct snapshot
    {
        std::vector<entry> items;
        win::error failure;
    };
    snapshot inspect( DWORD pid, std::uint64_t expected_creation_time = 0 );
    std::uintptr_t find( const snapshot& list, const std::wstring& name_or_path );
}
