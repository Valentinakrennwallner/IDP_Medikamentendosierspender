cmake_minimum_required(VERSION 3.16)

project(medication_dispenser_gateway C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL 3.0 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)

add_executable(medication_dispenser_gateway
    src/main.c
    src/ble/startup_transfer.c
    src/ble/heartbeat_monitor.c
    src/ble/bluez_central.c
    src/secure_channel/secure_channel.c
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
    PkgConfig::DBUS
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

    add_executable(heartbeat_monitor_test
        test/heartbeat_monitor_test.c
        src/ble/heartbeat_monitor.c
    )
    target_compile_options(heartbeat_monitor_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME heartbeat_monitor_tests COMMAND heartbeat_monitor_test)

    add_executable(heartbeat_client_test
        test/heartbeat_client_test.c
        ../esp32/components/ble/heartbeat_client.c
    )
    target_compile_options(heartbeat_client_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME heartbeat_client_tests COMMAND heartbeat_client_test)

    add_executable(weight_test
        ../esp32/test/weight_test.c
        ../esp32/components/weight/weight.c
    )
    target_include_directories(weight_test PRIVATE
        ../esp32/test/mocks
    )
    target_compile_options(weight_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(weight_test PRIVATE m)
    add_test(NAME weight_tests COMMAND weight_test)

    add_executable(secure_channel_test
        test/secure_channel_test.c
        src/secure_channel/secure_channel.c
    )
    target_include_directories(secure_channel_test PRIVATE
        ../protocol/include
    )
    target_compile_options(secure_channel_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(secure_channel_test PRIVATE OpenSSL::Crypto)
    add_test(NAME secure_channel_tests COMMAND secure_channel_test)

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
