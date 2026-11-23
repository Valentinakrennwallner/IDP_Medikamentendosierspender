#ifndef REPORT_BUFFER_SERVICE_H
#define REPORT_BUFFER_SERVICE_H

/*
 * The single report buffer of the ESP32, protected by a spinlock: reports
 * are queued by the weight-measurement thread (sensor fault), later by the
 * dose evaluation and the day end, and sent by the BLE thread.
 * Design Document, Chapter 2, Section 2.3, and Chapter 6, Section 6.1,
 * Requirements 1.6, 1.7, 2.6, 2.13 and 3.2.
 */

#include <stdbool.h>
#include <stdint.h>

#include "report_buffer.h"

/*
 * Queue a RESULT. Returns false if the report is invalid. Design Document,
 * Chapter 6, Section 6.1, Requirements 1.6, 2.13 and 3.2.
 */
bool report_buffer_service_push_result(report_status_t status, uint8_t intake_index);

/*
 * Queue a FAULT (start or end of a fault). Returns false if the report is
 * invalid. Design Document, Chapter 6, Section 6.1, Requirements 2.6, 2.10,
 * 3.2 and 3.3.
 */
bool report_buffer_service_push_fault(report_status_t status, report_fault_state_t fault_state);

/*
 * Queue the end-of-day message. Design Document, Chapter 6, Section 6.1,
 * Requirements 1.7 and 3.2.
 */
bool report_buffer_service_push_day_end(void);

/*
 * Send the buffered reports in order with the transport's send function
 * (tasks 18 and 19: after every (re)connect and when new reports are
 * waiting while connected). The lock is not held while send() runs.
 * Returns the number of reports sent. Design Document, Chapter 6,
 * Section 6.1, Requirement 3.2.
 */
uint32_t report_buffer_service_flush(report_buffer_send_fn send, void *context);

/*
 * Number of reports waiting to be sent. Design Document, Chapter 6,
 * Section 6.1, Requirement 3.2.
 */
uint32_t report_buffer_service_count(void);

#endif
