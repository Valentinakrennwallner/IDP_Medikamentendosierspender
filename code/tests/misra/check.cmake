# MISRA C screening with the cppcheck MISRA addon.
#
# The ESP32 firmware, the gateway, and every host test executable are separate
# programs that are never linked together, so each is analysed on its own.
# Analysing them together reports false duplicates (rules 5.6, 5.7, 8.6).
# All files under code/ must belong to a program; test and mock code is
# screened like production code.
# MISRA C screening with cppcheck and its MISRA addon (one device per run).
#
# Usage (registered as CTest "misra_c_screening" by both device builds):
#   cmake -DDEVICE=raspberry-pi|esp32
#         -DCOMPILE_COMMANDS=<build>/compile_commands.json
#         -DREPORT_DIR=<build>
#         [-DCPPCHECK_EXECUTABLE=cppcheck] [-DEXCLUDE_HEADERS_DIR=<SDK dir>]
#         -P check.cmake
#
# - Only production code of the selected device and the shared protocol is
#   checked (test code and mocks are excluded). Include paths and defines are
#   taken from the build's compile_commands.json.
# - Findings are classified with rule_categories.txt (rule numbers and
#   categories only). The test FAILS on Mandatory findings, as required by
#   the assignment. Required and Advisory findings are listed and written to
#   <REPORT_DIR>/misra_report_<DEVICE>.txt; they must be fixed or documented
#   as deviations (see docs/code-style.md).
# - Rule 17.3 (implicit function declarations) is not reported by cppcheck
#   here: without all SDK/library headers cppcheck reports false positives.
#   The rule is enforced by the compiler instead: GCC 14 and later reject
#   implicit function declarations as an error (ESP32: GCC 14.2.0,
#   Raspberry Pi: GCC 15.2.0, see README).
# - misra-config messages (unknown identifiers from headers cppcheck does not
#   parse) are tool configuration notes, not rule violations; they are
#   counted in the report but do not fail the test.
# The cppcheck MISRA addon is a preliminary screen covering a subset of the
# rules; it is not proof of MISRA C compliance.

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
foreach(REQUIRED_VARIABLE IN ITEMS DEVICE COMPILE_COMMANDS REPORT_DIR)
    if(NOT DEFINED ${REQUIRED_VARIABLE})
        message(FATAL_ERROR "check.cmake: ${REQUIRED_VARIABLE} is not set.")
    endif()
endforeach()
if(NOT EXISTS "${COMPILE_COMMANDS}")
    message(FATAL_ERROR
        "check.cmake: ${COMPILE_COMMANDS} not found. Build the ${DEVICE} project first.")
endif()

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
if(DEVICE STREQUAL "raspberry-pi")
    set(FILE_FILTERS "*/raspberry-pi/src/*" "*/protocol/src/*")
elseif(DEVICE STREQUAL "esp32")
    set(FILE_FILTERS "*/esp32/components/*" "*/esp32/main/*" "*/protocol/src/*")
else()
    message(FATAL_ERROR "check.cmake: unknown DEVICE '${DEVICE}' (raspberry-pi or esp32).")
endif()

# Locate the MISRA addon script of the installed cppcheck. Newer cppcheck
# versions resolve a relative "script" entry relative to the JSON file, so the
# absolute path is passed. Override with -DMISRA_SCRIPT=<path> if needed.
if(NOT IS_ABSOLUTE "${CPPCHECK_EXECUTABLE}")
    find_program(CPPCHECK_PATH NAMES "${CPPCHECK_EXECUTABLE}")
    if(NOT CPPCHECK_PATH)
        message(FATAL_ERROR "cppcheck ('${CPPCHECK_EXECUTABLE}') was not found on PATH.")
    endif()
    set(CPPCHECK_EXECUTABLE "${CPPCHECK_PATH}")
endif()
if(NOT DEFINED MISRA_SCRIPT)
    get_filename_component(CPPCHECK_DIR "${CPPCHECK_EXECUTABLE}" REALPATH)
    get_filename_component(CPPCHECK_DIR "${CPPCHECK_DIR}" DIRECTORY)
    file(GLOB SYSTEM_ADDON_DIRS LIST_DIRECTORIES true "/usr/lib/*/cppcheck/addons")
    find_file(MISRA_SCRIPT misra.py
        PATHS
            "${CPPCHECK_DIR}/addons"
            "${CPPCHECK_DIR}/../share/cppcheck/addons"
            "${CPPCHECK_DIR}/../lib/cppcheck/addons"
            ${SYSTEM_ADDON_DIRS}
            "/usr/share/cppcheck/addons"
        NO_DEFAULT_PATH)
