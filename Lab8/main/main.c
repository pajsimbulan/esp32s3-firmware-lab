#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BTN_GPIO 0

static QueueHandle_t evt_q;

static void IRAM_ATTR btn_isr(void *arg) { //interrupt contex -- must live in IRAM
    uint32_t gpio = (uint32_t)arg;
    BaseType_t hp = pdFALSE;
    xQueueSendFromISR(evt_q, &gpio, &hp);  //the only safe way to reach a task from an ISR
    if(hp) portYIELD_FROM_ISR();
}

static void worker_task(void *arg) {
    uint32_t gpio;
    while(1) {
        if(xQueueReceive(evt_q, &gpio, portMAX_DELAY)) {
            vTaskDelay(pdMS_TO_TICKS(20)); //debounce settle -- fine here, forbidden in the ISR
            if(gpio_get_level(gpio) == 0) ESP_LOGI("lab8", "button press handled (gpio %lu)", gpio);// still low?  a real press, not a bounce
        }
    }
}

void app_main(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE, //falling edge = the moment of press
    };
    gpio_config(&io);
    evt_q = xQueueCreate(8, sizeof(uint32_t));
    xTaskCreate(worker_task, "worker", 3072, NULL, 10, NULL);
    gpio_install_isr_service(0); // one service dispatches all per-pin ISRs
    gpio_isr_handler_add(BTN_GPIO, btn_isr, (void *)BTN_GPIO);
}