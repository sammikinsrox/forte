# Per-push checks (SPEC §9.3), all run by `ctest`:
#   - Catch2 unit tests, including the audio-thread allocation guard;
#   - the realtime build rule, and a self-test that it rejects a violation;
#   - the Tier 1 engine checks (SPEC §8.1): Catch2 tests tagged [tier1], plus forte-render
#     determinism and block-size independence;
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
    support/EngineRender.h
    AudioThreadGuardTests.cpp
    BridgeTests.cpp
    ControlsTests.cpp
    EngineTests.cpp
    InstructionSetTests.cpp
    MidiTests.cpp
    NoteModelTests.cpp
    PresetTests.cpp
    RoomTests.cpp
    SoundboardTests.cpp
    SympatheticBankTests.cpp
    Tier1Tests.cpp
    TunerTests.cpp
    UnisonModesTests.cpp
    VelocityCurveTests.cpp
    VoicingLabTests.cpp
    # The plugin's voicing export and presets, which are plain JUCE core and the engine.
    ${PROJECT_SOURCE_DIR}/plugin/src/Presets.cpp
    ${PROJECT_SOURCE_DIR}/plugin/src/VoicingExport.cpp)
add_dependencies(forte_tests ForteBuildCommit)

# The engine's own headers too, so DSP parts (the velocity curve, the piano model) can be
# tested directly.
target_include_directories(forte_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/engine/src"
    "${PROJECT_SOURCE_DIR}/plugin/src"
    "${PROJECT_BINARY_DIR}/plugin/generated")

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

# Catch2 tests tagged [tier1] are Tier 1 engine checks (SPEC §8.1) and get their own label.
catch_discover_tests(forte_tests
    TEST_SPEC "~[tier1]"
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "unit: "
    PROPERTIES LABELS "unit")

catch_discover_tests(forte_tests
    TEST_SPEC "[tier1]"
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "tier1: "
    PROPERTIES LABELS "tier1")

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

# SPEC §7.4: offline and real-time renders use different block sizes and must still match.
add_test(NAME "tier1: render is independent of block size"
    COMMAND "${CMAKE_COMMAND}"
        "-DFORTE_RENDER=$<TARGET_FILE:forte_render>"
        "-DMIDI=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/smoke.mid"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/render-block-size/$<CONFIG>"
        "-DSECOND_BLOCK_SIZE=37"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/RenderTwiceAndCompare.cmake")

set_tests_properties(
    "tier1: render is deterministic"
    "tier1: render is independent of block size"
    PROPERTIES LABELS "tier1")

# --- pluginval ---------------------------------------------------------------------------------

# pluginval also runs Steinberg's VST3 validator when given its path. JUCE's bundled SDK
# doesn't include the validator, so tools/get-vst3-validator.ps1 builds it from the full SDK.
# CI sets FORTE_REQUIRE_CHECK_TOOLS=ON, so a missing tool fails the configure step instead of
# quietly dropping a check.

option(FORTE_REQUIRE_CHECK_TOOLS "Fail if pluginval or the VST3 validator is missing" OFF)

find_program(FORTE_PLUGINVAL_EXE pluginval
    HINTS "${PROJECT_SOURCE_DIR}/.tools/pluginval"
    DOC "pluginval executable (tools/get-pluginval.ps1 downloads the pinned version)")

find_program(FORTE_VST3_VALIDATOR_EXE validator
    HINTS "${PROJECT_SOURCE_DIR}/.tools/vst3-validator"
    NO_DEFAULT_PATH
    DOC "Steinberg VST3 validator (tools/get-vst3-validator.ps1 builds the pinned version)")

set(missing_check_tools "")
if(NOT FORTE_PLUGINVAL_EXE)
    list(APPEND missing_check_tools "pluginval (run tools/get-pluginval.ps1)")
endif()
if(NOT FORTE_VST3_VALIDATOR_EXE)
    list(APPEND missing_check_tools "VST3 validator (run tools/get-vst3-validator.ps1)")
endif()

if(missing_check_tools)
    list(JOIN missing_check_tools ", " missing_list)
    if(FORTE_REQUIRE_CHECK_TOOLS)
        message(FATAL_ERROR "Missing check tools: ${missing_list}")
    endif()
    message(WARNING "Missing check tools: ${missing_list}. Then reconfigure.")
endif()

if(FORTE_PLUGINVAL_EXE)
    set(vst3_validator_args "")
    if(FORTE_VST3_VALIDATOR_EXE)
        set(vst3_validator_args --vst3validator "${FORTE_VST3_VALIDATOR_EXE}")
    endif()

    add_test(NAME "pluginval: VST3 at strictness 10"
        COMMAND "${FORTE_PLUGINVAL_EXE}"
            --strictness-level 10
            --validate-in-process
            --timeout-ms 600000
            ${vst3_validator_args}
            --output-dir "${CMAKE_CURRENT_BINARY_DIR}/pluginval/$<CONFIG>"
            # JUCE stores the bundle path as a generator expression, hence GENEX_EVAL.
            --validate "$<GENEX_EVAL:$<TARGET_PROPERTY:Forte_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
    set_tests_properties("pluginval: VST3 at strictness 10" PROPERTIES
        LABELS "pluginval"
        TIMEOUT 900
        # pluginval flips every parameter, Export voicing too: don't overwrite the user's
        # Documents/Forte/voicing.json (SPEC §6.5).
        ENVIRONMENT "FORTE_NO_VOICING_EXPORT=1"
        # Belt and braces: pluginval reports a skipped validator as success.
        FAIL_REGULAR_EXPRESSION "Skipping vst3 validator")
endif()
