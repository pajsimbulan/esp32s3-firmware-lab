#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define SDA_PIN 8
#define SCL_PIN 9
#define MPU_ADDR 0x68

static const char *TAG = "lab5";
static i2c_master_dev_handle_t mpu;

static esp_err_t mpu_write(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(mpu, buf, 2, 1000);
}

static esp_err_t mpu_read(uint8_t reg, uint8_t *dst, size_t n) {
return i2c_master_transmit_receive(mpu, &reg, 1, dst, n, 1000); //write reg addr, then  read n bytes
}   

void app_main(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, //GY-521 also has its own pull-ups
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU_ADDR,
        .scl_speed_hz = 400000, //400 khz fast mode
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &mpu));

    uint8_t who = 0;
    mpu_read(0x75, &who, 1); //WHO_AM_I register
    ESP_LOGI(TAG, "WHO_AM_I = 0x%02X (expect 0x68)", who);

    mpu_write(0x6B, 0x00); // PWR_MGMT_1 = 0 -> wake from sleep
    vTaskDelay(pdMS_TO_TICKS(50));

    while(1) {
        uint8_t r[6];
        mpu_read(0x3b, r, 6); // ACCEL_XOUT_H .. ACCEL_ZOUT_L, 6 bytes
        int16_t ax = (int16_t) ((r[0]<<8) | r[1]); //each axis is a big endian 16 bit word
        int16_t ay = (int16_t) ((r[2]<<8) | r[3]);
        int16_t az = (int16_t) ((r[4]<<8) | r[5]);

        //default full scale is +/-2g -> 16384 LSB per g
        ESP_LOGI(TAG, "ax=%+.2f ay=%+.2f az=%+.2f (g)", ax / 16384.0f, ay/16384.0f, az/16384.0f);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

}   