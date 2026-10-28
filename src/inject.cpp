#include "inject.hpp"
#include "core/diagnostics.hpp"
#include "core/loaders.hpp"
#include "core/pe_image.hpp"
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace inject
{
    namespace
    {
        struct engine
        {
            std::mutex mutex;
            std::mutex lifecycle;
            std::condition_variable wake;
            bool busy = false;
            bool stopping = false;
            bool loaded = false;
            completion completed;
            operation active;
            std::string target;
            std::string dll;
            ULONGLONG started = 0;
            std::jthread worker; // destroyed before state/mutexes

            ~engine( )
            {
                worker.request_stop( );
                wake.notify_all( );
                if ( worker.joinable( ) ) worker.join( );
            }
        } state;

        void stage( phase next )
        {
            std::lock_guard guard( state.mutex );
            state.active.state = next;
        }

        win::error validate_request( const options& o )
        {
            if ( !o.pid ) return { ERROR_INVALID_PARAMETER, "Select a process in Process List" };
            if ( static_cast<unsigned>( o.how ) > 2 ) return { ERROR_INVALID_PARAMETER, "Unknown injection method" };
            if ( !std::memchr( o.dll_path, 0, sizeof( o.dll_path ) ) || !o.dll_path[0] ) return { ERROR_INVALID_NAME, "Select a DLL using Browse" };
            if ( !std::memchr( o.name, 0, sizeof( o.name ) ) ) return { ERROR_INVALID_PARAMETER, "Invalid target name" };
            if ( o.delay_ms < 0 || o.delay_ms > 2000 || o.timeout_ms < 100 || o.timeout_ms > 120000 )
                return { ERROR_INVALID_PARAMETER, "Delay/timeout outside supported limits" };
            return { };
        }

        void run( options o, std::stop_token stop ) noexcept
        {
            win::error failure;
            phase final_state = phase::failed;
            try
            {
                failure = validate_request( o );
                if ( failure ) throw failure;
                detail::log( info, "Operation started: %s, PID %u", o.name, o.pid );
                std::vector<std::uint8_t> bytes;
                pe::image image;
                failure = pe::read( o.dll_path, bytes, image );
                if ( failure ) throw failure;

                // Hold the same process object through delay/execution; never reopen a reused PID.
                win::handle target( OpenProcess( PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                    PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, o.pid ) );
                if ( !target ) throw win::failure( "Open target process" );
                if ( WaitForSingleObject( target, 0 ) != WAIT_TIMEOUT ) throw win::error{ ERROR_PROCESS_ABORTED, "Target has exited" };
                const auto created = process::creation_time( target );
                if ( o.target_creation_time && created != o.target_creation_time )
                    throw win::error{ ERROR_INVALID_HANDLE, "PID was reused; select the target again" };
                const auto arch = process::query_architecture( target );
                failure = pe::compatible( image, arch );
                if ( failure ) throw failure;
                if ( o.how == method::manual_map )
                {
                    failure = pe::manual_map_support( bytes, image );
                    if ( failure ) throw failure;
                }
                if ( o.how != method::load_library )
                {
                    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY policy{};
                    if ( !GetProcessMitigationPolicy( target, ProcessDynamicCodePolicy, &policy, sizeof( policy ) ) )
                        throw win::failure( "Query target dynamic-code policy" );
                    if ( policy.ProhibitDynamicCode ) throw win::error{ ERROR_ACCESS_DISABLED_BY_POLICY, "Target prohibits dynamic code; operation stopped" };
                }
                if ( o.verbose ) detail::log( info, "DLL validated: %s (%s, %zu bytes); timeout %ums", detail::base_name( o.dll_path ).c_str( ), process::architecture_name( image.arch ), bytes.size( ), o.timeout_ms );
                if ( o.delay_ms )
                {
                    stage( phase::delay );
                    std::unique_lock guard( state.mutex );
                    state.wake.wait_for( guard, std::chrono::milliseconds( o.delay_ms ), [&] { return stop.stop_requested( ); } );
                }
                if ( stop.stop_requested( ) ) throw win::error{ ERROR_CANCELLED, "Operation cancelled before execution" };
                if ( WaitForSingleObject( target, 0 ) != WAIT_TIMEOUT ) throw win::error{ ERROR_PROCESS_ABORTED, "Target exited before execution" };
                stage( phase::executing );
                const int attempts = o.auto_retry ? 2 : 1;
                for ( int i = 0; i < attempts; ++i )
                {
                    if ( stop.stop_requested( ) ) throw win::error{ ERROR_CANCELLED, "Operation cancelled" };
                    if ( i ) detail::log( warn, "Retrying operation after a completed failure" );
                    const auto result = detail::execute( target, o, bytes, stop );
                    if ( result.success ) { final_state = phase::succeeded; failure = {}; break; }
                    failure = { static_cast<DWORD>( result.indeterminate ? ERROR_IO_INCOMPLETE : ERROR_DLL_INIT_FAILED ), result.message };
                    if ( result.indeterminate ) { final_state = phase::indeterminate; break; }
                }
            }
            catch ( const win::error& error ) { failure = error; }
            catch ( const std::exception& error ) { failure = { ERROR_UNHANDLED_EXCEPTION, error.what( ) }; }
            catch ( ... ) { failure = { ERROR_UNHANDLED_EXCEPTION, "Unexpected operation failure" }; }

            if ( failure.code == ERROR_CANCELLED ) final_state = phase::cancelled;
            const bool success = final_state == phase::succeeded;
            const std::string target_name( o.name, strnlen_s( o.name, sizeof( o.name ) ) );
            const std::string path( o.dll_path, strnlen_s( o.dll_path, sizeof( o.dll_path ) ) );
            const std::string message = success ? "DLL loaded successfully" : ( failure.message.empty() ? "Operation failed" : failure.message );
            detail::log( success ? ok : error, "%s: %s", phase_name( final_state ), message.c_str( ) );
            if ( o.sound_effects ) MessageBeep( success ? MB_ICONASTERISK : MB_ICONHAND );
            {
                std::lock_guard guard( state.mutex );
                state.active.state = final_state;
                state.active.error_code = failure.code;
                state.active.message = message;
                state.active.elapsed_ms = GetTickCount64( ) - state.started;
                state.loaded = success;
                state.target = target_name;
                state.dll = detail::base_name( path.c_str( ) );
                ++state.completed.epoch;
                state.completed.success = success;
                state.busy = false;
            }
        }
    }

    bool start( const options& o )
    {
        std::lock_guard lifecycle( state.lifecycle );
        { std::lock_guard guard( state.mutex ); if ( state.busy || state.stopping ) return false; }
        if ( state.worker.joinable( ) ) state.worker.join( );
        {
            std::lock_guard guard( state.mutex );
            state.busy = true;
            state.loaded = false;
            state.started = GetTickCount64( );
            state.target.assign( o.name, strnlen_s( o.name, sizeof( o.name ) ) );
            const std::string path( o.dll_path, strnlen_s( o.dll_path, sizeof( o.dll_path ) ) );
            state.dll = detail::base_name( path.c_str( ) );
            state.active = { state.active.id + 1, o.pid, o.how, phase::validating };
        }
        try { state.worker = std::jthread( [o]( std::stop_token stop ) { run( o, stop ); } ); }
        catch ( const std::exception& error )
        {
            std::lock_guard guard( state.mutex ); state.busy = false;
            state.active.state = phase::failed; state.active.message = error.what( );
            return false;
        }
        return true;
    }
    void cancel( )
    {
        std::lock_guard lifecycle( state.lifecycle );
        state.worker.request_stop( ); state.wake.notify_all( );
    }
    void shutdown( )
    {
        std::lock_guard lifecycle( state.lifecycle );
        { std::lock_guard guard( state.mutex ); state.stopping = true; }
        state.worker.request_stop( ); state.wake.notify_all( );
        if ( state.worker.joinable( ) ) state.worker.join( );
    }
    bool running( ) { std::lock_guard guard( state.mutex ); return state.busy; }
    bool dll_loaded( ) { std::lock_guard guard( state.mutex ); return state.loaded; }
    completion last_completion( ) { std::lock_guard guard( state.mutex ); return state.completed; }
    std::string last_target( ) { std::lock_guard guard( state.mutex ); return state.target; }
    std::string last_dll( ) { std::lock_guard guard( state.mutex ); return state.dll; }
    operation current_operation( )
    {
        std::lock_guard guard( state.mutex );
        auto copy = state.active;
        if ( state.busy ) copy.elapsed_ms = GetTickCount64( ) - state.started;
        return copy;
    }
    int status( )
    {
        const auto current = current_operation( );
        if ( current.state == phase::validating || current.state == phase::delay || current.state == phase::executing ) return 1;
        return current.state == phase::idle || current.state == phase::succeeded ? 0 : 2;
    }
    const char* phase_name( phase value )
    {
        switch ( value )
        {
        case phase::validating: return "Validating";
        case phase::delay: return "Waiting";
        case phase::executing: return "Executing";
        case phase::succeeded: return "Succeeded";
        case phase::failed: return "Failed";
        case phase::cancelled: return "Cancelled";
        case phase::indeterminate: return "Indeterminate";
        default: return "Ready";
        }
    }
}
