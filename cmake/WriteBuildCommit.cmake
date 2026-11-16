# Writes the git commit the build comes from to a header, at build time, and only when it changes
# (so an unchanged commit rebuilds nothing). The voicing lab's export records it (SPEC §6.5), so
# tools/fitting can tell which committed voicing the file's 50s meant.
#
# Usage: cmake -DSOURCE_DIR=<repo> -DOUTPUT=<header> -P WriteBuildCommit.cmake

execute_process(
    COMMAND git describe --always --dirty --abbrev=12
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE result)

if(NOT result EQUAL 0 OR commit STREQUAL "")
    set(commit "unknown")
endif()

set(content "// Generated at build time by cmake/WriteBuildCommit.cmake. Do not edit.
#pragma once

namespace forte
{
inline constexpr const char* buildCommit = \"${commit}\";
} // namespace forte
")

if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" existing)
else()
    set(existing "")
endif()

if(NOT existing STREQUAL content)
    file(WRITE "${OUTPUT}" "${content}")
endif()
