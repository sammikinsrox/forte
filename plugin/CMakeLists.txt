# Forte VST3 and Standalone (SPEC §1, §9.1).
#
# PLUGIN_MANUFACTURER_CODE and PLUGIN_CODE make up the VST3 class ID. Once a version ships,
# changing either one makes hosts treat Forte as a different plugin, and saved projects lose it.

juce_add_plugin(Forte
    PRODUCT_NAME "Forte"
    COMPANY_NAME "Forte"
    DESCRIPTION "Physically modeled concert grand piano"
    VERSION "${PROJECT_VERSION}"
    PLUGIN_MANUFACTURER_CODE Frte
    PLUGIN_CODE Fgnd
    FORMATS VST3 Standalone
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
    src/PluginProcessor.cpp
    src/PluginProcessor.h)

target_compile_definitions(Forte PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(Forte
    PRIVATE
        forte::engine
        forte::bridge
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        forte::compile_options)

set_target_properties(Forte PROPERTIES FOLDER "Forte")
