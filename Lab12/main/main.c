#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_log.h"

#define SLEEP_US (5* 1000000ULL) //5 seconds in microseconds

static RTC_DATA_ATTR int wake_count = 0; //RTC RAM -> survives deep sleep

void app_main(void)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if(cause == ESP_SLEEP_WAKEUP_TIMER) ESP_LOGI("lab12", "woke from deep sleep; wake #%d", ++wake_count);
    else ESP_LOGI("lab12", "cold boot (power-on or reset)");

    // --- your burst of real work goers here: sample, compute, transmit ---
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI("lab12", "sleeping for 5s");
    esp_sleep_enable_timer_wakeup(SLEEP_US);
    esp_deep_sleep_start(); // does not return --- reboots into app_main on wake
}