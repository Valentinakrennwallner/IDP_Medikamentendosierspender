/*
 * Hardware binding of the commissioning calibration: serial console (UART 0,
 * shared binding in components/serial_line), HX711 (weight component) and
 * NVS (persistence component).
 * Design Document, Chapter 2, Sections 2.1 and 2.2, and Chapter 6,
 * Section 6.1, Requirement 2.8.
 */
#include "calibration.h"

#include "serial_line_uart.h"

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
        serial_line_uart_read_byte,
        serial_line_uart_write_text,
        persistence_init,
        persistence_erase_and_init,
        weight_measure_tare,
        weight_measure_raw_mean,
        persistence_calibration_save,
        persistence_calibration_load,
        control_measure,
    };
    calibration_result_t result = CALIBRATION_OK;

    if (!serial_line_uart_open())
    {
        result = CALIBRATION_IO_ERROR;
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
