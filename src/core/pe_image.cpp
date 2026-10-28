#include "pe_image.hpp"
#include <algorithm>
#include <cstring>

namespace pe
{
    namespace
    {
        bool range( std::size_t offset, std::size_t count, std::size_t size )
        { return offset <= size && count <= size - offset; }

        template<typename T> bool get( std::span<const std::uint8_t> bytes, std::size_t offset, T& value )
        {
            if ( !range( offset, sizeof( T ), bytes.size( ) ) ) return false;
            std::memcpy( &value, bytes.data( ) + offset, sizeof( T ) );
            return true;
        }
        win::error invalid( const char* message ) { return { ERROR_BAD_EXE_FORMAT, message }; }
    }

    win::error inspect( std::span<const std::uint8_t> bytes, image& out )
    {
        out = { };
        image result;
        IMAGE_DOS_HEADER dos{ };
        if ( !get( bytes, 0, dos ) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < sizeof( dos ) )
            return invalid( "Invalid DOS header or PE offset" );
        const std::size_t at = static_cast<std::size_t>( dos.e_lfanew );
        DWORD signature = 0;
        IMAGE_FILE_HEADER file{ };
        if ( !get( bytes, at, signature ) || signature != IMAGE_NT_SIGNATURE || !get( bytes, at + sizeof( DWORD ), file ) )
            return invalid( "Invalid or truncated PE header" );
        const std::size_t optional_at = at + sizeof( DWORD ) + sizeof( file );
        WORD magic = 0;
        if ( !range( optional_at, file.SizeOfOptionalHeader, bytes.size( ) ) || !get( bytes, optional_at, magic ) )
            return invalid( "Truncated optional header" );
        DWORD headers = 0, alignment = 0, section_alignment = 0, directory_count = 0;
        auto parse = [&]( const auto& optional )
        {
            result.image_size = optional.SizeOfImage;
            result.entry_point = optional.AddressOfEntryPoint;
            result.preferred_base = optional.ImageBase;
            headers = optional.SizeOfHeaders;
            alignment = optional.FileAlignment;
            section_alignment = optional.SectionAlignment;
            directory_count = optional.NumberOfRvaAndSizes;
            std::copy_n( optional.DataDirectory, IMAGE_NUMBEROF_DIRECTORY_ENTRIES, result.directories );
        };
        if ( magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC )
        {
            IMAGE_OPTIONAL_HEADER64 optional{ };
            if ( file.SizeOfOptionalHeader < sizeof( optional ) || !get( bytes, optional_at, optional ) ) return invalid( "Truncated PE32+ header" );
            if ( file.Machine != IMAGE_FILE_MACHINE_AMD64 && file.Machine != IMAGE_FILE_MACHINE_ARM64 ) return invalid( "PE32+ machine mismatch" );
            result.arch = file.Machine == IMAGE_FILE_MACHINE_AMD64 ? process::architecture::x64 : process::architecture::arm64;
            parse( optional );
        }
        else if ( magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC )
        {
            IMAGE_OPTIONAL_HEADER32 optional{ };
            if ( file.SizeOfOptionalHeader < sizeof( optional ) || !get( bytes, optional_at, optional ) ) return invalid( "Truncated PE32 header" );
            if ( file.Machine != IMAGE_FILE_MACHINE_I386 ) return invalid( "Unsupported PE32 machine" );
            result.arch = process::architecture::x86;
            parse( optional );
        }
        else return invalid( "Unsupported optional header magic" );

        if ( directory_count > IMAGE_NUMBEROF_DIRECTORY_ENTRIES || file.NumberOfSections == 0 || file.NumberOfSections > 96 )
            return invalid( "Invalid directory or section count" );
        for ( DWORD i = directory_count; i < IMAGE_NUMBEROF_DIRECTORY_ENTRIES; ++i ) result.directories[i] = { };
        const std::size_t sections_at = optional_at + file.SizeOfOptionalHeader;
        if ( !range( sections_at, file.NumberOfSections * sizeof( IMAGE_SECTION_HEADER ), bytes.size( ) ) ||
             !range( sections_at, file.NumberOfSections * sizeof( IMAGE_SECTION_HEADER ), headers ) ) return invalid( "Truncated section table" );
        if ( headers == 0 || headers > bytes.size( ) || headers > result.image_size || result.image_size > 512u * 1024 * 1024 ||
             alignment == 0 || ( alignment & ( alignment - 1 ) ) != 0 || section_alignment < alignment )
            return invalid( "Invalid image size, headers or alignment" );
        if ( result.entry_point >= result.image_size ) return invalid( "Entry point outside image" );
        for ( WORD i = 0; i < file.NumberOfSections; ++i )
        {
            IMAGE_SECTION_HEADER section{ };
            get( bytes, sections_at + i * sizeof( section ), section );
            const auto extent = (std::max)( section.Misc.VirtualSize, section.SizeOfRawData );
            if ( !range( section.PointerToRawData, section.SizeOfRawData, bytes.size( ) ) ||
                 !range( section.VirtualAddress, extent, result.image_size ) || section.VirtualAddress < headers )
                return invalid( "Section exceeds file/image bounds or overlaps headers" );
            for ( const auto& previous : result.sections )
            {
                const std::uint64_t previous_end = std::uint64_t( previous.VirtualAddress ) + (std::max)( previous.Misc.VirtualSize, previous.SizeOfRawData );
                if ( extent && section.VirtualAddress < previous_end && previous.VirtualAddress < std::uint64_t( section.VirtualAddress ) + extent )
                    return invalid( "Overlapping image sections" );
            }
            result.sections.push_back( section );
        }
        if ( result.entry_point && std::none_of( result.sections.begin( ), result.sections.end( ), [&]( const auto& section )
             { return ( section.Characteristics & IMAGE_SCN_MEM_EXECUTE ) && result.entry_point >= section.VirtualAddress &&
                 result.entry_point - section.VirtualAddress < section.Misc.VirtualSize; } ) )
            return invalid( "Entry point is not inside an executable section" );
        for ( DWORD i = 0; i < directory_count; ++i )
        {
            const auto& directory = result.directories[i];
            if ( !directory.Size && !directory.VirtualAddress ) continue;
            const std::size_t size = i == IMAGE_DIRECTORY_ENTRY_SECURITY ? bytes.size( ) : result.image_size;
            if ( !directory.VirtualAddress || !directory.Size || !range( directory.VirtualAddress, directory.Size, size ) )
                return invalid( "Data directory outside file/image" );
        }
        result.is_dll = ( file.Characteristics & IMAGE_FILE_DLL ) != 0;
        result.managed = result.directories[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress != 0;
        out = std::move( result );
        return { };
    }

    win::error read( const std::string& path, std::vector<std::uint8_t>& bytes, image& out )
    {
        bytes.clear( ); out = { };
        const auto wide_path = win::wide( path );
        if ( wide_path.empty( ) ) return { ERROR_INVALID_NAME, "Empty path or invalid UTF-8" };
        win::handle file( CreateFileW( wide_path.c_str( ), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr ) );
        if ( !file ) return win::failure( "Open DLL" );
        LARGE_INTEGER size{ };
        if ( !GetFileSizeEx( file, &size ) ) return win::failure( "Read DLL size" );
        if ( size.QuadPart <= 0 || size.QuadPart > 256ll * 1024 * 1024 ) return { ERROR_FILE_TOO_LARGE, "DLL file size must be between 1 byte and 256 MiB" };
        bytes.resize( static_cast<std::size_t>( size.QuadPart ) );
        DWORD got = 0;
        if ( !ReadFile( file, bytes.data( ), static_cast<DWORD>( bytes.size( ) ), &got, nullptr ) ) return win::failure( "Read DLL" );
        if ( got != bytes.size( ) ) return { ERROR_HANDLE_EOF, "DLL changed or was truncated during read" };
        return inspect( bytes, out );
    }

    win::error compatible( const image& dll, process::architecture target )
    {
        if ( !dll.is_dll ) return invalid( "The selected PE is not a DLL" );
        if ( dll.managed ) return { ERROR_NOT_SUPPORTED, "Managed DLLs require a CLR host; this backend only loads native DLLs" };
        if ( target == process::architecture::unknown ) return { ERROR_NOT_SUPPORTED, "Target architecture could not be determined" };
        if ( dll.arch != target ) return { ERROR_EXE_MACHINE_TYPE_MISMATCH, "DLL and target architectures differ" };
        if ( target != process::architecture::x64 ) return { ERROR_NOT_SUPPORTED, "This build loads x64 DLLs into x64 targets only" };
        return { };
    }

    win::error manual_map_support( std::span<const std::uint8_t> bytes, const image& dll )
    {
        // These require loader services the current backend does not implement.
        const int unsupported[] = { IMAGE_DIRECTORY_ENTRY_TLS, IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT,
            IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG };
        for ( int index : unsupported )
            if ( dll.directories[index].Size ) return { ERROR_NOT_SUPPORTED,
                "Manual Map does not support TLS, delayed imports or load configuration in this build. Use LoadLibrary for this DLL." };
        if ( !dll.entry_point ) return { ERROR_NOT_SUPPORTED, "Manual Map currently requires a native entry point" };
        std::vector<std::uint8_t> mapped( dll.image_size, 0 );
        IMAGE_DOS_HEADER dos{}; get( bytes, 0, dos );
        IMAGE_NT_HEADERS64 nt{}; get( bytes, static_cast<std::size_t>( dos.e_lfanew ), nt );
        std::memcpy( mapped.data( ), bytes.data( ), nt.OptionalHeader.SizeOfHeaders );
        for ( const auto& section : dll.sections )
            if ( section.SizeOfRawData ) std::memcpy( mapped.data( ) + section.VirtualAddress, bytes.data( ) + section.PointerToRawData, section.SizeOfRawData );
        auto string_at = [&]( std::size_t offset )
        { return offset < mapped.size( ) && mapped[offset] && std::memchr( mapped.data( ) + offset, 0, mapped.size( ) - offset ) != nullptr; };
        const auto& exceptions = dll.directories[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if ( exceptions.Size % sizeof( RUNTIME_FUNCTION ) ) return invalid( "Invalid exception directory size" );
        DWORD previous_end = 0;
        for ( std::size_t offset = 0; offset < exceptions.Size; offset += sizeof( RUNTIME_FUNCTION ) )
        {
            RUNTIME_FUNCTION function{};
            get( mapped, std::size_t( exceptions.VirtualAddress ) + offset, function );
            if ( function.BeginAddress < previous_end || function.BeginAddress >= function.EndAddress ||
                 function.EndAddress > mapped.size( ) || function.UnwindData >= mapped.size( ) )
                return invalid( "Invalid runtime function/unwind entry" );
            previous_end = function.EndAddress;
        }
        const auto& relocs = dll.directories[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        std::size_t cursor = relocs.VirtualAddress;
        const std::size_t end = cursor + relocs.Size;
        while ( cursor < end )
        {
            IMAGE_BASE_RELOCATION block{};
            if ( !range( cursor, sizeof( block ), end ) || !get( mapped, cursor, block ) ||
                 block.SizeOfBlock < sizeof( block ) || block.SizeOfBlock % 2 || !range( cursor, block.SizeOfBlock, end ) )
                return invalid( "Malformed relocation block" );
            for ( std::size_t offset = sizeof( block ); offset < block.SizeOfBlock; offset += sizeof( WORD ) )
            {
                WORD fixup = 0; get( mapped, cursor + offset, fixup );
                const auto type = fixup >> 12;
                if ( type == IMAGE_REL_BASED_ABSOLUTE ) continue;
                const auto slot = std::uint64_t( block.VirtualAddress ) + ( fixup & 0xfff );
                if ( type != IMAGE_REL_BASED_DIR64 || !range( slot, sizeof( std::uint64_t ), mapped.size( ) ) ||
                     slot < nt.OptionalHeader.SizeOfHeaders || ( slot < end && slot + 8 > relocs.VirtualAddress ) )
                    return invalid( "Unsupported or out-of-range relocation" );
            }
            cursor += block.SizeOfBlock;
        }
        const auto& imports = dll.directories[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if ( imports.Size )
        {
            bool terminated = false;
            for ( std::size_t at = imports.VirtualAddress; range( at, sizeof( IMAGE_IMPORT_DESCRIPTOR ), std::size_t( imports.VirtualAddress ) + imports.Size ); at += sizeof( IMAGE_IMPORT_DESCRIPTOR ) )
            {
                IMAGE_IMPORT_DESCRIPTOR descriptor{}; get( mapped, at, descriptor );
                if ( !descriptor.Name ) { terminated = true; break; }
                if ( !string_at( descriptor.Name ) || !descriptor.FirstThunk ) return invalid( "Invalid import descriptor" );
                std::size_t thunk = descriptor.OriginalFirstThunk ? descriptor.OriginalFirstThunk : descriptor.FirstThunk;
                std::size_t iat = descriptor.FirstThunk;
                bool thunk_end = false;
                while ( range( thunk, sizeof( std::uint64_t ), mapped.size( ) ) && range( iat, sizeof( std::uint64_t ), mapped.size( ) ) )
                {
                    std::uint64_t value = 0; get( mapped, thunk, value );
                    if ( !value ) { thunk_end = true; break; }
                    if ( !( value & IMAGE_ORDINAL_FLAG64 ) && ( value > UINT32_MAX || !range( value, sizeof( WORD ) + 1, mapped.size( ) ) || !string_at( static_cast<std::size_t>( value ) + sizeof( WORD ) ) ) )
                        return invalid( "Invalid import name RVA" );
                    thunk += sizeof( value ); iat += sizeof( value );
                }
                if ( !thunk_end ) return invalid( "Unterminated import thunk table" );
            }
            if ( !terminated ) return invalid( "Unterminated import descriptor table" );
        }
        return { };
    }
}
