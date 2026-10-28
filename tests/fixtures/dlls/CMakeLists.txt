# These fixtures deliberately have no CRT. Disable compiler runtime-check helper
# calls only in this subdirectory; keep runtime checks enabled in the core/tests.
string( REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}" )
foreach( fixture IN ITEMS Good Slow Reject )
    add_library( InjectorFixture${fixture} SHARED ../dll.cpp )
    target_compile_options( InjectorFixture${fixture} PRIVATE /GS- /Zl /utf-8 )
    target_link_options( InjectorFixture${fixture} PRIVATE /NODEFAULTLIB /ENTRY:DllMain /INCREMENTAL:NO )
    target_link_libraries( InjectorFixture${fixture} PRIVATE kernel32 )
endforeach( )
target_compile_definitions( InjectorFixtureSlow PRIVATE INJECTOR_FIXTURE_SLOW=1 )
target_compile_definitions( InjectorFixtureReject PRIVATE INJECTOR_FIXTURE_REJECT=1 )
