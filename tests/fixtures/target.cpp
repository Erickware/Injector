#include <windows.h>
#include <string>

int wmain( int argc, wchar_t** argv )
{
    if ( argc < 2 || argc > 3 ) return 2;
    const std::wstring prefix = argv[1];
    HANDLE stop = OpenEventW( SYNCHRONIZE, FALSE, ( prefix + L"-stop" ).c_str( ) );
    HANDLE ready = OpenEventW( EVENT_MODIFY_STATE, FALSE, ( prefix + L"-ready" ).c_str( ) );
    if ( !stop || !ready ) return 3;
    if ( argc == 3 )
    {
        PROCESS_MITIGATION_DYNAMIC_CODE_POLICY policy{};
        policy.ProhibitDynamicCode = 1;
        if ( !SetProcessMitigationPolicy( ProcessDynamicCodePolicy, &policy, sizeof( policy ) ) ) return 5;
    }
#ifdef _WIN64
    // Force the Manual Map test to exercise relocation instead of the preferred base.
    void* reserved = VirtualAlloc( reinterpret_cast<void*>( 0x180000000ull ), 0x10000, MEM_RESERVE, PAGE_NOACCESS );
#endif
    SetEvent( ready );
    CloseHandle( ready );
    const auto deadline = GetTickCount64( ) + 30000;
    DWORD wait = WAIT_TIMEOUT;
    while ( wait == WAIT_TIMEOUT && GetTickCount64( ) < deadline ) wait = WaitForSingleObject( stop, 10 );
    CloseHandle( stop );
#ifdef _WIN64
    if ( reserved ) VirtualFree( reserved, 0, MEM_RELEASE );
#endif
    return wait == WAIT_OBJECT_0 ? 0 : 4;
}
