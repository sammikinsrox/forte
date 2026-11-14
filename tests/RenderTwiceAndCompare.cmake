# Tier 1 determinism check (SPEC §8.1): two renders of the same MIDI must be bit-identical.
#
# cmake -DFORTE_RENDER=<exe> -DMIDI=<file.mid> -DWORK_DIR=<dir> [-DSECOND_BLOCK_SIZE=<n>]
#       -P RenderTwiceAndCompare.cmake
#
# With SECOND_BLOCK_SIZE, the second render uses that block size instead of forte-render's
# default, which checks that output doesn't depend on block size (SPEC §7.4).

foreach(var FORTE_RENDER MIDI WORK_DIR)
    if(NOT ${var})
        message(FATAL_ERROR "RenderTwiceAndCompare: -D${var} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(args_a "")
set(args_b "")
if(SECOND_BLOCK_SIZE)
    set(args_b --block "${SECOND_BLOCK_SIZE}")
endif()

foreach(run a b)
    execute_process(
        COMMAND "${FORTE_RENDER}" "${MIDI}" "${WORK_DIR}/${run}.wav" ${args_${run}}
        RESULT_VARIABLE result)

    if(NOT result EQUAL 0)
        message(FATAL_ERROR "forte-render failed (exit ${result}) on run ${run}")
    endif()
endforeach()

file(SIZE "${WORK_DIR}/a.wav" size)
if(size LESS 100000)
    message(FATAL_ERROR "forte-render wrote a suspiciously small file (${size} bytes)")
endif()

# Two silent renders would match vacuously. The fixture plays from its first beat, so its
# opening must hold some non-zero sample bytes.
file(READ "${WORK_DIR}/a.wav" opening OFFSET 4096 LIMIT 65536 HEX)
if(NOT opening MATCHES "[1-9a-f]")
    message(FATAL_ERROR "forte-render's output is silent: ${WORK_DIR}/a.wav")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${WORK_DIR}/a.wav" "${WORK_DIR}/b.wav"
    RESULT_VARIABLE differ)

if(NOT differ EQUAL 0)
    message(FATAL_ERROR "Two renders of ${MIDI} differ: ${WORK_DIR}/a.wav vs b.wav")
endif()

message(STATUS "Two renders are bit-identical (${size} bytes).")
