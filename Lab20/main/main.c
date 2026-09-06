#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_continuous.h"
#include "esp_timer.h"
#include "esp_log.h"


#define SAMPLE_HZ 20000
#define FRAME_BYTES 1024 //256 samples -> 12.8ms
#define POOL_BYTES (FRAME_BYTES*4) //4 frames -> 12.8*3 = 38.4 ms of slack
#define STALL_MS 200  //set to 200 for proof case 2

static const char *TAG = "LAB20";

static adc_continuous_handle_t adc;
static TaskHandle_t consumer;

static volatile uint32_t g_frames, g_samples, g_bad_meta, g_ovf_cb, g_ovf_read;

static bool IRAM_ATTR on_conv_done(adc_continuous_handle_t h, const adc_continuous_evt_data_t *e, void *u) {
    BaseType_t hp = pdFALSE;
    vTaskNotifyGiveFromISR(consumer, &hp); //signal only, move no data here
    return hp == pdTRUE;
}

static bool IRAM_ATTR on_pool_ovf(adc_continuous_handle_t h, const adc_continuous_evt_data_t *e, void *u) {
g_ovf_cb++; 
return false;
}

static void consumer_task(void *arg) {
    (void) arg;
    uint8_t frame[FRAME_BYTES];
    uint32_t got = 0;
    int64_t t0 = esp_timer_get_time();
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if(STALL_MS) vTaskDelay(pdMS_TO_TICKS(STALL_MS)); // for case 2
        while(1) {
            esp_err_t err = adc_continuous_read(adc, frame, FRAME_BYTES, &got, 0);
            if(err == ESP_OK) {
                g_frames++;
                int n = got/SOC_ADC_DIGI_RESULT_BYTES;
                uint32_t sum = 0;
                int valid = 0;
                for(int i=0; i<n; i++) {
                    adc_digi_output_data_t *s = (adc_digi_output_data_t *) &frame[i*SOC_ADC_DIGI_RESULT_BYTES];
                    if(s->type2.unit != ADC_UNIT_1 || s->type2.channel != ADC_CHANNEL_0) {
                        g_bad_meta++;
                        continue;
                    }
                    sum += s->type2.data;
                    valid++;
                }
                g_samples += valid;
                if(valid && (g_frames%78) == 0) {
                    //~1 Hz at 78.125 fps
                    int64_t dt = esp_timer_get_time() - t0;
                    ESP_LOGI(TAG,"frames=%" PRIu32 " rate=%.1f/s mean=%" PRIu32
                                " ovf_cb=%" PRIu32 " ovf_read=%" PRIu32 " bad=%" PRIu32,
                                g_frames, g_frames * 1e6 / (double)dt,
                                sum / valid, g_ovf_cb, g_ovf_read, g_bad_meta);
                }
            } else if(err == ESP_ERR_INVALID_STATE) {
                g_ovf_read++;
                ESP_LOGW(TAG, "pool overflow -- samples lost "
                            "(events=%" PRIu32 " reads=%" PRIu32 ")",
                            g_ovf_cb, g_ovf_read);
                            break;

            } else if (err == ESP_ERR_TIMEOUT) break;
            else {
                ESP_LOGE(TAG, "unexpected read error: %s", esp_err_to_name(err));
 break;
            }
        }
    }
}

void app_main(void)
{
    xTaskCreate(consumer_task, "adc", 4096, NULL, 5, &consumer);
    adc_continuous_handle_cfg_t pool = {
        .max_store_buf_size = POOL_BYTES,
        .conv_frame_size = FRAME_BYTES,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&pool, &adc));

    adc_digi_pattern_config_t pattern = {
        .atten = ADC_ATTEN_DB_12, //widest range, roughly 0 to 3.3v
        .channel = ADC_CHANNEL_0, //GPIO 1
        .unit = ADC_UNIT_1,//ADC1 continuous, ADC2 not supported
        .bit_width = ADC_BITWIDTH_12, //4096 codes
    };
    adc_continuous_config_t cfg = {
        .sample_freq_hz = SAMPLE_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
        .pattern_num = 1,
        .adc_pattern = &pattern,
    };
    ESP_ERROR_CHECK(adc_continuous_config(adc, &cfg));

    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = on_conv_done,
        .on_pool_ovf = on_pool_ovf, 
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc, &cbs, NULL));
    ESP_ERROR_CHECK(adc_continuous_start(adc));
    ESP_LOGI(TAG, "running: %d Hz, %d B frames, %d B pool (%d frames, ~%.1f ms slack)",
        SAMPLE_HZ, FRAME_BYTES, POOL_BYTES,
        POOL_BYTES / FRAME_BYTES,
        (POOL_BYTES / FRAME_BYTES - 1) *
        (FRAME_BYTES / SOC_ADC_DIGI_RESULT_BYTES) * 1000.0 / SAMPLE_HZ);
}