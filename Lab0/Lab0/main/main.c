#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

void app_main(void)
{

   printf("Hello from ESP32-S3!\n");
    for (int i=10; i>0; i--) {
        printf("restarting in %d... \n", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    esp_restart();

}