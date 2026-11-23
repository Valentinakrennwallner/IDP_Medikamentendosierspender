cmake_minimum_required(VERSION 3.16)

# PROJECT_VERSION is the gateway software version printed in the start-up
# log message (Design Document, Chapter 6, Section 6.3, Requirement 5.3).
project(medication_dispenser_gateway VERSION 0.1.0 LANGUAGES C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
# compile_commands.json provides include paths for the MISRA screening test.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL 3.0 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
pkg_check_modules(JSONC REQUIRED IMPORTED_TARGET json-c)

# The logging thread lowers itself to SCHED_IDLE, a Linux extension of
# <sched.h>. Passed here instead of #define in the source (MISRA C:2012
# Rule 21.1).
set_source_files_properties(src/logging/logging.c
    PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE
)

add_executable(medication_dispenser_gateway
    src/main.c
    src/ble/startup_transfer.c
    src/ble/heartbeat_monitor.c
    src/ble/bluez_central.c
    src/ble/message_dispatch.c
    src/secure_channel/secure_channel.c
    src/time_sync/time_sync.c
    src/notifications/telegram.c
    src/schedule/schedule.c
    src/logging/logging.c
    ../protocol/src/startup_message.c
    ../protocol/src/report_message.c
)
target_include_directories(medication_dispenser_gateway PRIVATE
    ../protocol/include
)
target_compile_definitions(medication_dispenser_gateway PRIVATE
    GATEWAY_SOFTWARE_VERSION="${PROJECT_VERSION}"
)

target_compile_options(medication_dispenser_gateway PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(medication_dispenser_gateway PRIVATE
    Threads::Threads
    PkgConfig::DBUS
    PkgConfig::JSONC
    m
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

    add_executable(logging_test
        test/logging_test.c
        src/logging/logging.c
    )
    target_compile_definitions(logging_test PRIVATE
        GATEWAY_SOFTWARE_VERSION="${PROJECT_VERSION}"
    )
    target_compile_options(logging_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(logging_test PRIVATE Threads::Threads)
    add_test(NAME logging_tests COMMAND logging_test)
    # The test only uses bounded waits; the timeout is a second safety net.
    set_tests_properties(logging_tests PROPERTIES TIMEOUT 60)

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

    add_executable(sensor_fault_test
        ../esp32/test/sensor_fault_test.c
        ../esp32/components/sensor_fault/sensor_fault.c
    )
    target_include_directories(sensor_fault_test PRIVATE
        ../esp32/components/weight
        ../esp32/components/sensor_fault
    )
    target_compile_options(sensor_fault_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME sensor_fault_tests COMMAND sensor_fault_test)

    add_executable(weight_monitor_test
        ../esp32/test/weight_monitor_test.c
        ../esp32/components/weight_monitor/weight_monitor.c
        ../esp32/components/sensor_fault/sensor_fault.c
        ../esp32/components/weight/weight.c
    )
    target_include_directories(weight_monitor_test PRIVATE
        ../esp32/test/mocks
        ../esp32/components/weight
        ../esp32/components/sensor_fault
        ../esp32/components/weight_monitor
    )
    target_compile_options(weight_monitor_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(weight_monitor_test PRIVATE m)
    add_test(NAME weight_monitor_tests COMMAND weight_monitor_test)

    add_executable(persistence_test
        ../esp32/test/persistence_test.c
        ../esp32/components/persistence/persistence.c
    )
    target_include_directories(persistence_test PRIVATE
        ../esp32/test/mocks
        ../esp32/components/weight
        ../esp32/components/persistence
    )
    target_compile_options(persistence_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(persistence_test PRIVATE m)
    add_test(NAME persistence_tests COMMAND persistence_test)

    add_executable(calibration_test
        ../esp32/test/calibration_test.c
        ../esp32/components/calibration/calibration.c
    )
    target_include_directories(calibration_test PRIVATE
        ../esp32/test/mocks
        ../esp32/components/weight
        ../esp32/components/persistence
        ../esp32/components/calibration
    )
    target_compile_options(calibration_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(calibration_test PRIVATE m)
    add_test(NAME calibration_tests COMMAND calibration_test)

    add_executable(schedule_test
        test/schedule_test.c
        src/schedule/schedule.c
        ../protocol/src/startup_message.c
    )
    target_include_directories(schedule_test PRIVATE
        ../protocol/include
    )
    target_compile_definitions(schedule_test PRIVATE
        EXAMPLE_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/config/device.example.json"
    )
    target_compile_options(schedule_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(schedule_test PRIVATE PkgConfig::JSONC m)
    add_test(NAME schedule_tests COMMAND schedule_test)

    add_executable(dosing_test
        ../esp32/test/dosing_test.c
        ../esp32/components/dosing/dosing.c
        ../protocol/src/startup_message.c
    )
    target_include_directories(dosing_test PRIVATE
        ../protocol/include
    )
    target_compile_options(dosing_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME dosing_tests COMMAND dosing_test)

    add_executable(wall_clock_test
        ../esp32/test/wall_clock_test.c
        ../esp32/components/wall_clock/wall_clock.c
        ../esp32/components/dosing/dosing.c
        ../protocol/src/startup_message.c
    )
    target_include_directories(wall_clock_test PRIVATE
        ../protocol/include
        ../esp32/components/dosing
        ../esp32/components/wall_clock
    )
    target_compile_options(wall_clock_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME wall_clock_tests COMMAND wall_clock_test)

    add_executable(report_message_test
        test/report_message_test.c
        ../protocol/src/report_message.c
    )
    target_include_directories(report_message_test PRIVATE
        ../protocol/include
    )
    target_compile_options(report_message_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME report_message_tests COMMAND report_message_test)

    add_executable(message_dispatch_test
        test/message_dispatch_test.c
        src/ble/message_dispatch.c
        ../protocol/src/report_message.c
    )
    target_include_directories(message_dispatch_test PRIVATE
        ../protocol/include
    )
    target_compile_options(message_dispatch_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME message_dispatch_tests COMMAND message_dispatch_test)

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
    # The test holds a client and a server session at the same time; the
    # production default of the static session pool is 1.
    target_compile_definitions(secure_channel_test PRIVATE
        SECURE_CHANNEL_POOL_SIZE=2U
    )
    target_link_libraries(secure_channel_test PRIVATE OpenSSL::Crypto)
    add_test(NAME secure_channel_tests COMMAND secure_channel_test)

    # OpenSSL (gateway) <-> mbedTLS (ESP32) interoperability test. Needs the
    # host mbedTLS 3.6 package (libmbedtls-dev); ESP-IDF v5.5 bundles mbedTLS
    # 3.6 as well. Both backends define the same public functions, so the
    # mbedTLS backend's functions are renamed to esp32_secure_channel_*.
    # The struct tag secure_channel_backend_state is deliberately not
    # renamed: the test only sees it as an incomplete type, which is
    # compatible with both backends' definitions (C11 6.2.7). Renaming it in
    # one translation unit would give secure_channel_t incompatible member
    # types across translation units. The test uses one session per backend
    # at a time, so the production pool size (1) is kept.
    find_package(MbedTLS 3.6 CONFIG QUIET)
    if(MbedTLS_FOUND AND (MbedTLS_VERSION VERSION_LESS 3.7))
        add_executable(secure_channel_interop_test
            test/secure_channel_interop_test.c
            src/secure_channel/secure_channel.c
            ../esp32/components/ble/secure_channel_mbedtls.c
        )
        target_include_directories(secure_channel_interop_test PRIVATE
            ../protocol/include
            ../esp32/test/mocks
        )
        set(ESP32_SECURE_CHANNEL_RENAMES
            secure_channel_init=esp32_secure_channel_init
            secure_channel_process_handshake=esp32_secure_channel_process_handshake
            secure_channel_seal=esp32_secure_channel_seal
            secure_channel_open=esp32_secure_channel_open
            secure_channel_clear=esp32_secure_channel_clear
        )
        set_source_files_properties(
            ../esp32/components/ble/secure_channel_mbedtls.c
            PROPERTIES COMPILE_DEFINITIONS "${ESP32_SECURE_CHANNEL_RENAMES}"
        )
        target_compile_options(secure_channel_interop_test PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
        target_link_libraries(secure_channel_interop_test PRIVATE
            OpenSSL::Crypto
            MbedTLS::mbedcrypto
        )
        add_test(NAME secure_channel_interop_tests
            COMMAND secure_channel_interop_test)
    else()
        message(WARNING
            "mbedTLS 3.6 (libmbedtls-dev) not found: "
            "secure_channel_interop_tests is not built.")
    endif()

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
            "-DDEVICE=raspberry-pi"
            "-DCOMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json"
            "-DREPORT_DIR=${CMAKE_BINARY_DIR}"
            -P "${CMAKE_SOURCE_DIR}/../tests/misra/check.cmake"
    )

    add_test(
        NAME misra_deviation_list
        COMMAND "${CMAKE_COMMAND}"
            -P "${CMAKE_SOURCE_DIR}/../tests/misra/check_deviations.cmake"
    )
endif()
