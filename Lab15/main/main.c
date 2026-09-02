#include <stdbool.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"

#include "driver/i2c_master.h"
#include "protocol_examples_common.h"

#define HOST_IP "192.168.1.50" // LAPTOP IP
#define FIRMWARE_URL "http://" HOST_IP ":8070/lab15_ota.bin"
#define HEALTH_URL "http://" HOST_IP ":8070/health"

#define I2C_SDA_IO 8 //wiring
#define I2C_SCL_IO 9
#define MPU_ADDR 0x68
#define MPU_WHO_AM_I 0x75

#define FW_VERSION "v1-baseline"

static i2c_master_dev_handle_t s_mpu = NULL;

static const char *TAG = "lab15";


//I2C

static esp_err_t i2c_bringup(void) {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
       .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if(err != ESP_OK) return err;

    i2c_device_config_t dev_cfg =  {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU_ADDR,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(bus,&dev_cfg,&s_mpu);
    if(err != ESP_OK) return err;

    ESP_LOGI(TAG, "i2c: ready");
    return ESP_OK;
}

static esp_err_t mpu_read(uint8_t reg, uint8_t *buf, size_t len) {
    return i2c_master_transmit_receive(s_mpu, &reg, 1, buf, len, pdMS_TO_TICKS(1000));
}

static bool health_check_ok(const char *url, int attempts, int timeout_ms) {
    for(int i = 0; i< attempts; i++) {
        esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = timeout_ms,
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);

        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        //ESP_OK  only means the exchange completed. a 404 completes too.
        if(err == ESP_OK && status >= 200 && status < 300) {
            ESP_LOGI(TAG, "health: OK (status %d)", status);
            return true;
        }
        ESP_LOGW(TAG, "health: attempt %d/%d failed (%s, status %d)", i+1, attempts, esp_err_to_name(err), status);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    return false;
}

static void do_ota(const char *url) {
    esp_http_client_config_t http = {
        .url = url,
        .timeout_ms = 10000,
        //no .crt_bundle_attach for Path A - plain HTTP, gated by
        //CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP
    };
    esp_https_ota_config_t ota = {.http_config = &http};
    ESP_LOGI(TAG, "ota: starting from %s", url);
    esp_err_t err = esp_https_ota(&ota);
    if(err == ESP_OK) {
        ESP_LOGI(TAG, "ota: installed, rebooting into the new slot");
        esp_restart();
    }
    ESP_LOGE(TAG, "ota: failed (%s), staying on the current image", esp_err_to_name(err));
}
static void ota_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(15000));           // window to read the boot log
    do_ota(FIRMWARE_URL);
    vTaskDelete(NULL);                          // only reached if OTA failed
}

static bool self_test(void) {
    // 1. storage
    nvs_handle_t h;
    if(nvs_open("storage", NVS_READONLY,&h) != ESP_OK) {
        ESP_LOGE(TAG, "NVS namespace missing"); return false;
    }
    nvs_close(h);

    //2. sensor identifies itself (your breakout returns 0x70)
    uint8_t who = 0;
    if(mpu_read(0x75, &who, 1) != ESP_OK) {
        ESP_LOGE(TAG, "IMU no ACK");
        return false;
    }
    if(who != 0x70 && who != 0x68 && who != 0x71) {
        ESP_LOGE(TAG, "IMU WHO_AM_I = 0x%02X", who);
        return false;
    }

    //3a. associated - necessary but NOT sufficient
    wifi_ap_record_t ap;
    if(esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        ESP_LOGE(TAG, "selftest: not associated");
        return false;
    }

    //3b. can we actually REACH the update service?  bounded and retried
    if(!health_check_ok(HEALTH_URL,3,3000)) {
        ESP_LOGE(TAG, "selftest: update server unreachable");
        return false;
    }

    ESP_LOGI(TAG, "selftest: PASS(rssi %d)", ap.rssi);
    return true;
}


void app_main(void)
{
    //bring up what the self-test needs
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect()); // network up BEFORE self-test
    ESP_ERROR_CHECK(i2c_bringup()); // sensor up too

    //Create the "storage" namespace so self_test check1 can find it.
    //NVS_READWRITE creates it;  NVS_READONLY (used in the test) does not.
    nvs_handle_t h;
    if(nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "ok",1);
        nvs_commit(h);
        nvs_close(h);
    }

    //which build, which slot
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "boot: %s running from %s", FW_VERSION, running->label);

    //resolve probation if we are on it
    esp_ota_img_states_t st;
    if(esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "ota: on probation -- running self-test");
        if(self_test()) {
            ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
            ESP_LOGI(TAG, "image marked VALID");
        } else {
            ESP_LOGE(TAG, "self-test FAILED -- rolling back");
            esp_ota_mark_app_invalid_rollback_and_reboot(); //does not return
        }
    }
    //normal application
     xTaskCreate(ota_task, "ota", 8192, NULL, 5, NULL);
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "alive(%s on  %s)", FW_VERSION, running->label);
    }

}