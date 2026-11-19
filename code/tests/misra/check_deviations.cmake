# Check that docs/misra-deviations.md documents exactly the rules that the
# MISRA screening skips, and that each record states a category, a
# justification, and its conditions.
#
# Usage: cmake -P check_deviations.cmake
# Optional overrides (used to test this script): DEVIATED_RULES_FILE and
# DEVIATION_DOCUMENT.

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

if(NOT DEFINED DEVIATED_RULES_FILE)
    set(DEVIATED_RULES_FILE "${CMAKE_CURRENT_LIST_DIR}/deviated_rules.cmake")
endif()
if(NOT DEFINED DEVIATION_DOCUMENT)
    set(DEVIATION_DOCUMENT "${REPOSITORY_ROOT}/docs/misra-deviations.md")
endif()

include("${DEVIATED_RULES_FILE}")
if(NOT MISRA_DEVIATED_RULES)
    message(FATAL_ERROR "${DEVIATED_RULES_FILE} lists no deviated rules.")
endif()

file(READ "${DEVIATION_DOCUMENT}" DOCUMENT)

# Every rule the screening skips needs a complete record.
foreach(RULE IN LISTS MISRA_DEVIATED_RULES)
    string(FIND "${DOCUMENT}" "\n## Rule ${RULE} " RECORD_START)
    if(RECORD_START LESS 0)
        message(FATAL_ERROR
            "Rule ${RULE} is skipped by the screening but has no '## Rule ${RULE}' "
            "record in ${DEVIATION_DOCUMENT}.")
    endif()

    string(SUBSTRING "${DOCUMENT}" ${RECORD_START} -1 RECORD)
    string(SUBSTRING "${RECORD}" 4 -1 RECORD_BODY)
    string(FIND "${RECORD_BODY}" "\n## " RECORD_END)
    if(RECORD_END GREATER_EQUAL 0)
        string(SUBSTRING "${RECORD_BODY}" 0 ${RECORD_END} RECORD)
    endif()

    foreach(FIELD "**Category:** Advisory" "**Justification:**" "**Conditions:**")
        string(FIND "${RECORD}" "${FIELD}" FIELD_POSITION)
        if(FIELD_POSITION LESS 0)
            message(FATAL_ERROR "The record for rule ${RULE} lacks '${FIELD}'.")
        endif()
    endforeach()
endforeach()

# Every record must correspond to a rule the screening actually skips.
string(REGEX MATCHALL "\n## Rule [0-9]+\\.[0-9]+" HEADINGS "${DOCUMENT}")
foreach(HEADING IN LISTS HEADINGS)
    string(REGEX REPLACE "^\n## Rule " "" DOCUMENTED_RULE "${HEADING}")
    list(FIND MISRA_DEVIATED_RULES "${DOCUMENTED_RULE}" RULE_INDEX)
    if(RULE_INDEX LESS 0)
        message(FATAL_ERROR
            "Rule ${DOCUMENTED_RULE} has a record but is not skipped by the screening; "
            "add it to ${DEVIATED_RULES_FILE} or remove the record.")
    endif()
endforeach()

list(LENGTH MISRA_DEVIATED_RULES RULE_COUNT)
message(STATUS "MISRA deviation list matches ${RULE_COUNT} documented rules")
