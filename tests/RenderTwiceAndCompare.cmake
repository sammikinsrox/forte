# Tier 1 determinism check (SPEC §8.1): two renders of the same MIDI must be bit-identical.
#
# cmake -DFORTE_RENDER=<exe> -DMIDI=<file.mid> -DWORK_DIR=<dir> -P RenderTwiceAndCompare.cmake

foreach(var FORTE_RENDER MIDI WORK_DIR)
    if(NOT ${var})
        message(FATAL_ERROR "RenderTwiceAndCompare: -D${var} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

foreach(run a b)
    execute_process(
        COMMAND "${FORTE_RENDER}" "${MIDI}" "${WORK_DIR}/${run}.wav"
        RESULT_VARIABLE result)

    if(NOT result EQUAL 0)
        message(FATAL_ERROR "forte-render failed (exit ${result}) on run ${run}")
    endif()
endforeach()

file(SIZE "${WORK_DIR}/a.wav" size)
if(size LESS 1000)
    message(FATAL_ERROR "forte-render wrote a suspiciously small file (${size} bytes)")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${WORK_DIR}/a.wav" "${WORK_DIR}/b.wav"
    RESULT_VARIABLE differ)

if(NOT differ EQUAL 0)
    message(FATAL_ERROR "Two renders of ${MIDI} differ: ${WORK_DIR}/a.wav vs b.wav")
endif()

message(STATUS "Two renders are bit-identical (${size} bytes).")
