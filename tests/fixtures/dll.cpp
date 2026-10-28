#include <windows.h>
extern "C" BOOL WINAPI DllMain( HINSTANCE, DWORD, LPVOID );
static auto* volatile relocation_anchor = &DllMain;

// Dedicated test DLL: no C++ runtime, TLS, security cookie or exceptions.
// Creates a named event proving that DllMain actually executed in the child.
extern "C" BOOL WINAPI DllMain( HINSTANCE module, DWORD reason, LPVOID )
{
    if ( reason != DLL_PROCESS_ATTACH ) return TRUE;
    if ( relocation_anchor != &DllMain ) return FALSE;
    DWORD64 image_base = 0;
    if ( !RtlLookupFunctionEntry( reinterpret_cast<DWORD64>( &DllMain ), &image_base, nullptr ) ||
         image_base != reinterpret_cast<DWORD64>( module ) ) return FALSE;
#ifdef INJECTOR_FIXTURE_SLOW
    Sleep( 1500 );
#endif
#ifdef INJECTOR_FIXTURE_REJECT
    return FALSE;
#else
    wchar_t name[80] = L"Local\\InjectorFixture-";
    wchar_t digits[16]{};
    DWORD pid = GetCurrentProcessId( );
    unsigned count = 0;
    do { digits[count++] = L'0' + pid % 10; pid /= 10; } while ( pid );
    unsigned at = 22;
    while ( count ) name[at++] = digits[--count];
    name[at] = 0;
    // The harness opens/owns this event before injection, so no handle is leaked.
    HANDLE event = OpenEventW( EVENT_MODIFY_STATE, FALSE, name );
    if ( !event ) return FALSE;
    SetEvent( event );
    CloseHandle( event );
    return TRUE;
#endif
}
