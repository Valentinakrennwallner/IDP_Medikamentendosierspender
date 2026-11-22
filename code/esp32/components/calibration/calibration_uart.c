/*
 * Hardware binding of the commissioning calibration: serial console (UART 0
 * via the ESP-IDF UART driver, no stdio), HX711 (weight component) and NVS
 * (persistence component).
 * Design Document, Chapter 2, Sections 2.1 and 2.2, and Chapter 6,
 * Section 6.1, Requirement 2.8.
 */
#include "calibration.h"

#include <stddef.h>
#include <string.h>

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"

/* Console UART of the project (sdkconfig: CONFIG_ESP_CONSOLE_UART_NUM=0). */
#define CALIBRATION_UART UART_NUM_0
/* The driver needs a receive buffer larger than the 128-byte hardware FIFO. */
#define CALIBRATION_UART_RX_BUFFER_SIZE 256

/*
 * Block until one byte has been received on the console UART.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static bool uart_read_byte(uint8_t *byte)
{
    return uart_read_bytes(CALIBRATION_UART, byte, 1U, portMAX_DELAY) == 1;
}

/*
 * Send a zero-terminated text on the console UART.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static bool uart_write_text(const char *text)
{
    size_t length = strlen(text);

    return uart_write_bytes(CALIBRATION_UART, text, length) == (int)length;
}

/*
 * One calibrated measurement for the control step. The intake window is
 * treated as active, so placing or removing the reference weight is not
 * rejected by the 5 g rule.
 * Design Document, Chapter 2, Section 2.1, and Chapter 6, Section 6.1,
 * Requirements 2.4 and 2.8.
 */
static weight_measure_status_t control_measure(const weight_calibration_t *calibration,
                                               weight_measurement_t *measurement)
{
    return weight_measure(calibration, true, false, 0.0F, measurement);
}

/*
 * Install the UART driver if needed, initialise the HX711 and run the
 * dialog. The driver stays installed afterwards.
 * Design Document, Chapter 2, Sections 2.1 and 2.2, and Chapter 6,
 * Section 6.1, Requirement 2.8.
 */
calibration_result_t weight_calibrate(void)
{
    static const calibration_io_t io = {
        uart_read_byte,
        uart_write_text,
        persistence_init,
        persistence_erase_and_init,
        weight_measure_tare,
        weight_measure_raw_mean,
        persistence_calibration_save,
        persistence_calibration_load,
        control_measure,
    };
    calibration_result_t result = CALIBRATION_OK;

    if (!uart_is_driver_installed(CALIBRATION_UART))
    {
        if (uart_driver_install(CALIBRATION_UART, CALIBRATION_UART_RX_BUFFER_SIZE, 0, 0,
                                NULL, 0) != ESP_OK)
        {
            result = CALIBRATION_IO_ERROR;
        }
    }
    if ((result == CALIBRATION_OK) && (weight_init() != WEIGHT_RESULT_OK))
    {
        result = CALIBRATION_SENSOR_FAULT;
    }
    if (result == CALIBRATION_OK)
    {
        result = calibration_dialog_run(&io);
    }
    return result;
}
