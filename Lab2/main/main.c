#include "freertos/freeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "lab2";

static void heartbeat_task(void *arg) {
    TickType_t last = xTaskGetTickCount();
    while(1) {
        ESP_LOGI(TAG, "heartbeat");
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000)); //wakes on a fixed 1 Hz grid -- no drift
    }

}

static void worker_task(void *arg) {
    int n = 0;
    while(1) {
        ESP_LOGI(TAG, "worker: %d", n++);
        vTaskDelay(pdMS_TO_TICKS(250));  //~4 Hz, drifts by however long the log took
    }
}

void app_main(void)
{
    xTaskCreate(heartbeat_task, "hb", 2048, NULL, 5, NULL); //name, stack bytes, arg, priority, handle
    xTaskCreate(worker_task, "wk", 2048, NULL, 5, NULL);
    //app_main can return here -- the two tasks keep running under the scheduler
}