#include <stdio.h>
#include "esp_netif.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h" //provides example_connect()

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_event.h"

static void post_reading(float rms, float peak) {
    char body[96];
    int n = snprintf(body, sizeof(body), "{\"rms\":%.4f,\"peak\":%.4f}", rms, peak);

    esp_http_client_config_t cfg = {
        .url = "http://172.20.10.5:8000/telemetry",
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_post_field(c,body,n);

    if(esp_http_client_perform(c) == ESP_OK) ESP_LOGI("lab13", "POST -> HTTP %d", esp_http_client_get_status_code(c));
    else ESP_LOGE("lab13", "POST failed");
    esp_http_client_cleanup(c);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init()); //Wi-Fi keeps calibration in NVS
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect()); // blocks until STA has an IP
    

    post_reading(0.0123f, 0.0456f);
}   