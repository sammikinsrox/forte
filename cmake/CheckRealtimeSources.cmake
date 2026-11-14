# Realtime build rule (SPEC §9.3): DSP sources must not use locks or other blocking primitives.
#
# Usage: cmake -DFORTE_RT_DIRS="dir1|dir2" -P CheckRealtimeSources.cmake
#
# Fails (non-zero exit) when any C++ source or header under the given directories includes a
# banned header or uses a banned lock type or call. Comments are ignored. There is no opt-out
# comment: if DSP code really needs one of these, it doesn't belong in the DSP sources.

if(NOT FORTE_RT_DIRS)
    message(FATAL_ERROR "CheckRealtimeSources: pass -DFORTE_RT_DIRS=<dir>[|<dir>...]")
endif()
string(REPLACE "|" ";" FORTE_RT_DIRS "${FORTE_RT_DIRS}")

# Standard headers whose purpose is blocking or thread management, plus platform and JUCE
# headers (JUCE brings CriticalSection, SpinLock, WaitableEvent and allocating containers).
set(banned_headers
    mutex shared_mutex condition_variable thread future semaphore latch barrier stop_token
    syncstream
    windows\\.h synchapi\\.h process\\.h pthread\\.h
    JuceHeader\\.h juce_[A-Za-z0-9_]+/[A-Za-z0-9_]+\\.h)
list(JOIN banned_headers "|" banned_header_alternatives)
set(include_regex "#[ \t]*include[ \t]*[<\"](${banned_header_alternatives})[>\"]")

# Lock types and blocking calls that could arrive through another header.
set(banned_tokens
    "std::(recursive_|timed_|recursive_timed_|shared_|shared_timed_)?mutex"
    "std::condition_variable(_any)?"
    "std::(lock_guard|unique_lock|scoped_lock|shared_lock|counting_semaphore|binary_semaphore)"
    "std::this_thread::sleep_(for|until)"
    "std::jthread" "std::thread"
    "juce::(CriticalSection|SpinLock|ReadWriteLock|WaitableEvent|ScopedLock)"
    "(Enter|Leave|TryEnter)CriticalSection"
    "Acquire(SRWLock(Exclusive|Shared))"
    "Wait(ForSingleObject|ForMultipleObjects|OnAddress)"
    "(^|[^A-Za-z0-9_])Sleep(Ex)?[ \t]*\\(")
list(JOIN banned_tokens "|" token_regex)

set(violations "")
set(scanned 0)

foreach(dir IN LISTS FORTE_RT_DIRS)
    file(GLOB_RECURSE sources LIST_DIRECTORIES false
        "${dir}/*.h" "${dir}/*.hpp" "${dir}/*.inl" "${dir}/*.cpp" "${dir}/*.cc" "${dir}/*.cxx")

    foreach(source IN LISTS sources)
        math(EXPR scanned "${scanned} + 1")
        file(READ "${source}" text)

        # Strip block comments, then line comments.
        string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" text "${text}")
        string(REGEX REPLACE "//[^\n]*" "" text "${text}")

        string(REGEX MATCHALL "${include_regex}" bad_includes "${text}")
        string(REGEX MATCHALL "${token_regex}" bad_tokens "${text}")

        foreach(hit IN LISTS bad_includes bad_tokens)
            string(APPEND violations "  ${source}: ${hit}\n")
        endforeach()
    endforeach()
endforeach()

if(violations)
    message(FATAL_ERROR
        "Realtime rule violated: DSP sources must not include blocking headers or use locks "
        "(SPEC §9.3).\n${violations}")
endif()

message(STATUS "Realtime rule: ${scanned} DSP source file(s) clean.")
