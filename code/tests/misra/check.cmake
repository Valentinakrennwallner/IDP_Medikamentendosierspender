if(NOT DEFINED CPPCHECK_EXECUTABLE)
    find_program(CPPCHECK_EXECUTABLE NAMES cppcheck)
endif()

if(NOT CPPCHECK_EXECUTABLE)
    message(FATAL_ERROR
        "cppcheck is required for the MISRA C screening test. Install cppcheck with its MISRA addon.")
endif()

get_filename_component(CODE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
file(GLOB_RECURSE C_SOURCES LIST_DIRECTORIES false
    "${CODE_ROOT}/*.c"
    "${CODE_ROOT}/*.h"
)

set(CHECKED_SOURCES)
foreach(SOURCE IN LISTS C_SOURCES)
    file(TO_CMAKE_PATH "${SOURCE}" NORMALIZED_SOURCE)
    if(NOT NORMALIZED_SOURCE MATCHES "/(build|managed_components)/")
        list(APPEND CHECKED_SOURCES "${SOURCE}")
    endif()
endforeach()

if(NOT CHECKED_SOURCES)
    message(FATAL_ERROR "No C source or header files found under ${CODE_ROOT}.")
endif()

execute_process(
    COMMAND "${CPPCHECK_EXECUTABLE}"
        --addon=misra
        --error-exitcode=1
        --language=c
        --std=c11
        --enable=warning,style,performance,portability
        --inline-suppr
        --suppress=missingIncludeSystem
        ${CHECKED_SOURCES}
    RESULT_VARIABLE CPPCHECK_RESULT
    OUTPUT_VARIABLE CPPCHECK_OUTPUT
    ERROR_VARIABLE CPPCHECK_ERROR
)

if(NOT CPPCHECK_RESULT STREQUAL "0")
    message(FATAL_ERROR
        "MISRA C screening failed (cppcheck exit code: ${CPPCHECK_RESULT}).\n"
        "${CPPCHECK_OUTPUT}${CPPCHECK_ERROR}")
endif()

message(STATUS "MISRA C screening passed for ${CHECKED_SOURCES}")
