cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(medication_dispenser_esp32)

include(CTest)

if(BUILD_TESTING)
    add_test(
        NAME misra_c_screening
        COMMAND "${CMAKE_COMMAND}"
            "-DCPPCHECK_EXECUTABLE=cppcheck"
            "-DDEVICE=esp32"
            "-DCOMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json"
            "-DREPORT_DIR=${CMAKE_BINARY_DIR}"
            "-DEXCLUDE_HEADERS_DIR=$ENV{IDF_PATH}"
            -P "${CMAKE_SOURCE_DIR}/../tests/misra/check.cmake"
    )

    add_test(
        NAME misra_deviation_list
        COMMAND "${CMAKE_COMMAND}"
            -P "${CMAKE_SOURCE_DIR}/../tests/misra/check_deviations.cmake"
    )
endif()
