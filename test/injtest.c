// Test DLL for the injector's behavioural verification suite.
// No CRT: only USER32!MessageBoxW and KERNEL32!CreateThread imports, so the
// manual-map resolver gets real work (KERNEL32 forwards on Win10+ are covered)
// and the message box never blocks DllMain.
#include <windows.h>

static DWORD WINAPI marker_thread( LPVOID param )
{
    ( void )param;
    MessageBoxW( NULL,
                 L"loaded by the injector; close me to continue the test",
                 L"INJTEST-MARKER", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND );
    return 0;
}

BOOL WINAPI DllMain( HMODULE module, DWORD reason, LPVOID reserved )
{
    ( void )module; ( void )reserved;
    if ( reason != DLL_PROCESS_ATTACH )
        return TRUE;
    HANDLE thread = CreateThread( NULL, 0, marker_thread, NULL, 0, NULL );
    if ( thread )
        CloseHandle( thread );
    return TRUE;
}
