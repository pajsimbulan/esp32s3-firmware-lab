#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define TX_PIN GPIO_NUM_5
#define RX_PIN GPIO_NUM_4
#define MY_ID 0x123 //ID of this node
#define OTHER_ID 0x456 // used for case 2

#define FILTER_CODE(id) ((uint32_t)(id) << 21)
#define FILTER_MASK_EXACT 0x001FFFFFu

static const char *TAG = "LAB17";

static uint32_t g_tx_ok, g_tx_fail, g_rx_ok, g_busoff, g_recovered;

static const char *state_name(twai_state_t s) {
    switch(s) {
        case TWAI_STATE_STOPPED: return "STOPPED";
        case TWAI_STATE_RUNNING: return "RUNNING";
        case TWAI_STATE_BUS_OFF: return "BUSS_OFF";
        case TWAI_STATE_RECOVERING: return "RECOVERING";
        default:                 return "?";
        
    }
}

static void log_status(const char *why) {
    twai_status_info_t st;
    if(twai_get_status_info(&st) != ESP_OK) return;
    ESP_LOGI(TAG, "[%s] state=%s tx_err=%" PRIu32 " rx_err=%" PRIu32 " tx_q=%" PRIu32 " rx_q=%" PRIu32, why,
    state_name(st.state), st.tx_error_counter, st.rx_error_counter,
    st.msgs_to_tx, st.msgs_to_rx);
}

static void tx_task(void *arg) {
    (void)arg;
    uint8_t counter = 0;
    while(1) {
        twai_message_t tx = {
            .identifier = MY_ID,
            .data_length_code = 5,
            .self = 1, 
            .data = {0xDE, 0xAD, 0xBE,0xEF ,counter},
        };

        esp_err_t err = twai_transmit(&tx, pdMS_TO_TICKS(1000));
        if(err == ESP_OK) g_tx_ok++;
        else {
            g_tx_fail++;
            ESP_LOGW(TAG, "tx failed: %s", esp_err_to_name(err));
        }
        counter++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void rx_task(void *arg) {
    (void)arg;
    while(1) {
        twai_message_t rx;
        esp_err_t err = twai_receive(&rx, pdMS_TO_TICKS(2000));

        if(err == ESP_OK) {
            g_rx_ok++;
            char buf[(3*8)+1] = {0};
            for(int i=0; i<rx.data_length_code && i<8; i++) sprintf(buf + (i*3), "%02X ", rx.data[i]);
            ESP_LOGI(TAG, "RX id=0x%03" PRIX32 " dlc=%d data=[ %s]", rx.identifier, rx.data_length_code, buf);
        } else if (err == ESP_ERR_TIMEOUT) ESP_LOGW(TAG, "no frame in 2 s");
        else ESP_LOGE(TAG, "rx error: %s", esp_err_to_name(err));
    }
}

static void alert_task(void *arg) {
    (void) arg;
    while(1) {
        uint32_t alerts;
        if(twai_read_alerts(&alerts, pdMS_TO_TICKS(1000)) != ESP_OK) continue;
        if(alerts & TWAI_ALERT_ERR_PASS) ESP_LOGW(TAG, "ERROR PASSIVE -- an error counter passed 127");
        if(alerts & TWAI_ALERT_BUS_ERROR) ESP_LOGW(TAG, "bus error detected");
        if(alerts & TWAI_ALERT_TX_FAILED) ESP_LOGW(TAG, "a transmission failed after retries");
        if(alerts & TWAI_ALERT_RX_QUEUE_FULL) ESP_LOGW(TAG, "rx queue full -- frames were dropped");

        if(alerts & TWAI_ALERT_BUS_OFF) {
            g_busoff++;
            ESP_LOGE(TAG, "BUS-OFF. the node removed itself from the bus");
            log_status("bus-off");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_err_t e = twai_initiate_recovery();
            if(e != ESP_OK) ESP_LOGE(TAG, "recovery start faied: %s", esp_err_to_name(e));
        }
        if(alerts & TWAI_ALERT_BUS_RECOVERED) {
            g_recovered++;
            ESP_LOGW(TAG, "recovered. restarting the driver");
            esp_err_t e = twai_start();
            if(e != ESP_OK) ESP_LOGE(TAG, "restart faied: %s", esp_err_to_name(e));
            log_status("recovered");
        }

    }
}

static void stats_task(void *arg) {
    (void)arg;
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGW(TAG, "tx_ok=%" PRIu32 " tx_fail=%" PRIu32 " rx_ok=%" PRIu32 " bus_off=%" PRIu32 " recovered=%" PRIu32,
        g_tx_ok, g_tx_fail, g_rx_ok, g_busoff, g_recovered);
        log_status("periodic");
    }
}



void app_main(void)
{
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(TX_PIN, RX_PIN, TWAI_MODE_NO_ACK);
    g.alerts_enabled = TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_TX_FAILED | TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED;
    g.rx_queue_len = 10;
    g.tx_queue_len = 10;

    //500 kbits/s = 20Tq of 100ns , sample point at 80%
    //Proof case 5 swaps this for TWAI_TIMING_CONFIG_125KBITS()
    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();

    twai_filter_config_t f = {
        .acceptance_code = FILTER_CODE(MY_ID),
        .acceptance_mask = FILTER_MASK_EXACT,
        .single_filter   = true,
    };

    ESP_ERROR_CHECK(twai_driver_install(&g, &t, &f));
    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(TAG, "TWAI up at 500 kbits/s, accepting only 0x%03X " 
    "(code 0x%08" PRIX32 ", mask 0x%08" PRIX32 ")",MY_ID, FILTER_CODE(MY_ID), (uint32_t)FILTER_MASK_EXACT);
    log_status("start");

    //Check every task creation. A silent failure here shows up Later as a
    //mystery --- no frames received, or no alerts handled, with no clue why,
    BaseType_t ok = pdPASS;
    ok &= xTaskCreate(rx_task, "rx", 4096, NULL, 6, NULL);
    ok &= xTaskCreate(alert_task, "alert", 4096, NULL, 7, NULL);
    ok &= xTaskCreate(tx_task, "tx", 4096, NULL, 5, NULL);
    ok &= xTaskCreate(stats_task, "stats", 4096, NULL, 3, NULL);
    if(ok != pdPASS) {
        ESP_LOGE(TAG, "task creation failed -- out of heap?");
        abort();
    }


}