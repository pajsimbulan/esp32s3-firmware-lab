#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_log.h"

#define PIN_MOSI 11
#define PIN_MISO 13
#define PIN_SCLK 12
#define PIN_CS 10

static spi_device_handle_t dev;

void app_main(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1, //-1 = unused (only for quad/octal modes)
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO)); //SPI1 is flash
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1*1000*1000, //1Mhz
        .mode = 0, //CPOL=0, CPHA=0
        .spics_io_num = PIN_CS, //driver toggles CS around each transaction
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &dev));

    uint8_t tx[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t rx[4] = {0};
    spi_transaction_t t = {
        .length = 8 *sizeof(tx), //Length is in bitsm not bytes --classic trap
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    while(1) {
        memset(rx, 0, sizeof(rx));
        ESP_ERROR_CHECK(spi_device_transmit(dev, &t));
        ESP_LOGI("lab9", "rx: %02X %02X %02X %02X", rx[0], rx[1], rx[2], rx[3]);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}