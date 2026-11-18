cmake_minimum_required(VERSION 3.25)

# The one version number for every Forte binary (SPEC §9.1).
project(Forte VERSION 0.9.0 LANGUAGES C CXX)

# Windows with MSVC (Visual Studio 2026), or Linux with GCC or Clang, on x86-64 (SPEC §9.1): the
# engine's hot loops are SSE2 and AVX2.
if(WIN32)
    if(NOT MSVC)
        message(FATAL_ERROR "On Windows, Forte builds with MSVC (Visual Studio 2026). See SPEC §9.1.")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
        message(FATAL_ERROR "On Linux, Forte builds with GCC or Clang. See SPEC §9.1.")
    endif()
else()
    message(FATAL_ERROR "Forte builds on Windows and Linux. See SPEC §9.1.")
endif()

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64)$")
    message(FATAL_ERROR "Forte builds for x86-64 only (SSE2 and AVX2 kernels), not ${CMAKE_SYSTEM_PROCESSOR}. See SPEC §9.2.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The engine is a static library linked into the plugins, which on Linux are shared objects: it
# must be position-independent there (no effect on Windows).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Static CRT, so Forte.vst3 and Forte.exe need no VC++ redistributable (portable zip, SPEC §9.4).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(FORTE_BUILD_TESTS "Build the Catch2 tests and register the CTest checks" ON)

add_subdirectory(external/JUCE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ForteCompileOptions)
include(ForteRealtimeRules)

add_subdirectory(tools/embed)
add_subdirectory(engine)
add_subdirectory(bridge)
add_subdirectory(plugin)
add_subdirectory(tools/forte-render)
add_subdirectory(tools/editor-snapshot)
add_subdirectory(tools/fitting/probes)

if(FORTE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