endif()
if(NOT MISRA_SCRIPT OR NOT EXISTS "${MISRA_SCRIPT}")
    message(FATAL_ERROR
        "MISRA addon (misra.py) not found next to ${CPPCHECK_EXECUTABLE}. "
        "Pass -DMISRA_SCRIPT=<path to misra.py>.")
endif()

# The addon receives the rule category file through a JSON addon description.
set(RULE_CATEGORIES "${CMAKE_CURRENT_LIST_DIR}/rule_categories.txt")
set(ADDON_JSON "${REPORT_DIR}/misra_addon_${DEVICE}.json")
file(WRITE "${ADDON_JSON}"
    "{\"script\": \"${MISRA_SCRIPT}\", \"args\": [\"--rule-texts=${RULE_CATEGORIES}\"]}\n")

set(CPPCHECK_ARGUMENTS
    "--project=${COMPILE_COMMANDS}"
    "--addon=${ADDON_JSON}"
    --language=c
    --std=c11
    --enable=style
    --inline-suppr
    --quiet
    "--template={file}:{line}:{id}:{message}"
    --suppress=missingIncludeSystem
    --suppress=missingInclude
    --suppress=toomanyconfigs
    --suppress=unmatchedSuppression
    --suppress=misra-c2012-17.3
)
foreach(FILTER IN LISTS FILE_FILTERS)
    list(APPEND CPPCHECK_ARGUMENTS "--file-filter=${FILTER}")
endforeach()
# Generated headers in the build directory (e.g. ESP-IDF sdkconfig.h with
# about 1000 CONFIG_ macros) are not project code.
string(REPLACE "\\" "/" BUILD_DIR_PATTERN "${REPORT_DIR}")
list(APPEND CPPCHECK_ARGUMENTS "--suppress=*:${BUILD_DIR_PATTERN}/*")
if(DEFINED EXCLUDE_HEADERS_DIR AND NOT EXCLUDE_HEADERS_DIR STREQUAL "")
    file(TO_CMAKE_PATH "${EXCLUDE_HEADERS_DIR}" EXCLUDE_HEADERS_DIR)
    list(APPEND CPPCHECK_ARGUMENTS
        "--config-exclude=${EXCLUDE_HEADERS_DIR}"
        "--suppress=*:${EXCLUDE_HEADERS_DIR}/*")
endif()

execute_process(
    COMMAND "${CPPCHECK_EXECUTABLE}" ${CPPCHECK_ARGUMENTS}
    RESULT_VARIABLE CPPCHECK_RESULT
    OUTPUT_VARIABLE CPPCHECK_OUTPUT
    ERROR_VARIABLE CPPCHECK_ERROR
)
if(NOT CPPCHECK_RESULT STREQUAL "0")
    message(FATAL_ERROR
        "cppcheck failed (exit code ${CPPCHECK_RESULT}):\n${CPPCHECK_OUTPUT}${CPPCHECK_ERROR}")
endif()

# cppcheck prints findings on stderr; the same file can appear in several
# compile_commands entries, so duplicates are removed.
string(REPLACE "\r" "" CPPCHECK_ERROR "${CPPCHECK_ERROR}")
string(REPLACE ";" "," CPPCHECK_ERROR "${CPPCHECK_ERROR}")
string(REPLACE "\n" ";" FINDING_LINES "${CPPCHECK_ERROR}")
list(REMOVE_DUPLICATES FINDING_LINES)

