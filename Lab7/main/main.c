#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_log.h"

#define SDA_PIN 8
#define SCL_PIN 9
#define MPU_ADDR 0x68
#define FS_HZ 1000 //sample rate
#define WIN 256 //samples per feature window (~256ms)
#define U_PORT UART_NUM_1 //UART log output, looped back TX17 -> RX18 (lab 3)
#define TX_PIN 17
#define RX_PIN 18

static const char *TAG = "vib";
static i2c_master_dev_handle_t mpu;
static SemaphoreHandle_t tick_sem; //timer -> sampler
static SemaphoreHandle_t win_ready; //sampler -> feature
static float win_buf[WIN];
static int win_idx = 0;

static void tick_cb(void *arg) {
    xSemaphoreGive(tick_sem); //tiny: just release the sampler (task-dispatch context)
}


static void sampler_task(void *arg) {
    while(1) {
        xSemaphoreTake(tick_sem, portMAX_DELAY);
        uint8_t reg = 0x3b;
        uint8_t r[6];
        i2c_master_transmit_receive(mpu, &reg, 1, r, 6, 1000);
        int16_t ax = (int16_t) ((r[0] << 8) | r[1]);
        int16_t ay = (int16_t) ((r[2] << 8) | r[3]);
        int16_t az = (int16_t) ((r[4] << 8) | r[5]);
        float mag = sqrtf( (float)(ax*ax) + (float)(ay*ay) + (float)(az*az)) / 16384.0f;
        win_buf[win_idx++] = mag;
        if(win_idx >= WIN) {
            win_idx = 0;
            xSemaphoreGive(win_ready);
        }
    }
}

static void feature_task(void *arg) {
    while(1) {
        xSemaphoreTake(win_ready, portMAX_DELAY);
        float mean = 0;
        for (int i=0; i<WIN; i++) mean += win_buf[i];
        mean /= WIN;
        float ss = 0;
        float peak = 0;
        for (int i=0; i<WIN; i++) {
            float d = win_buf[i] - mean;
            ss += d*d;
            if(fabsf(d)>peak) peak = fabsf(d);
        }
        float rms = sqrtf(ss/WIN); // AC RMS of the signal magnitude
        ESP_LOGI(TAG, "rms=%.4f peak=%.4f", rms, peak);

        //UART log: send the result out TX, read it back on RX through the loopback wire
        char line[48];
        int len = snprintf(line, sizeof(line), "rms=%.4f peak=%.4f\r\n", rms, peak);
        uart_flush_input(U_PORT); //drop stale bytes so each read lines up with this message
        uart_write_bytes(U_PORT, line, len);
        uint8_t rx[48];
        int n = uart_read_bytes(U_PORT, rx, len, pdMS_TO_TICKS(20));
        if(n > 0) {
            rx[n] = 0;
            ESP_LOGI(TAG, "uart looped back: %s", (char *)rx);
        } else ESP_LOGW(TAG, "uart: nothing came back, check the TX17 -> RX18 wire");

    }
}


void app_main(void)
{
    // --- I@C bring-up (same as lab 5) ---
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    i2c_new_master_bus(&bus_cfg, &bus);
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU_ADDR,
        .scl_speed_hz = 400000,
    };
    i2c_master_bus_add_device(bus, &dev_cfg, &mpu);
    uint8_t wake[2] = {0x6B, 0x00};
    i2c_master_transmit(mpu, wake, 2, 1000); //wake from sleep

    // --- UART log bring-up (same as lab 3) ---
    uart_config_t ucfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(U_PORT, 1024, 0, 0, NULL, 0);
    uart_param_config(U_PORT, &ucfg);
    uart_set_pin(U_PORT, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // --- plumbing ---
    tick_sem = xSemaphoreCreateBinary();
    win_ready = xSemaphoreCreateBinary();

    //pin the real-time work to core1; core 0 stays free for WiFi later
    xTaskCreatePinnedToCore(sampler_task, "samp", 4096, NULL,  6, NULL, 1);
    xTaskCreatePinnedToCore(feature_task, "feat", 4096, NULL,  5, NULL, 1);

    const esp_timer_create_args_t a = {
        .callback = &tick_cb,
        .name = "fs"
    };
    esp_timer_handle_t h;
    esp_timer_create(&a, &h);
    esp_timer_start_periodic(h, 1000000 / FS_HZ);  //1000 us = 1 kHz
}