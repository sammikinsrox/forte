# forte_engine: the one shared engine library (SPEC §9.1). The VST3, the Standalone,
# forte-render and the tests all link it, so forte-render's output is the plugin's output.
#
# Everything under engine/ is a DSP source: it is plain C++ with no JUCE, and the realtime rule
# (cmake/CheckRealtimeSources.cmake) rejects locks and blocking headers here.

configure_file(Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/forte/Version.h" @ONLY)

add_library(forte_engine STATIC
    include/forte/engine/Engine.h
    include/forte/engine/MidiEvent.h
    include/forte/engine/VoicingLab.h
    src/EmbeddedFiles.h
    src/Engine.cpp
    src/Fft.h
    src/LabMapping.cpp
    src/LabMapping.h
    src/NoteModel.cpp
    src/NoteModel.h
    src/DecayFit.inc
    src/HammerFit.inc
    src/PianoModel.cpp
    src/PianoModel.h
    src/ScopedFlushDenormals.h
    src/Soundboard.cpp
    src/Soundboard.h
    src/SoundboardIrs.cpp
    src/SoundboardIrs.h
    src/SympatheticBank.cpp
    src/SympatheticBank.h
    src/UnisonModes.cpp
    src/UnisonModes.h
    src/VelocityCurve.cpp
    src/VelocityCurve.h)
add_library(forte::engine ALIAS forte_engine)

# The soundboard IRs (SPEC §3.5): data/ir/<perspective>-<region>.wav, built into the engine.
set(ir_files "")
foreach(perspective IN ITEMS microphones pianist)
    foreach(region IN ITEMS bass tenor middle treble)
        list(APPEND ir_files "${perspective}-${region}.wav=${PROJECT_SOURCE_DIR}/data/ir/${perspective}-${region}.wav")
    endforeach()
endforeach()
forte_embed_files(forte_engine
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedFiles.cpp"
    FILES ${ir_files})

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
