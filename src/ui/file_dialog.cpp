#include "file_dialog.hpp"
#include "core/diagnostics.hpp"
#include "inject.hpp"
#include <commdlg.h>

namespace ui
{
    std::optional<std::string> choose_file( HWND owner, bool save, const wchar_t* filter, const wchar_t* title, const wchar_t* extension )
    {
        std::wstring path( 32768, L'\0' );
        OPENFILENAMEW dialog{ sizeof( dialog ) };
        dialog.hwndOwner = owner;
        dialog.lpstrFile = path.data( );
        dialog.nMaxFile = static_cast<DWORD>( path.size( ) );
        dialog.lpstrFilter = filter;
        dialog.nFilterIndex = 1;
        dialog.lpstrTitle = title;
        dialog.lpstrDefExt = extension;
        dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY | ( save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST );
        if ( !( save ? GetSaveFileNameW( &dialog ) : GetOpenFileNameW( &dialog ) ) )
        {
            const auto code = CommDlgExtendedError( );
            if ( code ) inject::detail::log( inject::error, "File dialog failed: %lu", code );
            return std::nullopt;
        }
        path.resize( wcslen( path.c_str( ) ) );
        return win::utf8( path );
    }
}
