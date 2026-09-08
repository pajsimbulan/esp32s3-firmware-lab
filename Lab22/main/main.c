#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cat_img.h"

#define PIN_SCLK GPIO_NUM_12
#define PIN_MOSI GPIO_NUM_11
#define PIN_CS GPIO_NUM_10
#define PIN_DC GPIO_NUM_14
#define PIN_RST GPIO_NUM_15

#define H_RES 240
#define V_RES 320
#define STRIPE 40
#define STRIPE_PX (H_RES *STRIPE)
#define NBUF 2

static const char *TAG = "LAB22";

static esp_lcd_panel_io_handle_t io;
static esp_lcd_panel_handle_t panel;

static uint16_t *buf[NBUF];
static SemaphoreHandle_t buf_free;
static volatile uint32_t g_done;


/** 
 * 
 static uint16_t band_colour(int band, int frame) {
    static const uint16_t pal[8] = {
        0xF800, 0xFD20, 0xFFE0, 0x07E0, 0X001F, 0X780F, 0x07FF, 0xFFFF,
    };
    return pal[(band+frame) % 8];
}

*/
static bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t h, esp_lcd_panel_io_event_data_t *e, void *ctx) {
    (void) h;
    (void) e;
    (void) ctx;

    BaseType_t hp = pdFALSE;
    g_done++;
    xSemaphoreGiveFromISR(buf_free, &hp);
    return hp == pdTRUE;
}

static void display_init(void) {
    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SCLK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1, //write-only: no MISO wire at all
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = STRIPE_PX * sizeof(uint16_t), // must cover on stripe
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = 10 *1000 * 1000, //10 Mhz Lab 9 -- DMA does the pushing
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0, //CPOL 0, CPHA 0, as lab 9 
        .trans_queue_depth = 10,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io));
    esp_lcd_panel_io_callbacks_t cbs = {.on_color_trans_done = on_color_done};
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io, &cbs, NULL));

    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &pcfg, &panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel,true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,true));
    
}

static void display_task(void *arg) {
    (void) arg;
    buf_free = xSemaphoreCreateCounting(NBUF, NBUF);
    configASSERT(buf_free);

    for(int i=0; i<NBUF; i++) {
        buf[i] = heap_caps_malloc(STRIPE_PX * sizeof(uint16_t),MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if(!buf[i]) {
            ESP_LOGE(TAG, "buffer %d alloc failed", i);
            vTaskDelete(NULL);
        }
    }
    ESP_LOGI(TAG, "%d buffers x %d B DMA-capable", NBUF,(int)(STRIPE_PX * sizeof(uint16_t)));
    int idx = 0, frame =0;
    int64_t t_fill = 0, t_frame0 = esp_timer_get_time();

    while(1) {
        for(int y =0, band =0; y<V_RES; y+= STRIPE, band++) {
            xSemaphoreTake(buf_free, portMAX_DELAY);
            uint16_t *b = buf[idx];
            int64_t f0 = esp_timer_get_time();
            //uint16_t col = band_colour(band,frame);
            //for(int i=0; i<STRIPE_PX; i++) b[i] = col;
            memcpy(b, &cat_img[y * H_RES], STRIPE_PX * sizeof(uint16_t));
            t_fill += esp_timer_get_time() - f0;

            esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 0, y, H_RES, y+STRIPE, b);
            if(err ==ESP_OK) idx = (idx+1) %NBUF;
            else {
                ESP_LOGE(TAG, "draw_bitmap: %s", esp_err_to_name(err));
                xSemaphoreGive(buf_free);
            }
        }
        if(++frame % 60 == 0) {
            int64_t dt = esp_timer_get_time() - t_frame0;
            ESP_LOGI(TAG, "60 frames in %lld ms -> %.1f fps | fill %lld ms | done=%" PRIu32, dt/1000, 60.0*1e6 / (double)dt, t_fill/1000, g_done);
            t_fill=0; 
            t_frame0 = esp_timer_get_time();
        }
    }


}




void app_main(void)
{
    display_init();
    xTaskCreate(display_task, "disp", 4096, NULL, 5, NULL);
}