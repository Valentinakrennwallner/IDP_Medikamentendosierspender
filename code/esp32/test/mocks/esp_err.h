#ifndef MOCK_ESP_ERR_H
#define MOCK_ESP_ERR_H

/* Host stand-in for the ESP-IDF error codes (GPIO and NVS mocks). */
typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_NVS_BASE 0x1100
#define ESP_ERR_NVS_NOT_FOUND (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_INVALID_LENGTH (ESP_ERR_NVS_BASE + 0x0c)
#define ESP_ERR_NVS_NO_FREE_PAGES (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x10)

#endif
