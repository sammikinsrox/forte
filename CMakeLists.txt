cmake_minimum_required(VERSION 3.25)

# The one version number for every Forte binary (SPEC §9.1).
project(Forte VERSION 1.0.0 LANGUAGES C CXX)

if(NOT MSVC)
    message(FATAL_ERROR "Forte v1 builds with MSVC (Visual Studio 2026) only. See SPEC §9.1.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

if(FORTE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
