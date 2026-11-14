# forte-render: MIDI file in, WAV out, through the real engine (SPEC §8.4).

juce_add_console_app(forte_render
    PRODUCT_NAME "forte-render"
    VERSION "${PROJECT_VERSION}")

target_sources(forte_render PRIVATE src/Main.cpp)

target_compile_definitions(forte_render PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(forte_render
    PRIVATE
        forte::engine
        forte::bridge
        forte::compile_options
        juce::juce_audio_formats)

set_target_properties(forte_render PROPERTIES FOLDER "Tools" OUTPUT_NAME "forte-render")
