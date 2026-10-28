#pragma once
#include "win32.hpp"
#include <cstddef>

namespace inject::detail
{
    void log( int level, const char* format, ... );
    void win32_fail( const char* operation, char* why, std::size_t size );
    inline std::wstring to_wide( const char* text ) { return win::wide( text ? text : "" ); }
    inline std::string to_utf8( const wchar_t* text ) { return win::utf8( text ? text : L"" ); }
    std::string base_name( const char* path );
}
