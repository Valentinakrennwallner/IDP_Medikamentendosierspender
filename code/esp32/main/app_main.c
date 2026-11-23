/*
 * ESP32 application start (task 21, measurement part): LED self-test,
 * loading the calibration from NVS and starting the weight-measurement
 * thread. Without a valid calibration the serial calibration is started and
 * no evaluation runs (fail closed, Requirement 2.8).
 *
 * Still open in task 21: BLE connection and schedule/time reception, the
 * clock, the filling check, the dose-evaluation thread, the result buffer
 * and the heartbeat. Note: the Design Document flowchart (Chapter 3) loads
 * the calibration after the BLE connection; it is loaded first here because
 * BLE is not wired yet. The order is revisited when BLE is added.
 */
#include <stdbool.h>
#include <stdint.h>

#include "calibration.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_status.h"
#include "persistence.h"
#include "weight_task.h"

static const char *TAG = "meddispenser";

/* Pause before the calibration dialog is offered again after a failure. */
#define APP_CALIBRATION_RETRY_DELAY_MS UINT32_C(1000)

/*
 * Run the serial calibration until it succeeds, then restart so that the
 * new calibration is loaded through the normal start-up path. After a
 * cancellation or an error the red LED flashes and the dialog is offered
 * again; no evaluation runs meanwhile. Never returns.
 * Design Document, Chapter 2, Section 2.1 (weight_calibrate()), and
 * Chapter 6, Section 6.1, Requirement 2.8.
 */
static void app_run_calibration(void)
{
    calibration_result_t result;

    for (;;)
    {
        ESP_LOGW(TAG, "No valid calibration: starting serial calibration");
        result = weight_calibrate();
        if (result == CALIBRATION_OK)
        {
            ESP_LOGI(TAG, "Calibration saved, restarting");
            esp_restart();
        }
        ESP_LOGE(TAG, "Calibration not completed (result %d)", (int)result);
        if (led_status_set(LED_STATUS_FAULT_FLASH) != ESP_OK)
        {
            ESP_LOGE(TAG, "RGB LED update failed");
        }
        vTaskDelay(pdMS_TO_TICKS(APP_CALIBRATION_RETRY_DELAY_MS));
    }
}

/*
 * Load the calibration from NVS. Returns true only for a valid stored
 * calibration; any NVS or data error counts as "no calibration" (fail
 * closed). Design Document, Chapter 2, Section 2.1, and Chapter 6,
 * Section 6.1, Requirement 2.8.
 */
static bool app_load_calibration(weight_calibration_t *calibration)
{
    persistence_result_t result = persistence_init();

    if (result == PERSISTENCE_OK)
    {
        result = persistence_calibration_load(calibration);
    }
    if (result != PERSISTENCE_OK)
    {
        ESP_LOGW(TAG, "Calibration not available (result %d)", (int)result);
    }
    return (result == PERSISTENCE_OK);
}

/*
 * ESP-IDF entry point: LED self-test, load the calibration (or run the
 * serial calibration), then start the weight-measurement thread.
 * Design Document, Chapter 2, Section 2.1 (system start), and Chapter 3
 * (ESP32 flowchart).
 */
void app_main(void)
{
    weight_calibration_t calibration;
    esp_err_t result;

    ESP_LOGI(TAG, "ESP32 application started");
    ESP_LOGW(TAG, "BLE secure transport remains disabled until a commissioned PSK is loaded");

    result = led_status_init();
    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "RGB LED initialization failed: %s", esp_err_to_name(result));
        return;
    }

    result = led_status_self_test();
    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "RGB LED self-test failed: %s", esp_err_to_name(result));
        return;
    }

    if (!app_load_calibration(&calibration))
    {
        app_run_calibration();
    }

    result = weight_task_start(&calibration);
    if (result != ESP_OK)
    {
        /* No measurement and no evaluation: flash red (fail closed). */
        ESP_LOGE(TAG, "Weight measurement could not be started: %s", esp_err_to_name(result));
        (void)led_status_set(LED_STATUS_FAULT_FLASH);
        return;
    }
    ESP_LOGI(TAG, "Weight measurement started");
}
