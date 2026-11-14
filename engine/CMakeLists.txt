# forte_engine: the one shared engine library (SPEC §9.1). The VST3, the Standalone,
# forte-render and the tests all link it, so forte-render's output is the plugin's output.
#
# Everything under engine/ is a DSP source: it is plain C++ with no JUCE, and the realtime rule
# (cmake/CheckRealtimeSources.cmake) rejects locks and blocking headers here.

configure_file(Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/forte/Version.h" @ONLY)

add_library(forte_engine STATIC
    include/forte/engine/Engine.h
    include/forte/engine/MidiEvent.h
    src/Engine.cpp
    src/NoteModel.cpp
    src/NoteModel.h
    src/PianoModel.cpp
    src/PianoModel.h
    src/ScopedFlushDenormals.h
    src/UnisonModes.cpp
    src/UnisonModes.h
    src/VelocityCurve.cpp
    src/VelocityCurve.h)
add_library(forte::engine ALIAS forte_engine)

target_include_directories(forte_engine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_features(forte_engine PUBLIC cxx_std_20)
target_link_libraries(forte_engine PRIVATE forte::compile_options forte::strict_warnings)
set_target_properties(forte_engine PROPERTIES FOLDER "Forte")

forte_enforce_realtime_rules(forte_engine DIRS "${CMAKE_CURRENT_SOURCE_DIR}")
