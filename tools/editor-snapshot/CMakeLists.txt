# forte-editor-snapshot: draws the real editor offscreen, every view in both themes, to PNGs, so
# the editor can be looked at without a host. A development tool; it never ships.

juce_add_console_app(forte_editor_snapshot
    PRODUCT_NAME "forte-editor-snapshot"
    VERSION "${PROJECT_VERSION}")

file(GLOB plugin_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/plugin/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/plugin/src/editor/*.cpp")

target_sources(forte_editor_snapshot PRIVATE src/Main.cpp ${plugin_sources})
add_dependencies(forte_editor_snapshot ForteBuildCommit)

target_include_directories(forte_editor_snapshot PRIVATE
    "${PROJECT_SOURCE_DIR}/plugin/src"
    "${PROJECT_BINARY_DIR}/plugin/generated")

target_compile_definitions(forte_editor_snapshot PRIVATE
    JucePlugin_Name="Forte"
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(forte_editor_snapshot
    PRIVATE
        ForteAssets
        forte::engine
        forte::bridge
        forte::compile_options
        juce::juce_audio_utils
        juce::juce_audio_processors)

set_target_properties(forte_editor_snapshot PROPERTIES FOLDER "Tools" OUTPUT_NAME "forte-editor-snapshot")
