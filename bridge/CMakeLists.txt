# forte_bridge: header-only glue between JUCE buffers and forte::Engine.
#
# The plugin and forte-render both call forte::bridge::renderBlock(), so a block of audio and MIDI
# reaches the engine the same way in both, sample-accurately.
#
# It's header-only on purpose. JUCE modules compile into each consuming target, so a static
# library that used JUCE would duplicate JUCE's symbols in the plugin.

add_library(forte_bridge INTERFACE)
add_library(forte::bridge ALIAS forte_bridge)
target_include_directories(forte_bridge INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(forte_bridge INTERFACE forte::engine)
