#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../../vendor/blackbone/HandleGuard.hpp"
#include <string>
#include <string_view>

namespace win
{
    using handle = blackbone::Handle;

    class remote_allocation
    {
        HANDLE process_;
        void* address_ = nullptr;
    public:
        explicit remote_allocation( HANDLE process, void* address = nullptr ) : process_( process ), address_( address ) { }
        remote_allocation( const remote_allocation& ) = delete;
        remote_allocation& operator=( const remote_allocation& ) = delete;
        ~remote_allocation( ) { reset( ); }
        void reset( void* address = nullptr )
        {
            if ( address_ ) VirtualFreeEx( process_, address_, 0, MEM_RELEASE );
            address_ = address;
        }
        void* release( ) { void* value = address_; address_ = nullptr; return value; }
    };

    inline std::wstring wide( std::string_view text )
    {
        if ( text.empty( ) ) return { };
        const int count = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, text.data( ), static_cast<int>( text.size( ) ), nullptr, 0 );
        if ( count <= 0 ) return { };
        std::wstring result( count, L'\0' );
        MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, text.data( ), static_cast<int>( text.size( ) ), result.data( ), count );
        return result;
    }

    inline std::string utf8( std::wstring_view text )
    {
        if ( text.empty( ) ) return { };
        const int count = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, text.data( ), static_cast<int>( text.size( ) ), nullptr, 0, nullptr, nullptr );
        if ( count <= 0 ) return { };
        std::string result( count, '\0' );
        WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, text.data( ), static_cast<int>( text.size( ) ), result.data( ), count, nullptr, nullptr );
        return result;
    }

    struct error
    {
        DWORD code = ERROR_SUCCESS;
        std::string message;
        explicit operator bool( ) const { return code != ERROR_SUCCESS; }
    };

    inline error failure( std::string_view operation, DWORD code = GetLastError( ) )
    {
        wchar_t text[1024]{ };
        FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, text, 1024, nullptr );
        std::string message = utf8( text );
        while ( !message.empty( ) && ( message.back( ) == '\r' || message.back( ) == '\n' ) ) message.pop_back( );
        return { code, std::string( operation ) + " (" + std::to_string( code ) + "): " + message };
    }
}
