#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define DEVICE_NAME "lab14-vib"

static const char *TAG = "lab14";

// 128-bit UUIDs, generated once. NimBLE takes these LSB first.
// service      2f9b0001-5c7e-4d3a-9c21-a1f0d4e77b10
// rms char     2f9b0002-5c7e-4d3a-9c21-a1f0d4e77b10
static const ble_uuid128_t vib_svc_uuid =
    BLE_UUID128_INIT(0x10, 0x7b, 0xe7, 0xd4, 0xf0, 0xa1, 0x21, 0x9c,
                     0x3a, 0x4d, 0x7e, 0x5c, 0x01, 0x00, 0x9b, 0x2f);

static const ble_uuid128_t vib_chr_uuid =
    BLE_UUID128_INIT(0x10, 0x7b, 0xe7, 0xd4, 0xf0, 0xa1, 0x21, 0x9c,
                     0x3a, 0x4d, 0x7e, 0x5c, 0x02, 0x00, 0x9b, 0x2f);

static uint16_t vib_val_handle = 0;                       // stack fills this in
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;  // who to notify
static float s_rms = 0.0f;                                // the published value

static uint8_t s_own_addr_type;
static void start_advertising(void);

// GATT ------------------------------------------------------------------

// called when the phone READS. pull, not push.
static int vib_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;

    // four raw little-endian IEEE-754 bytes. BLE carries no type info.
    int rc = os_mbuf_append(ctxt->om, &s_rms, sizeof(s_rms));
    return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// file scope on purpose: the stack keeps a pointer to this table and walks it
// long after registration returns.
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &vib_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid       = &vib_chr_uuid.u,
                .access_cb  = vib_access_cb,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &vib_val_handle,
            },
            { 0 }   // end of characteristics
        },
    },
    { 0 }           // end of services
};

static void notify_rms(uint16_t conn_handle, float rms)
{
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) return;

    // copies, so rms can go out of scope right after. can return NULL under
    // memory pressure, which the manual's listing does not check and this does.
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&rms, sizeof(rms));
    if (om == NULL) {
        ESP_LOGW(TAG, "notify: out of mbufs");
        return;
    }

    // succeeds whether or not anyone subscribed. silence is not an error here.
    int rc = ble_gatts_notify_custom(conn_handle, vib_val_handle, om);
    if (rc != 0) ESP_LOGW(TAG, "notify: rc=%d", rc);
}

// GAP -------------------------------------------------------------------

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "connected (handle %d)", s_conn_handle);
        } else {
            ESP_LOGW(TAG, "connect failed (status %d)", event->connect.status);
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected (reason %d)", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;   // stale handle = dead notify
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        // this is the stage everyone skips. no subscription, no notifications.
        ESP_LOGI(TAG, "subscribe: handle %d notify=%d",
                 event->subscribe.attr_handle, event->subscribe.cur_notify);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu = %d", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;  // LE only
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    // adv packet max is 31 bytes: flags(3) + tx power(3) + name(11) = 17 fits,
    // adding the 128-bit UUID (18) would make 35, so it goes in the scan response
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields rc=%d (payload too big?)", rc);
        return;
    }

    struct ble_hs_adv_fields rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.uuids128 = (ble_uuid128_t *)&vib_svc_uuid;
    rsp.num_uuids128 = 1;
    rsp.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_rsp_set_fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    if (rc != 0) ESP_LOGE(TAG, "adv_start rc=%d", rc);
    else         ESP_LOGI(TAG, "advertising as %s", name);
}

// host ------------------------------------------------------------------

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type); 
    if (rc != 0) { ESP_LOGE(TAG, "infer_auto rc=%d", rc); return; }

    uint8_t addr[6] = {0};
    ble_hs_id_copy_addr(s_own_addr_type, addr, NULL);
    ESP_LOGI(TAG, "addr %02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "host reset, reason %d", reason);
}

static void host_task(void *param)
{
    nimble_port_run();              // blocks until nimble_port_stop()
    nimble_port_freertos_deinit();
}

// stand-in for the Lab 7 feature task. replace with the real rms.
static void rms_task(void *arg)
{
    float t = 0.0f;
    while (1) {
        s_rms = 0.05f + 0.04f * sinf(t);
        t += 0.2f;

        notify_rms(s_conn_handle, s_rms);
        ESP_LOGI(TAG, "rms = %.4f", s_rms);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();   // BLE keeps calibration data in NVS
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    // these return a NimBLE rc, not an esp_err_t, so check them by hand
    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "count_cfg rc=%d", rc); return; }

    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "add_svcs rc=%d", rc); return; }

    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) { ESP_LOGE(TAG, "name_set rc=%d", rc); return; }

    nimble_port_freertos_init(host_task);

    xTaskCreate(rms_task, "rms", 4096, NULL, 5, NULL);
}
