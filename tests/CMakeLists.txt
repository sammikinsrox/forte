# Per-push checks (SPEC §9.3), all run by `ctest`:
#   - Catch2 unit tests, including the audio-thread allocation guard;
#   - the realtime build rule, and a self-test that it rejects a violation;
#   - forte-render determinism (Tier 1, SPEC §8.1);
#   - pluginval at strictness 10 on the VST3 (when pluginval is available).

include(FetchContent)

FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.1.tar.gz
    URL_HASH SHA256=d16bd1b8b2364918bd472cb216394acb93bde6784890df0d91bccfb3f28d777d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)

foreach(catch_target IN ITEMS Catch2 Catch2WithMain)
    set_target_properties(${catch_target} PROPERTIES FOLDER "External")
endforeach()

# --- Unit tests --------------------------------------------------------------------------------

juce_add_console_app(forte_tests PRODUCT_NAME "forte-tests")

target_sources(forte_tests PRIVATE
    support/AudioThreadGuard.cpp
    support/AudioThreadGuard.h
    AudioThreadGuardTests.cpp
    BridgeTests.cpp
    EngineTests.cpp)

target_include_directories(forte_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(forte_tests PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(forte_tests PRIVATE
    forte::engine
    forte::bridge
    forte::compile_options
    juce::juce_audio_basics
    Catch2::Catch2WithMain)

set_target_properties(forte_tests PROPERTIES FOLDER "Tests" OUTPUT_NAME "forte-tests")

catch_discover_tests(forte_tests
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "unit: "
    PROPERTIES LABELS "unit")

# --- Realtime build rule -----------------------------------------------------------------------

add_test(NAME "realtime-rule: engine sources are clean"
    COMMAND "${CMAKE_COMMAND}" "-DFORTE_RT_DIRS=${PROJECT_SOURCE_DIR}/engine" -P "${FORTE_REALTIME_CHECK_SCRIPT}")

add_test(NAME "realtime-rule: ignores comments"
    COMMAND "${CMAKE_COMMAND}" "-DFORTE_RT_DIRS=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/realtime-clean" -P "${FORTE_REALTIME_CHECK_SCRIPT}")

add_test(NAME "realtime-rule: rejects <mutex>"
    COMMAND "${CMAKE_COMMAND}" "-DFORTE_RT_DIRS=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/realtime-violation" -P "${FORTE_REALTIME_CHECK_SCRIPT}")
set_tests_properties("realtime-rule: rejects <mutex>" PROPERTIES WILL_FAIL TRUE)

set_tests_properties(
    "realtime-rule: engine sources are clean"
    "realtime-rule: ignores comments"
    "realtime-rule: rejects <mutex>"
    PROPERTIES LABELS "realtime-rule")

# --- Tier 1 through forte-render ---------------------------------------------------------------

add_test(NAME "tier1: render is deterministic"
    COMMAND "${CMAKE_COMMAND}"
        "-DFORTE_RENDER=$<TARGET_FILE:forte_render>"
        "-DMIDI=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/smoke.mid"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/render-determinism/$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/RenderTwiceAndCompare.cmake")
set_tests_properties("tier1: render is deterministic" PROPERTIES LABELS "tier1")

# --- pluginval ---------------------------------------------------------------------------------

find_program(FORTE_PLUGINVAL_EXE pluginval
    HINTS "${PROJECT_SOURCE_DIR}/.tools/pluginval"
    DOC "pluginval executable (tools/get-pluginval.ps1 downloads the pinned version)")

if(FORTE_PLUGINVAL_EXE)
    add_test(NAME "pluginval: VST3 at strictness 10"
        COMMAND "${FORTE_PLUGINVAL_EXE}"
            --strictness-level 10
            --validate-in-process
            --timeout-ms 600000
            --output-dir "${CMAKE_CURRENT_BINARY_DIR}/pluginval/$<CONFIG>"
            # JUCE stores the bundle path as a generator expression, hence GENEX_EVAL.
            --validate "$<GENEX_EVAL:$<TARGET_PROPERTY:Forte_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
    set_tests_properties("pluginval: VST3 at strictness 10" PROPERTIES LABELS "pluginval" TIMEOUT 900)
else()
    message(WARNING "pluginval not found, so the pluginval test is skipped. "
                    "Run tools/get-pluginval.ps1, then reconfigure.")
endif()
