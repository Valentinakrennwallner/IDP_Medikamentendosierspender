#ifndef SERIAL_LINE_UART_H
#define SERIAL_LINE_UART_H

/*
 * Console UART binding of the serial commissioning dialogs (UART 0 via the
 * ESP-IDF UART driver, no stdio). Shared by the calibration and the
 * pre-shared key dialog.
 * Design Document, Chapter 2, Section 2.1 (weight_calibrate()), and
 * Chapter 6, Section 6.1, Requirement 2.8, and Section 6.2, Requirement 4.7.
 */

#include <stdbool.h>
#include <stdint.h>

/*
 * Install the UART driver of the console UART if it is not installed yet.
 * The driver stays installed. Returns false if the installation fails.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8, and Section 6.2,
 * Requirement 4.7.
 */
bool serial_line_uart_open(void);

/*
 * Block until one byte has been received on the console UART.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8, and Section 6.2,
 * Requirement 4.7.
 */
bool serial_line_uart_read_byte(uint8_t *byte);

/*
 * Send a zero-terminated text on the console UART.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8, and Section 6.2,
 * Requirement 4.7.
 */
bool serial_line_uart_write_text(const char *text);

#endif
