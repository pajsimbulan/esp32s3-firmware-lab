#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_log.h"

#define BOOT_GPIO GPIO_NUM_0
#define WIN 256

#ifndef SESSION_ID
#define SESSION_ID 0
#endif

_Static_assert(configTICK_RATE_HZ == 1000,
    "Lab 19 needs a 1 ms tick -- set CONFIG_FREERTOS_HZ=1000 in menuconfig.");

static i2c_master_dev_handle_t imu;
static float win[WIN];
static int widx;

static esp_err_t mpu_read(uint8_t reg, uint8_t *dst, size_t n)
{
return i2c_master_transmit_receive(imu, &reg, 1, dst, n, pdMS_TO_TICKS(100));
}

static esp_err_t mpu_write(uint8_t reg, uint8_t val) {
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(imu, b, sizeof(b), pdMS_TO_TICKS(100));
}

static void imu_init(void) {
    i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_8,
        .scl_io_num = GPIO_NUM_9,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,

    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &bus));
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x68, .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dc, &imu));

    uint8_t who = 0;
    ESP_ERROR_CHECK(mpu_read(0x75, &who, 1));
    ESP_LOGI("imu", "WHO_AM_I = 0x%02X (expect 0x70 on MPU-6500)", who);
    ESP_ERROR_CHECK(mpu_write(0x6B, 0x00));
    vTaskDelay(pdMS_TO_TICKS(100));
}

static float read_magnitude(void) {
    uint8_t r[6];
    if(mpu_read(0x3B, r, 6) != ESP_OK) return NAN;
    int16_t ax = (int16_t)((r[0] << 8) | r[1]);
    int16_t ay = (int16_t)((r[2] << 8) | r[3]);
    int16_t az = (int16_t)((r[4] << 8) | r[5]);
    float x = ax / 16384.0f, y = ay / 16384.0f, z = az / 16384.0f;
    return sqrtf(x*x + y*y + z*z);
}

static void compute_features(const float *w, int n, float *rms, float *peak)
{
    float mean = 0.0f;
    for (int i = 0; i < n; i++) mean += w[i];
    mean /= n;

    float acc = 0.0f, pk = 0.0f;
    for (int i = 0; i < n; i++) {
        float d = w[i] - mean;
        acc += d * d;
        float a = fabsf(d);
        if (a > pk) pk = a;
    }
    *rms  = sqrtf(acc / n);
    *peak = pk;
}

void app_main(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL <<BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    imu_init();

    printf("session,label,rms,peak\n");
    ESP_LOGI("cap", "session %d -- hold BOOT to label windows as 'fault'", SESSION_ID);

    int nwin = 0;
    int64_t t_start = esp_timer_get_time();
    TickType_t next = xTaskGetTickCount();

    while(1) {
        float m = read_magnitude();
        if(!isnan(m)) win[widx++] = m;

        if(widx >= WIN) {
            widx = 0;
            const char *label = (gpio_get_level(BOOT_GPIO) == 0) ? "fault" : "normal";
            float rms, peak;
            compute_features(win, WIN, &rms, &peak);
            printf("%d,%s,%.5f,%.5f\n", SESSION_ID, label, (double)rms, (double)peak);

            if(++nwin %40 == 0) {
                double secs = (esp_timer_get_time() - t_start) / 1e6;
                ESP_LOGI("cap", "%d windows in %.1f s = %.2f win/s (expect 3.90)", nwin, secs, nwin/secs);
            }
        }
        vTaskDelayUntil(&next, pdMS_TO_TICKS(1));
    }
    

}