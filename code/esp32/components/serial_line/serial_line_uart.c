/*
 * Console UART binding of the serial commissioning dialogs. Taken out of
 * the calibration binding (task 9) so that the pre-shared key dialog
 * (task 17) uses the same UART functions.
 */
#include "serial_line_uart.h"

#include <stddef.h>
#include <string.h>

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"

/* Console UART of the project (sdkconfig: CONFIG_ESP_CONSOLE_UART_NUM=0). */
#define SERIAL_LINE_UART UART_NUM_0
/* The driver needs a receive buffer larger than the 128-byte hardware FIFO. */
#define SERIAL_LINE_UART_RX_BUFFER_SIZE 256

/*
 * Install the UART driver of the console UART if it is not installed yet.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8, and Section 6.2,
 * Requirement 4.7.
 */
bool serial_line_uart_open(void)
{
    bool open = true;

    if (!uart_is_driver_installed(SERIAL_LINE_UART))
    {
        open = (uart_driver_install(SERIAL_LINE_UART, SERIAL_LINE_UART_RX_BUFFER_SIZE, 0, 0,
                                    NULL, 0) == ESP_OK);
    }
    return open;
}

/*
 * Block until one byte has been received on the console UART.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8, and Section 6.2,
 * Requirement 4.7.
 */
bool serial_line_uart_read_byte(uint8_t *byte)
{
    return uart_read_bytes(SERIAL_LINE_UART, byte, 1U, portMAX_DELAY) == 1;
}

/*
 * Send a zero-terminated text on the console UART.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8, and Section 6.2,
 * Requirement 4.7.
 */
bool serial_line_uart_write_text(const char *text)
{
    size_t length = strlen(text);

    return uart_write_bytes(SERIAL_LINE_UART, text, length) == (int)length;
}
