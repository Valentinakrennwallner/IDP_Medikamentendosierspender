#ifndef MOCK_DRIVER_GPIO_H
#define MOCK_DRIVER_GPIO_H

#include <stdint.h>

/* Minimal ESP-IDF GPIO declarations so the weight driver builds on the host. */
typedef int esp_err_t;
typedef int gpio_num_t;

#define ESP_OK 0
#define GPIO_NUM_18 18
#define GPIO_NUM_19 19

typedef enum
{
    GPIO_MODE_INPUT,
    GPIO_MODE_OUTPUT
} gpio_mode_t;

typedef enum
{
    GPIO_PULLUP_DISABLE
} gpio_pullup_t;

typedef enum
{
    GPIO_PULLDOWN_DISABLE
} gpio_pulldown_t;

typedef enum
{
    GPIO_INTR_DISABLE
} gpio_int_type_t;

typedef struct
{
    uint64_t pin_bit_mask;
    gpio_mode_t mode;
    gpio_pullup_t pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;

esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(gpio_num_t gpio, uint32_t level);
int gpio_get_level(gpio_num_t gpio);

#endif
