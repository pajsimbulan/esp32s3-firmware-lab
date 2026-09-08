#include <stdio.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/i2c_master.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_flash.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define TEST_WINDOW_MS 3000 //how long we listen for magic string
#define MAGIC "TESTMODE"

static i2c_master_dev_handle_t imu;
static char g_serial[13];

static esp_err_t mpu_read(uint8_t reg, uint8_t *dst, size_t n) {
    return i2c_master_transmit_receive(imu, &reg, 1, dst, n, pdMS_TO_TICKS(100));
}

static esp_err_t mpu_write(uint8_t reg, uint8_t val) {
    uint8_t b[2] = {reg,val};
    return i2c_master_transmit(imu, b, 2, pdMS_TO_TICKS(100));
}

static esp_err_t i2c_bringup(void) {
    i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0, 
        .sda_io_num = GPIO_NUM_8,
        .scl_io_num = GPIO_NUM_9,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    esp_err_t e = i2c_new_master_bus(&bc, &bus);
    if(e != ESP_OK) return e;
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x68, 
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &dc, &imu);
}

static bool test_flash(void) {
    uint32_t sz = 0;
    if(esp_flash_get_size(NULL, &sz) != ESP_OK) return false;
    return sz == 16*1024*1024; //catches an 8MB module fitted by mistake
}

static bool test_psram(void) {
    //Tests if chip is present and sdkconfig enabled it
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 4 *1024*1024;
}

static bool test_imu_present(void) {
    uint8_t who = 0;
    if(mpu_read(0x75, &who, 1) != ESP_OK) return false;
    return who == 0x70 || who == 0x68 || who == 0x71;
}

static bool test_imu_sane(void) {
    if(mpu_write(0x6B, 0x00) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(100));
    uint8_t r[6];
    if(mpu_read(0x3B, r, 6) != ESP_OK) return false;
    float ax = (int16_t) ((r[0] << 8) | r[1]) / 16384.0f; 
    float ay = (int16_t) ((r[2] << 8) | r[3]) / 16384.0f; 
    float az = (int16_t) ((r[4] << 8) | r[5]) / 16384.0f; 
    float g = sqrtf(ax*ax + ay*ay + az*az);

    return g>0.85f && g<1.15f;
}

static bool test_provision(void) {
    uint8_t mac[6];
    if(esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return false;
    snprintf(g_serial, sizeof(g_serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
 
    nvs_handle_t h;
    if(nvs_open("factory", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_str(h, "serial", g_serial);
    if(e == ESP_OK) e = nvs_set_u8(h, "tested",1);
    if(e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;

}

static void run_factory_test(void) {
    static const struct {const char *name; bool (*fn)(void);} checks [] = {
        {"FLASH", test_flash},
        {"PSRAM", test_psram},
        {"IMU_PRESENT", test_imu_present},
        {"IMU_SANE", test_imu_sane},
    };
    
    bool all = true;

    for(size_t i =0; i<sizeof(checks)/sizeof(checks[0]); i++) {
        bool ok = checks[i].fn();
        printf("TEST %s %s\n", checks[i].name, ok? "PASS":"FAIL");
        fflush(stdout);
        if(!ok) all = false;
    }

    if(all) {
        bool ok = test_provision();
        printf("TEST PROVISION %s\n", ok? "PASS":"FAIL");
        if(!ok) all = false;
        if(g_serial[0]) printf("SERIAL=%s\n", g_serial);
        else printf("TEST PROVISION SKIPPED\n");
    }
    fflush(stdout);
    printf("RESULT %s\n", all? "PASS":"FAIL");
    fflush(stdout);
}

static void uart_setup(void) {
    uart_config_t c = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0,512,0,0,NULL,0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &c));
}

static bool wait_for_magic(int ms) {
    char buf[32] = {0};
    int len = 0;
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(ms);

    while(xTaskGetTickCount() < end) {
        uint8_t ch;
        if(uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(50)) == 1) {
            if( ch == '\n' || ch == '\r') {
                buf[len] = 0;
                if(strcmp(buf,MAGIC) == 0) return true;
                len=0;
            }else if (len < (int)sizeof(buf)-1) buf[len++] = (char)ch;
        }
    }
    return false;
}



void app_main(void)
{
// ---- UART FIRST. This is a race fix, not a style choice.
 // The host sends its token shortly after reset. If we spent that time in
 // nvs_flash_init() and i2c_bringup() with no UART driver installed, the
 // bytes would land in a peripheral FIFO nobody is draining and be lost --
 // intermittently, depending on how long init happened to take.
 // Installing the driver first means the token is BUFFERED while we are
 // busy, and is still there when we look for it.
    uart_setup();
    printf("\nFACTORY_READY\n");
    fflush(stdout);

    esp_err_t e = nvs_flash_init();
    if(e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init()); 
    }
    ESP_ERROR_CHECK(i2c_bringup());
    if(wait_for_magic(TEST_WINDOW_MS)) {
        run_factory_test();
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart(); 
    }
    ESP_LOGI("app", "normal application starting");
}