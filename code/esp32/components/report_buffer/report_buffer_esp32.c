/*
 * ESP32 binding of the report buffer: one report_buffer instance protected
 * by a spinlock. Only the buffer operations run inside the critical
 * section; sending (BLE) and logging happen outside it. Report contents
 * (results such as a missed dose) are health data and are never logged
 * (Design Document, Chapter 6, Section 6.2, Requirement 4.4).
 */
#include "report_buffer_service.h"

#include <stddef.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *REPORT_BUFFER_LOG_TAG = "report_buffer";

static portMUX_TYPE report_buffer_lock = portMUX_INITIALIZER_UNLOCKED;
/*
 * Zero-initialised, which is the state report_buffer_init() sets: empty,
 * next report gets sequence number 0.
 */
static report_buffer_t service_buffer;

/*
 * Log the outcome of queuing a report without its content.
 * Design Document, Chapter 6, Section 6.1, Requirement 3.2, and Section 6.2,
 * Requirement 4.4.
 */
static bool report_buffer_service_log_push(report_buffer_push_result_t result)
{
    if (result == REPORT_BUFFER_PUSH_OLDEST_DROPPED)
    {
        ESP_LOGW(REPORT_BUFFER_LOG_TAG, "Report buffer full: oldest report dropped");
    }
    else if (result == REPORT_BUFFER_PUSH_INVALID)
    {
        ESP_LOGE(REPORT_BUFFER_LOG_TAG, "Invalid report not queued");
    }
    else
    {
        /* REPORT_BUFFER_PUSH_OK: nothing to log. */
    }
    return (result != REPORT_BUFFER_PUSH_INVALID);
}

/*
 * Queue a RESULT. Design Document, Chapter 6, Section 6.1, Requirements
 * 1.6, 2.13 and 3.2.
 */
bool report_buffer_service_push_result(report_status_t status, uint8_t intake_index)
{
    report_buffer_push_result_t result;

    portENTER_CRITICAL(&report_buffer_lock);
    result = report_buffer_push_result(&service_buffer, status, intake_index);
    portEXIT_CRITICAL(&report_buffer_lock);
    return report_buffer_service_log_push(result);
}

/*
 * Queue a FAULT. Design Document, Chapter 6, Section 6.1, Requirements 2.6,
 * 2.10, 3.2 and 3.3.
 */
bool report_buffer_service_push_fault(report_status_t status, report_fault_state_t fault_state)
{
    report_buffer_push_result_t result;

    portENTER_CRITICAL(&report_buffer_lock);
    result = report_buffer_push_fault(&service_buffer, status, fault_state);
    portEXIT_CRITICAL(&report_buffer_lock);
    return report_buffer_service_log_push(result);
}

/*
 * Queue the end-of-day message. Design Document, Chapter 6, Section 6.1,
 * Requirements 1.7 and 3.2.
 */
bool report_buffer_service_push_day_end(void)
{
    report_buffer_push_result_t result;

    portENTER_CRITICAL(&report_buffer_lock);
    result = report_buffer_push_day_end(&service_buffer);
    portEXIT_CRITICAL(&report_buffer_lock);
    return report_buffer_service_log_push(result);
}

/*
 * Send the buffered reports in order: peek under the lock, send without
 * the lock, commit under the lock. Stops at the first failed send; the
 * report stays queued with its sequence number. Design Document, Chapter 6,
 * Section 6.1, Requirement 3.2.
 */
uint32_t report_buffer_service_flush(report_buffer_send_fn send, void *context)
{
    report_buffer_frame_t frame;
    uint32_t sent = 0U;
    bool sending = (send != NULL);
    bool available;

    while (sending)
    {
        portENTER_CRITICAL(&report_buffer_lock);
        available = report_buffer_peek(&service_buffer, &frame);
        portEXIT_CRITICAL(&report_buffer_lock);

        if (!available)
        {
            sending = false;
        }
        else if (send(context, frame.message_type, frame.payload, frame.length))
        {
            portENTER_CRITICAL(&report_buffer_lock);
            (void)report_buffer_commit(&service_buffer, frame.id);
            portEXIT_CRITICAL(&report_buffer_lock);
            sent++;
        }
        else
        {
            sending = false;
        }
    }
    return sent;
}

/*
 * Number of reports waiting to be sent. Design Document, Chapter 6,
 * Section 6.1, Requirement 3.2.
 */
uint32_t report_buffer_service_count(void)
{
    uint32_t count;

    portENTER_CRITICAL(&report_buffer_lock);
    count = report_buffer_count(&service_buffer);
    portEXIT_CRITICAL(&report_buffer_lock);
    return count;
}
