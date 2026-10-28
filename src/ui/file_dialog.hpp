#pragma once
#include "core/win32.hpp"
#include <optional>

namespace ui
{
    std::optional<std::string> choose_file( HWND owner, bool save, const wchar_t* filter, const wchar_t* title, const wchar_t* extension );
}
