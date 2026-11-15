# forte-string-probe: note models' raw bridge force for hammer and felt variants, without the
# soundboard. The felt fit (tools/fitting/fit_hammer.py) drives it.

add_executable(forte_string_probe StringProbe.cpp)

# The note model's own headers: the probe builds NoteModels directly.
target_include_directories(forte_string_probe PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")

target_link_libraries(forte_string_probe PRIVATE forte::engine forte::compile_options)

set_target_properties(forte_string_probe PROPERTIES FOLDER "Tools" OUTPUT_NAME "forte-string-probe")
