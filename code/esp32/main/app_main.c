#include "esp_log.h"
#include "led_status.h"

static const char *TAG = "meddispenser";

/* ESP-IDF entry point for initializing the sensor-node application. */
void app_main(void)
{
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
}
