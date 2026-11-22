#ifndef MOCK_NVS_FLASH_H
#define MOCK_NVS_FLASH_H

#include "esp_err.h"

/* Host stand-in for the ESP-IDF NVS flash initialisation; implemented by the test. */
esp_err_t nvs_flash_init(void);

#endif
