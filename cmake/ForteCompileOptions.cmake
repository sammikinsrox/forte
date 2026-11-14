# Compiler settings shared by every Forte target (SPEC §9.1).
#
# Release uses CMake's MSVC default of /O2 (JUCE's juce_recommended_config_flags would switch it to
# /Ox, so Forte targets don't link that). /fp:fast applies everywhere. Flush-to-zero and
# denormals-are-zero are set inside the engine at run time, not here.

add_library(forte_compile_options INTERFACE)
add_library(forte::compile_options ALIAS forte_compile_options)

target_compile_options(forte_compile_options INTERFACE
    /W4
    /permissive-
    /utf-8
    /fp:fast
    /EHsc
    /MP
    /Zc:__cplusplus
    $<$<CONFIG:Debug>:/Od /Zi>
    $<$<CONFIG:Release>:/O2>)

target_link_options(forte_compile_options INTERFACE $<$<CONFIG:Debug>:/DEBUG>)

# Forte's own code (not JUCE's) also treats warnings as errors.
add_library(forte_strict_warnings INTERFACE)
add_library(forte::strict_warnings ALIAS forte_strict_warnings)
target_compile_options(forte_strict_warnings INTERFACE /WX)
