#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define PROBE_GPIO 4

static volatile uint32_t samples = 0;

static void sample_cb(void *arg) {
    samples++;
    gpio_set_level(PROBE_GPIO,samples & 1);
}

void app_main(void)
{
    gpio_set_direction(PROBE_GPIO, GPIO_MODE_OUTPUT);
    const esp_timer_create_args_t a= {
        .callback = &sample_cb,
        .name = "sampler",
    };
    esp_timer_handle_t h;
    esp_timer_create(&a, &h);
    esp_timer_start_periodic(h,1000);

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI("lab4", "samples in the last second: %lu", samples);
        samples = 0;
    }
}