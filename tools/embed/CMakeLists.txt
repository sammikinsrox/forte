# forte-embed: a build-time tool that turns data files into a C++ source (see Embed.cpp).
# The engine uses it to embed the soundboard IRs (SPEC §3.5).

add_executable(forte_embed Embed.cpp)
target_compile_features(forte_embed PRIVATE cxx_std_20)
target_link_libraries(forte_embed PRIVATE forte::compile_options forte::strict_warnings)
set_target_properties(forte_embed PROPERTIES FOLDER "Tools" OUTPUT_NAME "forte-embed")

# forte_embed_files(<target> OUTPUT <file.cpp> FILES <name>=<path>...)
#
# Generates <file.cpp> from the files and adds it to <target>. It regenerates when a file changes.
function(forte_embed_files target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "OUTPUT" "FILES")

    set(paths "")
    foreach(entry IN LISTS arg_FILES)
        string(FIND "${entry}" "=" equals)
        math(EXPR path_start "${equals} + 1")
        string(SUBSTRING "${entry}" ${path_start} -1 path)
        list(APPEND paths "${path}")
    endforeach()

    add_custom_command(
        OUTPUT "${arg_OUTPUT}"
        COMMAND forte_embed "${arg_OUTPUT}" ${arg_FILES}
        DEPENDS forte_embed ${paths}
        COMMENT "Embedding data files in ${target}"
        VERBATIM)

    target_sources(${target} PRIVATE "${arg_OUTPUT}")
endfunction()
