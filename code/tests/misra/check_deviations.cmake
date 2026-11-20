# Check the deviation records in docs/misra-deviations.md against the
# screening configuration. There are two kinds of records (see
# docs/code-style.md):
#
# - Advisory deviations, heading "## Rule N.M - <title>". The rule is skipped
#   by the screening (listed in deviated_rules.cmake). The list and these
#   records must match exactly, and each record states a category,
#   a justification, and its conditions.
# - Required deviations (MISRA Compliance:2020), heading
#   "## Required deviation: Rule N.M - <title>". The rule is NOT skipped: its
#   findings stay visible in the screening report. Each record states the
#   category, rationale, scope, risk assessment, mitigation, and approval.
#
# Categories are checked against rule_categories.txt (MISRA C:2012), so a
# Required or Mandatory rule cannot be suppressed through
# deviated_rules.cmake, and a Mandatory rule cannot have any record.
#
# Usage: cmake -P check_deviations.cmake
# Optional overrides (used to test this script): DEVIATED_RULES_FILE,
# DEVIATION_DOCUMENT and RULE_CATEGORIES_FILE.

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

if(NOT DEFINED DEVIATED_RULES_FILE)
    set(DEVIATED_RULES_FILE "${CMAKE_CURRENT_LIST_DIR}/deviated_rules.cmake")
endif()
if(NOT DEFINED DEVIATION_DOCUMENT)
    set(DEVIATION_DOCUMENT "${REPOSITORY_ROOT}/docs/misra-deviations.md")
endif()
if(NOT DEFINED RULE_CATEGORIES_FILE)
    set(RULE_CATEGORIES_FILE "${CMAKE_CURRENT_LIST_DIR}/rule_categories.txt")
endif()

include("${DEVIATED_RULES_FILE}")
if(NOT MISRA_DEVIATED_RULES)
    message(FATAL_ERROR "${DEVIATED_RULES_FILE} lists no deviated rules.")
endif()

file(READ "${DEVIATION_DOCUMENT}" DOCUMENT)

file(STRINGS "${RULE_CATEGORIES_FILE}" CATEGORY_LINES
    REGEX "^Rule [0-9]+\\.[0-9]+ (Mandatory|Required|Advisory)$")

# Look up the MISRA C:2012 category of a rule in rule_categories.txt.
function(misra_rule_category RULE OUTPUT_VARIABLE)
    set(CATEGORY "")
    foreach(LINE IN LISTS CATEGORY_LINES)
        if(LINE MATCHES "^Rule ([0-9]+\\.[0-9]+) ([A-Za-z]+)$")
            if(CMAKE_MATCH_1 STREQUAL RULE)
                set(CATEGORY "${CMAKE_MATCH_2}")
            endif()
        endif()
    endforeach()
    if(CATEGORY STREQUAL "")
        message(FATAL_ERROR "Rule ${RULE} is not listed in ${RULE_CATEGORIES_FILE}.")
    endif()
    set(${OUTPUT_VARIABLE} "${CATEGORY}" PARENT_SCOPE)
endfunction()

# Return the text of the record that starts with HEADING (up to the next
# "## " heading), or an empty string if there is no such record.
function(deviation_record HEADING OUTPUT_VARIABLE)
    set(RECORD "")
    string(FIND "${DOCUMENT}" "\n${HEADING}" RECORD_START)
    if(RECORD_START GREATER_EQUAL 0)
        string(SUBSTRING "${DOCUMENT}" ${RECORD_START} -1 RECORD)
        string(SUBSTRING "${RECORD}" 4 -1 RECORD_BODY)
        string(FIND "${RECORD_BODY}" "\n## " RECORD_END)
        if(RECORD_END GREATER_EQUAL 0)
            string(SUBSTRING "${RECORD_BODY}" 0 ${RECORD_END} RECORD)
        endif()
    endif()
    set(${OUTPUT_VARIABLE} "${RECORD}" PARENT_SCOPE)
endfunction()

# Fail if a record lacks one of the given fields.
function(require_fields RULE RECORD)
    foreach(FIELD IN LISTS ARGN)
        string(FIND "${RECORD}" "${FIELD}" FIELD_POSITION)
        if(FIELD_POSITION LESS 0)
            message(FATAL_ERROR "The record for rule ${RULE} lacks '${FIELD}'.")
        endif()
    endforeach()
endfunction()

# 1. Advisory deviations: every rule the screening skips is Advisory and
#    has a complete "## Rule N.M" record.
foreach(RULE IN LISTS MISRA_DEVIATED_RULES)
    misra_rule_category("${RULE}" CATEGORY)
    if(NOT CATEGORY STREQUAL "Advisory")
        message(FATAL_ERROR
            "Rule ${RULE} is ${CATEGORY}; only Advisory rules may be listed in "
            "${DEVIATED_RULES_FILE}. Required rules need a 'Required deviation' "
            "record and must stay visible in the screening.")
    endif()

    deviation_record("## Rule ${RULE} " RECORD)
    if(RECORD STREQUAL "")
        message(FATAL_ERROR
            "Rule ${RULE} is skipped by the screening but has no '## Rule ${RULE}' "
            "record in ${DEVIATION_DOCUMENT}.")
    endif()
    require_fields("${RULE}" "${RECORD}"
        "**Category:** Advisory" "**Justification:**" "**Conditions:**")
endforeach()

# Every advisory record must correspond to a rule the screening skips.
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

# 2. Required deviations: the rule is Required, is not skipped, and the
#    record is complete. "Approved by" may still be pending.
string(REGEX MATCHALL "\n## Required deviation: Rule [0-9]+\\.[0-9]+" REQUIRED_HEADINGS
    "${DOCUMENT}")
set(REQUIRED_RULES "")
foreach(HEADING IN LISTS REQUIRED_HEADINGS)
    string(REGEX REPLACE "^\n## Required deviation: Rule " "" RULE "${HEADING}")
    list(APPEND REQUIRED_RULES "${RULE}")

    misra_rule_category("${RULE}" CATEGORY)
    if(NOT CATEGORY STREQUAL "Required")
        message(FATAL_ERROR
            "Rule ${RULE} is ${CATEGORY}; a 'Required deviation' record is only "
            "allowed for Required rules (Mandatory rules are never deviated).")
    endif()

    list(FIND MISRA_DEVIATED_RULES "${RULE}" RULE_INDEX)
    if(RULE_INDEX GREATER_EQUAL 0)
        message(FATAL_ERROR
            "Required rule ${RULE} is listed in ${DEVIATED_RULES_FILE}; Required "
            "deviations must not be suppressed in the screening.")
    endif()

    deviation_record("## Required deviation: Rule ${RULE} " RECORD)
    require_fields("${RULE}" "${RECORD}"
        "**Category:** Required" "**Rationale:**" "**Scope:**" "**Risk assessment:**"
        "**Mitigation:**" "**Approved by:**")

    string(REGEX MATCH "\\*\\*Approved by:\\*\\*[^\n]*[Pp]ending" APPROVAL_PENDING
        "${RECORD}")
    if(APPROVAL_PENDING)
        message(STATUS "Required deviation for rule ${RULE} is awaiting group approval")
    endif()
endforeach()

list(LENGTH MISRA_DEVIATED_RULES RULE_COUNT)
list(LENGTH REQUIRED_RULES REQUIRED_COUNT)
message(STATUS
    "MISRA deviation list matches ${RULE_COUNT} documented advisory rules; "
    "${REQUIRED_COUNT} required deviation records (not suppressed)")
