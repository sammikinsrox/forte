# Forte VST3 and Standalone, and on Linux LV2 too (SPEC §1, §9.1).
#
# PLUGIN_MANUFACTURER_CODE and PLUGIN_CODE make up the VST3 class ID, and LV2URI is the LV2
# plugin's identity. Once a version ships, changing any of them makes hosts treat Forte as a
# different plugin, and saved projects lose it.

set(forte_formats VST3 Standalone)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND forte_formats LV2)
endif()

juce_add_plugin(Forte
    PRODUCT_NAME "Forte"
    COMPANY_NAME "Forte"
    DESCRIPTION "Physically modeled concert grand piano"
    VERSION "${PROJECT_VERSION}"
    PLUGIN_MANUFACTURER_CODE Frte
    PLUGIN_CODE Fgnd
    FORMATS ${forte_formats}
    LV2URI "https://github.com/sammikinsrox/forte"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Piano
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Forte PRIVATE
    src/LabParameters.cpp
    src/LabParameters.h
    src/MachineSettings.cpp
    src/MachineSettings.h
    src/PluginEditor.cpp
    src/PluginEditor.h
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/PresetManager.cpp
    src/PresetManager.h
    src/Presets.cpp
    src/Presets.h
    src/SoundParameters.cpp
    src/SoundParameters.h
    src/VoicingExport.cpp
    src/VoicingExport.h
    src/VoicingWorker.cpp
    src/VoicingWorker.h
    src/editor/ForteLookAndFeel.cpp
    src/editor/ForteLookAndFeel.h
    src/editor/LabView.cpp
    src/editor/LabView.h
    src/editor/PianoViews.cpp
    src/editor/PianoViews.h
    src/editor/Widgets.cpp
    src/editor/Widgets.h)

# The editor's fonts (SPEC §6.1), embedded unmodified (SIL OFL 1.1).
juce_add_binary_data(ForteAssets
    HEADER_NAME ForteAssets.h
    NAMESPACE ForteAssets
    SOURCES
        assets/fonts/Jost.ttf
        assets/fonts/Italiana-Regular.ttf)
set_target_properties(ForteAssets PROPERTIES FOLDER "Forte")

target_compile_definitions(Forte PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(Forte
    PRIVATE
        ForteAssets
        forte::engine
        forte::bridge
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        forte::compile_options)

set_target_properties(Forte PROPERTIES FOLDER "Forte")

# The git commit the plugin is built from, for the voicing export (SPEC §6.5). Checked on every
# build; the header only changes, and the plugin only rebuilds, when the commit does.
set(build_commit_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_target(ForteBuildCommit
    COMMAND "${CMAKE_COMMAND}" -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DOUTPUT=${build_commit_dir}/forte/BuildCommit.h
            -P "${PROJECT_SOURCE_DIR}/cmake/WriteBuildCommit.cmake"
    BYPRODUCTS "${build_commit_dir}/forte/BuildCommit.h"
    COMMENT "Recording the build's git commit"
    VERBATIM)
set_target_properties(ForteBuildCommit PROPERTIES FOLDER "Forte")
add_dependencies(Forte ForteBuildCommit)
target_include_directories(Forte PRIVATE "${build_commit_dir}")
