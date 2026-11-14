# forte_enforce_realtime_rules(<target> DIRS <dir>...)
#
# Runs CheckRealtimeSources.cmake over <dir>... before <target> builds, so a DSP source that
# includes <mutex> (and the like) fails the build itself. The check reruns whenever a file in
# those directories changes, or one is added or removed.

set(FORTE_REALTIME_CHECK_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/CheckRealtimeSources.cmake")

function(forte_enforce_realtime_rules target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "DIRS")
    if(NOT arg_DIRS)
        message(FATAL_ERROR "forte_enforce_realtime_rules: DIRS is required")
    endif()

    set(watched "")
    foreach(dir IN LISTS arg_DIRS)
        file(GLOB_RECURSE dir_sources CONFIGURE_DEPENDS
            "${dir}/*.h" "${dir}/*.hpp" "${dir}/*.inl" "${dir}/*.cpp" "${dir}/*.cc" "${dir}/*.cxx")
        list(APPEND watched ${dir_sources})
    endforeach()

    # '|' separates the directories, so the list survives being passed as one argument.
    list(JOIN arg_DIRS "|" dirs_arg)

    set(stamp "${CMAKE_CURRENT_BINARY_DIR}/${target}-realtime-rules.stamp")
    add_custom_command(
        OUTPUT "${stamp}"
        COMMAND "${CMAKE_COMMAND}" "-DFORTE_RT_DIRS=${dirs_arg}" -P "${FORTE_REALTIME_CHECK_SCRIPT}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${stamp}"
        DEPENDS ${watched} "${FORTE_REALTIME_CHECK_SCRIPT}"
        COMMENT "Checking realtime rules for ${target}"
        VERBATIM)

    add_custom_target(${target}_realtime_rules DEPENDS "${stamp}")
    set_target_properties(${target}_realtime_rules PROPERTIES FOLDER "Checks")
    add_dependencies(${target} ${target}_realtime_rules)
endfunction()
