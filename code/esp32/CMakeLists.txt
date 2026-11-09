cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(medication_dispenser_esp32)

include(CTest)

if(BUILD_TESTING)
    add_test(
        NAME misra_c_screening
        COMMAND "${CMAKE_COMMAND}"
            "-DCPPCHECK_EXECUTABLE=cppcheck"
            -P "${CMAKE_SOURCE_DIR}/../tests/misra/check.cmake"
    )
endif()
