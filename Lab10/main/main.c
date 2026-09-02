#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); //standard boot pattern: wipe & retry if layout changed
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &h)); //"storage" = a namespace

    int32_t boots = 0;
    nvs_get_i32(h,"boots", &boots); //NOT_FOUDN on first boot -> boot stays 0
    boots++;
    ESP_ERROR_CHECK(nvs_set_i32(h, "boots", boots));
    ESP_ERROR_CHECK(nvs_commit(h)); //nothing persists until THIS line
    nvs_close(h);

    ESP_LOGI("lab10", "this device has booted %ld times", boots);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();  //reboot so you can watch it climb

}