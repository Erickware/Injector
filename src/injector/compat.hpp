#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#if __has_include( "injector_config.hpp" )
#include "injector_config.hpp"
#endif

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace injector
{
    inline ImTextureID texture_id( const void* object ) noexcept
    {
        return ( ImTextureID )( std::uintptr_t )object;
    }

    inline void* keep_for_frame( const void* data, std::size_t size )
    {
        struct frame_store
        {
            int frame = -1;
            std::vector< std::unique_ptr< unsigned char[] > > blocks[2];
        };
        static frame_store store;
        const int frame = ImGui::GetFrameCount( );
        if ( store.frame != frame )
        {
            store.blocks[1].swap( store.blocks[0] );
            store.blocks[0].clear( );
            store.frame = frame;
        }
        store.blocks[0].emplace_back( new unsigned char[size > 0 ? size : 1] );
        if ( size > 0 )
            std::memcpy( store.blocks[0].back( ).get( ), data, size );
        return store.blocks[0].back( ).get( );
    }

    inline void add_callback( ImDrawList* draw, ImDrawCallback callback, const void* data, std::size_t size )
    {
#if IMGUI_VERSION_NUM >= 19130
        draw->AddCallback( callback, const_cast< void* >( data ), size );
#else
        draw->AddCallback( callback, keep_for_frame( data, size ) );
#endif
    }
}
