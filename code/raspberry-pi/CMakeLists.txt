cmake_minimum_required(VERSION 3.16)

project(medication_dispenser_gateway C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(medication_dispenser_gateway
    src/main.c
    src/ble/startup_transfer.c
    src/schedule/schedule.c
    src/time_sync/time_sync.c
    src/notifications/telegram.c
    ../protocol/src/startup_message.c
)
target_include_directories(medication_dispenser_gateway PRIVATE
    ../protocol/include
)

target_compile_options(medication_dispenser_gateway PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(medication_dispenser_gateway PRIVATE
    Threads::Threads
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
)

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

    add_executable(startup_transfer_test
        test/startup_transfer_test.c
        ../esp32/components/ble/startup_receiver.c
        src/ble/startup_transfer.c
        src/schedule/schedule.c
        src/time_sync/time_sync.c
        ../protocol/src/startup_message.c
    )
    target_include_directories(startup_transfer_test PRIVATE
        ../protocol/include
    )
    target_compile_options(startup_transfer_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME startup_transfer_tests COMMAND startup_transfer_test)

    add_executable(telegram_test
        test/telegram_test.c
        src/notifications/telegram.c
        src/time_sync/time_sync.c
    )
    target_compile_options(telegram_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(telegram_test PRIVATE
        Threads::Threads
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
    )
    add_test(NAME telegram_tests COMMAND telegram_test)

    add_test(
        NAME misra_c_screening
        COMMAND "${CMAKE_COMMAND}"
            "-DCPPCHECK_EXECUTABLE=cppcheck"
            -P "${CMAKE_SOURCE_DIR}/../tests/misra/check.cmake"
    )
endif()
