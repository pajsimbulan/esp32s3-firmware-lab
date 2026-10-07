#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "LAB21";

#define WIN 8

typedef struct {
 float win[WIN];
 uint32_t win_len; // the consumer's loop bound. The producer never
 uint32_t frames; // writes this -- so any change to it is the bug.
} shared_t;

static shared_t g = {.win_len = WIN};

_Static_assert(offsetof(shared_t, win_len) == sizeof(float) * WIN, "win_len must sit immediately after win[] for this lab to work");

static void producer_task(void *arg) {
    (void)arg ;
    uint32_t n = 0; 
    while(1) {
        for(int i=0; i< WIN; i++) g.win[i] = (float) (n+i);
        g.frames++;
        n++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void consumer_task(void *arg) {
    (void) arg;
    while(1) {
        float sum = 0.0f;
        for(uint32_t i =0; i<g.win_len;i++) sum += g.win[i];
        ESP_LOGI(TAG, "len=%"PRIu32" sum=%.1f frames=%"PRIu32, g.win_len, (double)sum, g.frames);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}



void app_main(void)
{
    ESP_LOGW(TAG, "win=%p win_len=%p (adjacent by construction)", (void*)g.win, (void*)&g.win_len);
    xTaskCreate(producer_task,"prod",4096, NULL, 5, NULL);   
    xTaskCreate(consumer_task,"cons",4096, NULL, 5, NULL);  
}