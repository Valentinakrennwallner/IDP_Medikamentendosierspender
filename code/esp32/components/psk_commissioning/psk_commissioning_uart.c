/*
 * Hardware binding of the pre-shared key commissioning: console UART
 * (components/serial_line) and NVS (components/persistence).
 * Design Document, Chapter 2, Sections 2.1 and 2.2, and Chapter 6,
 * Section 6.2, Requirement 4.7.
 */
#include "psk_commissioning.h"

#include "serial_line_uart.h"

/*
 * Install the UART driver if needed and run the dialog with the NVS
 * persistence functions. Design Document, Chapter 6, Section 6.2,
 * Requirement 4.7.
 */
psk_commissioning_result_t psk_commission(void)
{
    static const psk_commissioning_io_t io = {
        serial_line_uart_read_byte,
        serial_line_uart_write_text,
        persistence_init,
        persistence_psk_save,
        persistence_psk_load,
    };
    psk_commissioning_result_t result = PSK_COMMISSIONING_IO_ERROR;

    if (serial_line_uart_open())
    {
        result = psk_commissioning_dialog_run(&io);
    }
    return result;
}
