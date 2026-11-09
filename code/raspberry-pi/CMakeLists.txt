cmake_minimum_required(VERSION 3.16)

project(medication_dispenser_gateway C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(medication_dispenser_gateway
    src/main.c
    src/time_sync/time_sync.c
)

target_compile_options(medication_dispenser_gateway PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(medication_dispenser_gateway PRIVATE Threads::Threads)

if(BUILD_TESTING)
    add_executable(time_sync_test
        test/time_sync_test.c
        src/time_sync/time_sync.c
    )
    target_compile_options(time_sync_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME time_sync_tests COMMAND time_sync_test)

    add_test(
        NAME misra_c_screening
        COMMAND "${CMAKE_COMMAND}"
            "-DCPPCHECK_EXECUTABLE=cppcheck"
            -P "${CMAKE_SOURCE_DIR}/../tests/misra/check.cmake"
    )
endif()
