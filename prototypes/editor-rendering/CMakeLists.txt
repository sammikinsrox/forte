# PROTOTYPE (throwaway). Answers: should Forte's editor be a WebView page or native JUCE drawing?
# Builds the same toy instrument twice: "Forte Web Proto" (WebView2 editor) and "Forte Native Proto" (JUCE editor).
# Build: build.bat
cmake_minimum_required(VERSION 3.22)
project(ForteEditorRenderingProto VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 20)
set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/deps/nuget")
add_subdirectory(JUCE)

juce_add_binary_data(ForteProtoAssets SOURCES
    web/index.html
    JUCE/modules/juce_gui_extra/native/typescript/webview-interop/dist/index.js
    deps/fonts/Italiana-Regular.ttf
    deps/fonts/Jost.ttf)

function(forte_proto target product code editor_define)
    juce_add_plugin(${target}
        COMPANY_NAME "Forte Prototype"
        PRODUCT_NAME "${product}"
        PLUGIN_MANUFACTURER_CODE Frtp
        PLUGIN_CODE ${code}
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_WEBVIEW2 TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        FORMATS VST3 Standalone)
    target_sources(${target} PRIVATE Source/Processor.cpp Source/WebEditor.cpp Source/NativeEditor.cpp)
    target_compile_definitions(${target} PUBLIC
        ${editor_define}=1
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(${target} PRIVATE ForteProtoAssets
        juce::juce_audio_utils juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endfunction()

forte_proto(ForteWebProto    "Forte Web Proto"    Fwpb FORTE_EDITOR_WEB)
forte_proto(ForteNativeProto "Forte Native Proto" Fnpb FORTE_EDITOR_NATIVE)