get_filename_component(CODE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
file(TO_CMAKE_PATH "${CODE_ROOT}" CODE_ROOT)

set(MANDATORY_FINDINGS)
set(REQUIRED_FINDINGS)
set(ADVISORY_FINDINGS)
set(OTHER_FINDINGS)
set(CONFIG_NOTES 0)
foreach(LINE IN LISTS FINDING_LINES)
    # Normalise Windows path separators; file(TO_CMAKE_PATH) is not used
    # because it treats ':' as a list separator on Linux.
    string(REPLACE "\\" "/" LINE "${LINE}")
    string(REPLACE "${CODE_ROOT}/" "" LINE "${LINE}")
    string(REGEX REPLACE "^(\\.\\./)+[^:]*/code/" "" LINE "${LINE}")
    if(LINE MATCHES ":misra-config:")
        math(EXPR CONFIG_NOTES "${CONFIG_NOTES} + 1")
    elseif(LINE MATCHES ":misra-c2012-[0-9.]+:Mandatory rule")
        list(APPEND MANDATORY_FINDINGS "${LINE}")
    elseif(LINE MATCHES ":misra-c2012-[0-9.]+:Required rule")
        list(APPEND REQUIRED_FINDINGS "${LINE}")
    elseif(LINE MATCHES ":misra-c2012-[0-9.]+:Advisory rule")
        list(APPEND ADVISORY_FINDINGS "${LINE}")
    elseif(LINE MATCHES ":[a-zA-Z0-9_.-]+:")
        list(APPEND OTHER_FINDINGS "${LINE}")
    endif()
endforeach()

list(LENGTH MANDATORY_FINDINGS MANDATORY_COUNT)
list(LENGTH REQUIRED_FINDINGS REQUIRED_COUNT)
list(LENGTH ADVISORY_FINDINGS ADVISORY_COUNT)
list(LENGTH OTHER_FINDINGS OTHER_COUNT)

# Count findings per rule for the summary.
function(misra_rule_summary FINDINGS OUTPUT_VARIABLE)
    set(RULES)
    foreach(FINDING IN LISTS FINDINGS)
        string(REGEX MATCH "misra-c2012-[0-9.]+" RULE "${FINDING}")
        list(APPEND RULES "${RULE}")
    endforeach()
    set(UNIQUE_RULES ${RULES})
    if(UNIQUE_RULES)
        list(REMOVE_DUPLICATES UNIQUE_RULES)
        list(SORT UNIQUE_RULES COMPARE NATURAL)
    endif()
    set(SUMMARY "")
    foreach(RULE IN LISTS UNIQUE_RULES)
        set(COUNT 0)
        foreach(ITEM IN LISTS RULES)
            if(ITEM STREQUAL RULE)
                math(EXPR COUNT "${COUNT} + 1")
            endif()
        endforeach()
        string(APPEND SUMMARY "  ${RULE}: ${COUNT}\n")
    endforeach()
    set(${OUTPUT_VARIABLE} "${SUMMARY}" PARENT_SCOPE)
endfunction()

misra_rule_summary("${MANDATORY_FINDINGS}" MANDATORY_SUMMARY)
misra_rule_summary("${REQUIRED_FINDINGS}" REQUIRED_SUMMARY)
misra_rule_summary("${ADVISORY_FINDINGS}" ADVISORY_SUMMARY)
string(REPLACE ";" "\n" MANDATORY_LIST "${MANDATORY_FINDINGS}")
string(REPLACE ";" "\n" REQUIRED_LIST "${REQUIRED_FINDINGS}")
string(REPLACE ";" "\n" ADVISORY_LIST "${ADVISORY_FINDINGS}")
string(REPLACE ";" "\n" OTHER_LIST "${OTHER_FINDINGS}")

set(REPORT
"MISRA C screening report (cppcheck MISRA addon) - device: ${DEVICE}

Mandatory findings: ${MANDATORY_COUNT}
${MANDATORY_SUMMARY}
Required findings: ${REQUIRED_COUNT}
${REQUIRED_SUMMARY}
Advisory findings: ${ADVISORY_COUNT}
${ADVISORY_SUMMARY}
Other cppcheck findings: ${OTHER_COUNT}
misra-config notes (not violations): ${CONFIG_NOTES}
Rule 17.3: enforced by the compiler (GCC >= 14), not by this screen.

--- Mandatory ---
${MANDATORY_LIST}

--- Required ---
${REQUIRED_LIST}

--- Advisory ---
${ADVISORY_LIST}

--- Other ---
${OTHER_LIST}
")
set(REPORT_FILE "${REPORT_DIR}/misra_report_${DEVICE}.txt")
file(WRITE "${REPORT_FILE}" "${REPORT}")

message(STATUS
    "MISRA screening (${DEVICE}): ${MANDATORY_COUNT} mandatory, ${REQUIRED_COUNT} required, "
    "${ADVISORY_COUNT} advisory, ${OTHER_COUNT} other findings.\n"
    "Required by rule:\n${REQUIRED_SUMMARY}"
    "Advisory by rule:\n${ADVISORY_SUMMARY}"
    "Full report: ${REPORT_FILE}")

if(MANDATORY_COUNT GREATER 0)
    message(FATAL_ERROR
        "MISRA screening failed: ${MANDATORY_COUNT} finding(s) of mandatory rules:\n"
        "${MANDATORY_LIST}")
endif()
