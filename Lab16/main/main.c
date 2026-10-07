#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

#define USE_MUTEX 0 //0 = Part A (binary sempahore), 1 =  Part B (mutex)
#define CORE 0 // pin everything to ONE core
#define HOLD_MS 300 //LOW
#define MED_MS 500 //MED
#define TRIALS 30
#define PRIO_LOW 3
#define PRIO_MED 5
#define PRIO_HIGH 8
#define PRIO_CTRL 9 // highest so regains control

static const char *TAG = "lab16";

static SemaphoreHandle_t lock;
static TaskHandle_t h_low, h_med, h_high, h_ctrl;


static int32_t results[TRIALS]; //Written by High only
static int n_results; //Written by High only
static volatile  bool med_ran_during_hold;  //Written by MED, read by CTRL

static void busy(int ms) {
    int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
    while(esp_timer_get_time() <end) {}  
}

static void low_task(void *arg) {
    (void) arg;
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); //1. wait for CTRL
        xSemaphoreTake(lock, portMAX_DELAY); //2. LOW now holds the lock
        ESP_LOGI(TAG," LOW_LOCKED");

        xTaskNotifyGive(h_high); //3. HIGH preempts HERE and blocks
        xTaskNotifyGive(h_med);  //4. MED becomes ready

        ESP_LOGI(TAG, " LOW_RUN"); 
        busy(HOLD_MS); // critical section

        xSemaphoreGive(lock);
        ESP_LOGI(TAG, " LOW_UNLOCKED");

        xTaskNotifyGive(h_ctrl); //6. trial over
    }
}

static void med_task(void *arg) {
    (void)arg;
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        med_ran_during_hold = true; //evidence for the trace check
        ESP_LOGI(TAG, " MED_RUN");
        busy(MED_MS); //never touches the lock
    }
}

static void high_task(void *arg) {
    (void)arg;
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        int64_t t_high_blocked = esp_timer_get_time();  //clock starts BEFORE the take
        ESP_LOGI(TAG, " HIGH_BLOCKED");

        xSemaphoreTake(lock, portMAX_DELAY); //blocks until LOW releases
        int32_t waited_ms = (int32_t)((esp_timer_get_time() - t_high_blocked)/1000);
        ESP_LOGI(TAG, " HIGH_ACQUIRED after %" PRId32 " ms", waited_ms);
        if(n_results<TRIALS) results[n_results++] = waited_ms;

        xSemaphoreGive(lock);
    }
}

static void ctrl_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500)); //let the log settle after boot

    ESP_LOGW(TAG, "=== %s, %d trials ===", USE_MUTEX? "MUTEX (inheritance)" : "BINARY SEMAPHORE", TRIALS);
    
    for(int i=0; i<TRIALS; i++) {
        med_ran_during_hold = false;
        ESP_LOGI(TAG, "trial %d", i+1);

        xTaskNotifyGive(h_low); //start the trial
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); //wait for LOW to finish
        if(!med_ran_during_hold) ESP_LOGE(TAG, "trial %d INVALID: MED never ran", i+1);
        vTaskDelay(pdMS_TO_TICKS(MED_MS + 100)); // let HIGH and MED finish
    }

    // aggregate
    int32_t mn = results[0];
    int32_t mx = results [0];
    int64_t sum = 0;
    for(int i=0; i<n_results;i++) {
        if(results[i] < mn) mn = results[i];
        if(results[i] > mx) mx = results[i];
        sum += results[i];
    }

    ESP_LOGW(TAG, "=========================================");
    ESP_LOGW(TAG, " lock type : %s", USE_MUTEX? "MUTEX" : "BINARY SEMAPHORE");
    ESP_LOGW(TAG, " trials : %d",n_results);
    ESP_LOGW(TAG, " min : %" PRId32 " ms", mn);
    ESP_LOGW(TAG, " mean : %" PRId32 " ms", (int32_t) (sum /n_results));
    ESP_LOGW(TAG, " max : %" PRId32 " ms", mx);
    ESP_LOGW(TAG, " spread : %" PRId32 " ms", mx-mn);
    ESP_LOGW(TAG, "=========================================");

    vTaskDelete(NULL);
}

void app_main(void)
{
    if(USE_MUTEX) {
        lock = xSemaphoreCreateMutex();
        configASSERT(lock);
    } else {
        lock = xSemaphoreCreateBinary();
        configASSERT(lock);
        xSemaphoreGive(lock);
    }

    xTaskCreatePinnedToCore(low_task, "low", 3072, NULL, PRIO_LOW, &h_low, CORE);
    xTaskCreatePinnedToCore(med_task, "med", 3072, NULL, PRIO_MED, &h_med, CORE);
    xTaskCreatePinnedToCore(high_task, "high", 3072, NULL, PRIO_HIGH, &h_high, CORE);
    xTaskCreatePinnedToCore(ctrl_task, "ctrl", 3072, NULL, PRIO_CTRL, &h_ctrl, CORE);
}