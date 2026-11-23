idf_component_register(SRCS "app_main.c" "weight_task.c"
                       INCLUDE_DIRS "."
                       REQUIRES ble led weight weight_monitor persistence calibration wall_clock
                                report_buffer
                                esp_timer freertos)
