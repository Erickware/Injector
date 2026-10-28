#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include <cstddef>

#include <imgui.h>

namespace ui::widgets
{
    // Existing button styling with flexible sizing and native input ownership.
    bool action_button( const char* label, ImVec2 size, const char* icon = nullptr, bool enabled = true );
    bool close_button( const char* id );
    struct scrollbar_style
    {
        scrollbar_style( );
        ~scrollbar_style( );
        scrollbar_style( const scrollbar_style& ) = delete;
        scrollbar_style& operator=( const scrollbar_style& ) = delete;
    };
    bool search_box( const char* label, char* value, std::size_t size );
    bool combo( const char* label, int& value, const char* const* items, int count );
    bool input_text( char* value, std::size_t size, const char* text );
    bool button( const char* label );
    bool button_2( const char* label );
    bool search_box_2( const char* label, char* value, std::size_t size );
    bool button_3( const char* label );
    bool checkbox( const char* label, bool& value );
    bool checkbox_2( const char* label, bool& value );
    bool checkbox_2( const char* label, bool& value, ImVec2 hit_min, ImVec2 hit_size );
    bool checkbox_2( const char* label, bool& value, ImVec2 hit_min, ImVec2 hit_size, ImVec2 box_at );
    bool button_4( const char* label );
    bool combo_2( const char* label, int& value, const char* const* items, int count );
    bool toggle( const char* label, bool& value );
    bool slider( const char* label, float& value, float min, float max, const char* format );
    bool slider_2( const char* label, float& value, float min, float max, const char* format );
    bool slider_3( const char* label, float& value, float min, float max, const char* format );
    bool combo_3( const char* label, int& value, const char* const* items, int count );
    bool button_5( const char* label, const char* icon );
    bool button_6( const char* label );

    template < std::size_t size >
    bool search_box( const char* label, char ( &value )[size] )
    {
        return search_box( label, value, size );
    }

    template < int count >
    bool combo( const char* label, int& value, const char* const ( &items )[count] )
    {
        return combo( label, value, items, count );
    }

    template < std::size_t size >
    bool input_text( char ( &value )[size], const char* text )
    {
        return input_text( value, size, text );
    }

    template < std::size_t size >
    bool search_box_2( const char* label, char ( &value )[size] )
    {
        return search_box_2( label, value, size );
    }

    template < int count >
    bool combo_2( const char* label, int& value, const char* const ( &items )[count] )
    {
        return combo_2( label, value, items, count );
    }

    template < int count >
    bool combo_3( const char* label, int& value, const char* const ( &items )[count] )
    {
        return combo_3( label, value, items, count );
    }
}
