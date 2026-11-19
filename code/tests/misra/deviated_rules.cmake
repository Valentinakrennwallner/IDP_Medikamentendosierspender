# Advisory MISRA C:2012 rules that the project has deviated from.
#
# Every rule listed here must have a deviation record (a "## Rule N.M" section)
# in docs/misra-deviations.md; the misra_deviation_list test enforces this.
# The misra_c_screening test passes each rule to cppcheck as a suppression.
# Required and mandatory rules must not be added without a formal deviation
# record that names the affected files.
set(MISRA_DEVIATED_RULES
    2.5
    8.9
    15.1
    15.5
)
