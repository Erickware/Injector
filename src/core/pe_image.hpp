#pragma once
#include "win32.hpp"
#include "../process.hpp"
#include <span>
#include <vector>

namespace pe
{
    struct image
    {
        process::architecture arch = process::architecture::unknown;
        bool is_dll = false;
        bool managed = false;
        std::uint32_t image_size = 0;
        std::uint32_t entry_point = 0;
        std::uint64_t preferred_base = 0;
        std::vector<IMAGE_SECTION_HEADER> sections;
        IMAGE_DATA_DIRECTORY directories[IMAGE_NUMBEROF_DIRECTORY_ENTRIES]{ };
    };

    win::error inspect( std::span<const std::uint8_t> bytes, image& out );
    win::error read( const std::string& utf8_path, std::vector<std::uint8_t>& bytes, image& out );
    win::error compatible( const image& dll, process::architecture target );
    win::error manual_map_support( std::span<const std::uint8_t> bytes, const image& dll );
}
