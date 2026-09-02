#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "led_strip.h"

#define RGB_GPIO 48
#define BOOT_GPIO 0

static led_strip_handle_t strip;

static void rgb_init(void) {
    led_strip_config_t sc = { 
        .strip_gpio_num = RGB_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rc = {
        .resolution_hz = 10 * 1000 *1000, //10 MHz RMT tick
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&sc, &rc, &strip));
    led_strip_clear(strip);
}


void app_main(void)
{
    rgb_init();

    gpio_config_t btn = {                   //replaces RCGCGPIO + DIR + DEN + Pull setup
        .pin_bit_mask = 1ULL << BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, //button pulls the line LOW when pressed 
    };
    gpio_config(&btn);
    int index = 0;
    // bool on = false;
    while(1) {
        if(gpio_get_level(BOOT_GPIO) == 0 ) //active low: pressed == 0
        {
            // on = !on;
            index = (index+1) %4;
            //crude debounce: wait for release
            while(gpio_get_level(BOOT_GPIO) == 0) vTaskDelay(pdMS_TO_TICKS(10));
        }
        switch(index) {
            case 0: led_strip_set_pixel(strip,0,200,0,0); break; //dim red
            case 1: led_strip_set_pixel(strip,0,0,200,0); break; //dim green
            case 2: led_strip_set_pixel(strip,0,0,0,200); break; //dim blue
            case 3: led_strip_clear(strip); break; //off
        }
        led_strip_refresh(strip); //nothing shows until you refresh
        vTaskDelay(pdMS_TO_TICKS(20));

    }
}