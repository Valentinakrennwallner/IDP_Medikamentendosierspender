# MISRA C screening with the cppcheck MISRA addon.
#
# The ESP32 firmware, the gateway, and every host test executable are separate
# programs that are never linked together, so each is analysed on its own.
# Analysing them together reports false duplicates (rules 5.6, 5.7, 8.6).
# All files under code/ must belong to a program; test and mock code is
# screened like production code.

if(NOT DEFINED CPPCHECK_EXECUTABLE)
    find_program(CPPCHECK_EXECUTABLE NAMES cppcheck)
endif()

if(NOT CPPCHECK_EXECUTABLE)
    message(FATAL_ERROR
        "cppcheck is required for the MISRA C screening test. Install cppcheck with its MISRA addon.")
endif()

get_filename_component(CODE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# Skip only the advisory rules with a record in docs/misra-deviations.md.
include("${CMAKE_CURRENT_LIST_DIR}/deviated_rules.cmake")
set(DEVIATION_ARGUMENTS)
foreach(RULE IN LISTS MISRA_DEVIATED_RULES)
    list(APPEND DEVIATION_ARGUMENTS "--suppress=misra-c2012-${RULE}")
endforeach()

# Collect the C sources and headers below the given directories of code/,
# ignoring build output.
function(collect_sources RESULT)
    set(FOUND)
    foreach(DIRECTORY IN LISTS ARGN)
        file(GLOB_RECURSE FILES LIST_DIRECTORIES false
            "${CODE_ROOT}/${DIRECTORY}/*.c"
            "${CODE_ROOT}/${DIRECTORY}/*.h"
        )
        foreach(FILE IN LISTS FILES)
            get_filename_component(FILE "${FILE}" ABSOLUTE)
            file(TO_CMAKE_PATH "${FILE}" NORMALIZED_FILE)
            if(NOT NORMALIZED_FILE MATCHES "/(build|managed_components)/")
                list(APPEND FOUND "${FILE}")
            endif()
        endforeach()
    endforeach()
    set(${RESULT} "${FOUND}" PARENT_SCOPE)
endfunction()

# Project headers must be resolvable, otherwise cppcheck reports unknown types
# and constants as misra-config findings and implicit declarations (rule 17.3).
function(include_arguments RESULT)
    set(ARGUMENTS)
    foreach(FILE IN LISTS ARGN)
        if(FILE MATCHES "\\.h$")
            get_filename_component(HEADER_DIRECTORY "${FILE}" DIRECTORY)
            list(APPEND ARGUMENTS "-I${HEADER_DIRECTORY}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES ARGUMENTS)
    set(${RESULT} "${ARGUMENTS}" PARENT_SCOPE)
endfunction()

collect_sources(ALL_SOURCES .)
if(NOT ALL_SOURCES)
    message(FATAL_ERROR "No C source or header files found under ${CODE_ROOT}.")
endif()
include_arguments(ALL_INCLUDES ${ALL_SOURCES})

# The firmware and the gateway each compile protocol/ into their program.
collect_sources(FIRMWARE_SOURCES esp32/components esp32/main protocol)
collect_sources(GATEWAY_SOURCES raspberry-pi/src protocol)
include_arguments(FIRMWARE_INCLUDES ${FIRMWARE_SOURCES})
include_arguments(GATEWAY_INCLUDES ${GATEWAY_SOURCES})
set(PROGRAM_NAMES firmware gateway)
set(PROGRAM_firmware_SOURCES ${FIRMWARE_SOURCES})
set(PROGRAM_firmware_INCLUDES ${FIRMWARE_INCLUDES})
set(PROGRAM_gateway_SOURCES ${GATEWAY_SOURCES})
set(PROGRAM_gateway_INCLUDES ${GATEWAY_INCLUDES})

# Each host test is its own program: the test source plus the headers in its
# test directory (mocks), with every project header directory available.
set(ASSIGNED_SOURCES ${FIRMWARE_SOURCES} ${GATEWAY_SOURCES})
file(GLOB_RECURSE TEST_SOURCES LIST_DIRECTORIES false "${CODE_ROOT}/*/test/*_test.c")
foreach(TEST_SOURCE IN LISTS TEST_SOURCES)
    get_filename_component(TEST_SOURCE "${TEST_SOURCE}" ABSOLUTE)
    file(TO_CMAKE_PATH "${TEST_SOURCE}" NORMALIZED_TEST)
    if(NOT NORMALIZED_TEST MATCHES "/(build|managed_components)/")
        get_filename_component(TEST_DIRECTORY "${TEST_SOURCE}" DIRECTORY)
        file(RELATIVE_PATH TEST_RELATIVE_DIRECTORY "${CODE_ROOT}" "${TEST_DIRECTORY}")
        collect_sources(TEST_SUPPORT "${TEST_RELATIVE_DIRECTORY}")
        list(FILTER TEST_SUPPORT INCLUDE REGEX "\\.h$")
        get_filename_component(TEST_NAME "${TEST_SOURCE}" NAME_WE)
        set(PROGRAM_${TEST_NAME}_SOURCES "${TEST_SOURCE}" ${TEST_SUPPORT})
        set(PROGRAM_${TEST_NAME}_INCLUDES ${ALL_INCLUDES})
        list(APPEND PROGRAM_NAMES ${TEST_NAME})
        list(APPEND ASSIGNED_SOURCES ${TEST_SOURCE} ${TEST_SUPPORT})
    endif()
endforeach()

# A file in no program would escape the screening.
foreach(SOURCE IN LISTS ALL_SOURCES)
    list(FIND ASSIGNED_SOURCES "${SOURCE}" ASSIGNED_INDEX)
    if(ASSIGNED_INDEX LESS 0)
        message(FATAL_ERROR
            "${SOURCE} belongs to no program in check.cmake; add its directory to "
            "a program so that it is screened.")
    endif()
endforeach()

set(FAILED_PROGRAMS)
set(REPORT)
foreach(PROGRAM IN LISTS PROGRAM_NAMES)
    execute_process(
        COMMAND "${CPPCHECK_EXECUTABLE}"
            --quiet
            --addon=misra
            --error-exitcode=1
            --language=c
            --std=c11
            --enable=warning,style,performance,portability
            --inline-suppr
            --suppress=missingIncludeSystem
            ${DEVIATION_ARGUMENTS}
            ${PROGRAM_${PROGRAM}_INCLUDES}
            ${PROGRAM_${PROGRAM}_SOURCES}
        RESULT_VARIABLE CPPCHECK_RESULT
        OUTPUT_VARIABLE CPPCHECK_OUTPUT
        ERROR_VARIABLE CPPCHECK_ERROR
    )
    if(NOT CPPCHECK_RESULT STREQUAL "0")
        list(APPEND FAILED_PROGRAMS "${PROGRAM}")
        string(APPEND REPORT
            "\n== ${PROGRAM} (cppcheck exit code ${CPPCHECK_RESULT})\n"
            "${CPPCHECK_OUTPUT}${CPPCHECK_ERROR}")
    endif()
endforeach()

if(FAILED_PROGRAMS)
    message(FATAL_ERROR
        "MISRA C screening failed for: ${FAILED_PROGRAMS}\n${REPORT}")
endif()

list(LENGTH PROGRAM_NAMES PROGRAM_COUNT)
message(STATUS "MISRA C screening passed for ${PROGRAM_COUNT} programs")
