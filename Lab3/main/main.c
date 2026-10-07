#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"




#define U_PORT UART_NUM_1
#define TX_PIN 17
#define RX_PIN 18
#define BUF 1024

/**
 * 
 */
 void app_main(void)
 {
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
        };
        uart_driver_install(U_PORT, BUF,0,0,NULL,0);
        uart_param_config(U_PORT, &cfg);
        uart_set_pin(U_PORT, TX_PIN, RX_PIN,UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        const char *msg = "ping\r\n";
        uint8_t rx[BUF];
        while(1) {
            uart_write_bytes(U_PORT, msg, strlen(msg));
            int n = uart_read_bytes(U_PORT, rx, sizeof(rx)-1, pdMS_TO_TICKS(50));
            if(n>0) {
                rx[n] = 0;
                ESP_LOGI("lab3", "looped back: %s", (char *)rx);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
}

               
/**\
* try it yourself exercise


#include <stdbool.h>
#include "driver/usb_serial_jtag.h"
#include "led_strip.h"
#define LED_PIN 48 //onboard WS2812, fixed by board wiring  

#define LED_COUNT 1
#define CMD_LINE_MAX  64  //longest command line we accept
#define USB_BUF 10243 //driver ring buffer, must be >=1024
static const char *TAG="lab3";
static led_strip_handle_t strip; 
static bool led_on = false; //shadow state: WS2812 cannot be read back

static void led_init(void) {
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_PIN,
        .max_leds       = LED_COUNT,
};
led_strip_rmt_config_t rmt_cfg = {.resolution_hz = 10*1000*1000,}; //10 Mhz -> 0.1us per tick
ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip));
    led_strip_clear(strip);// start dark
};

static void led_set(bool on) {
    if(on) {
        led_strip_set_pixel(strip, 0, 0, 32, 0); //R,G,B,  green dim
        led_strip_refresh(strip); //nothing lights until refresh
    } else led_strip_clear(strip);
    led_on = on;
}

static void handle_command(const char *line) {
    if(line[0] == '\0') return;
    if(strcmp(line, "led on") == 0) {
        led_set(true);
        ESP_LOGI(TAG, "LED on");
    }
    else if (strcmp(line, "led off") == 0) {
        led_set(false);
        ESP_LOGI(TAG, "LED off");
    }
    else if (strcmp(line, "led status") == 0) ESP_LOGI(TAG, "LED is %s", led_on? "on":"off");
    else ESP_LOGW(TAG, "unknown command: '%s'", line);
}

void app_main(void) {
    led_init();
    usb_serial_jtag_driver_config_t usb_cfg = {
        .tx_buffer_size = USB_BUF,
        .rx_buffer_size = USB_BUF,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_cfg));
    ESP_LOGI(TAG, "ready. commands: led on/ led off / led status");
    uint8_t rx[64];  //scratch: bytes that arrived this iteration
    
    char line[CMD_LINE_MAX];
    int idx = 0;
    
    while(1) {
        int n = usb_serial_jtag_read_bytes(rx, sizeof(rx), pdMS_TO_TICKS(20));
        for (int i =0; i<n; i++) {
            char c = (char) rx[i];
            usb_serial_jtag_write_bytes(&rx[i], 1, 0);
            if(c=='\r' || c == '\n') {
                line[idx] = '\0';
                handle_command(line);
                idx = 0;  //reset for the next command
            } 
            else if (idx <CMD_LINE_MAX -1) line[idx++] = c;
            else {
                ESP_LOGW(TAG, "line too long, dropping input");
                idx =0;
            }
        }
        
    }
}
    */