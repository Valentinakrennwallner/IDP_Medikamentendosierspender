#ifndef MOCK_DRIVER_UART_H
#define MOCK_DRIVER_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Minimal ESP-IDF UART declarations so that calibration_uart.c can be parsed
 * by the MISRA screening on the host. Not used by the host tests.
 */
typedef int uart_port_t;
typedef uint32_t TickType_t;
typedef void *QueueHandle_t;

#define UART_NUM_0 0
#define portMAX_DELAY 0xFFFFFFFFU

esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size, int tx_buffer_size,
                              int queue_size, QueueHandle_t *uart_queue,
                              int intr_alloc_flags);
bool uart_is_driver_installed(uart_port_t uart_num);
int uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length,
                    TickType_t ticks_to_wait);
int uart_write_bytes(uart_port_t uart_num, const void *src, size_t size);

#endif
