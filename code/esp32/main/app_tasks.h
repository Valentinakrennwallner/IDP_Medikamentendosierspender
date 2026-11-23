#ifndef APP_TASKS_H
#define APP_TASKS_H

/*
 * FreeRTOS priorities and stack sizes of the ESP32 threads. The order
 * follows Table 2 (safety before communication before logging): "high" = 5,
 * "medium" = 4, "low" = 2. Weight measurement and dose evaluation are both
 * "high" and therefore share one priority.
 * Design Document, Chapter 2, Section 2.3 (Table 2).
 *
 * Only the weight-measurement thread exists so far; the other values are
 * reserved for the threads still to be wired in task 21 (dose evaluation,
 * BLE, heartbeat, logging).
 */

#define APP_TASK_PRIORITY_WEIGHT 5U
#define APP_TASK_PRIORITY_EVALUATION 5U
#define APP_TASK_PRIORITY_BLE 4U
#define APP_TASK_PRIORITY_HEARTBEAT 4U
#define APP_TASK_PRIORITY_LOGGING 2U

/* Stack size of the weight-measurement thread in bytes (ESP-IDF unit). */
#define APP_TASK_STACK_WEIGHT_BYTES 4096U

#endif
