# Compiler settings shared by every Forte target (SPEC §9.1).
#
# MSVC (Windows): Release uses CMake's MSVC default of /O2 (JUCE's juce_recommended_config_flags
# would switch it to /Ox, so Forte targets don't link that). /fp:fast applies everywhere.
#
# GCC and Clang (Linux): -O2 for Release, like /O2. No -ffast-math: it assumes there are no NaNs or
# infinities, and the tests check for them. -ffp-contract=off keeps the compiler from fusing
# multiply-adds the source doesn't ask for (Clang fuses by default where FMA exists, as it does in
# the AVX2 kernels), so renders stay as close as they can to the Windows build's. They still
# differ in the last bits: the maths libraries round differently.
#
# Flush-to-zero and denormals-are-zero are set inside the engine at run time, not here.

add_library(forte_compile_options INTERFACE)
add_library(forte::compile_options ALIAS forte_compile_options)

if(MSVC)
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
else()
    target_compile_options(forte_compile_options INTERFACE
        -Wall
        -Wextra
        -fno-math-errno
        -ffp-contract=off
        $<$<CONFIG:Debug>:-O0 -g>
        $<$<CONFIG:Release>:-O2 -ffunction-sections -fdata-sections>)

    # Drop code nothing calls, as MSVC's /OPT:REF does by default on Windows.
    target_link_options(forte_compile_options INTERFACE $<$<CONFIG:Release>:-Wl,--gc-sections>)
endif()

# Audio codecs JUCE builds by default that Forte never reads or writes (it writes WAV only): off,
# so they aren't in the binaries (THIRD-PARTY.md). MSVC's linker dropped them anyway; GCC's didn't.
target_compile_definitions(forte_compile_options INTERFACE
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_OPUS=0
    JUCE_USE_MP3AUDIOFORMAT=0)

# Forte's own code (not JUCE's) also treats warnings as errors.
add_library(forte_strict_warnings INTERFACE)
add_library(forte::strict_warnings ALIAS forte_strict_warnings)
if(MSVC)
    target_compile_options(forte_strict_warnings INTERFACE /WX)
else()
    target_compile_options(forte_strict_warnings INTERFACE -Werror)
endif()

# The compiler flag for a source built with AVX2 and FMA (SPEC §9.2: the hot loops' AVX2 set).
if(MSVC)
    set(FORTE_AVX2_FLAGS "/arch:AVX2")
else()
    set(FORTE_AVX2_FLAGS "-mavx2;-mfma")
endif()
