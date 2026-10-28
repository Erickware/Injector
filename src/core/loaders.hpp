#pragma once
#include "../inject.hpp"
#include "win32.hpp"
#include <stop_token>

namespace inject::detail
{
    struct backend_result
    {
        bool success = false;
        bool indeterminate = false;
        std::string message;
    };
    backend_result execute( HANDLE target, const options& request, const std::vector<std::uint8_t>& image, std::stop_token stop );
}
