#include <stdio.h>
// Removed duplicate #include <stdio.h>
#include <string.h> // Added for string functions
#include <stdlib.h> // Added for malloc, free
#include <ctype.h>  // Added for isdigit, isprint
#include <errno.h>  // Added for errno
// Removed vector, string, map includes (moved to keyboard_layout.cpp)

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" // Correct header for semaphores
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_littlefs.h" // Added for LittleFS
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_mac.h"

#include "lvgl.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_ft5x06.h"

#include "esp_io_expander_tca9554.h"

// Include shared ESP-NOW definitions
#include "esp_now_common.h"

extern "C" {
    #include "esp_bt.h"
    #include "esp_gap_ble_api.h"
    #include "esp_gatts_api.h"
    #include "esp_gatt_defs.h"
    #include "esp_bt_main.h"
    #include "esp_bt_device.h"
    #include "nvs_flash.h" // Already included, but ensure it's here for BLE context
    #include "esp_hidd_prf_api.h"
    #include "hid_dev.h" // Includes keycodes like HID_KEY_DELETE
}

// Include the new keyboard layout header
#include "keyboard_layout.h"
#include "hid_keycodes.h" // Still needed for initialize_keycode_map call

#include <sys/stat.h> // For stat()

// --- Global Variables (Main Application) ---
const char *TAG = "thumbpad"; // Changed TAG for clarity
static SemaphoreHandle_t lvgl_mux = NULL;

// --- Filesystem Configuration ---
#define FS_PARTITION_LABEL "storage"
#define FS_BASE_PATH "/fs"
#define KEYBOARD_LAYOUT_FILE FS_BASE_PATH "/menu.cfg"
// KEYBOARD_BACKUP_FILE is defined in keyboard_layout.cpp

// --- LVGL Event Code ---
#define LV_EVENT_LOAD_LAYOUT ((lv_event_code_t)(_LV_EVENT_LAST + 0)) // Use first available user event

// --- LCD/Touch Defines ---
#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL (16)
#endif

#define LCD_HOST SPI2_HOST
#define TOUCH_HOST I2C_NUM_0

#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL 1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_LCD_CS (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_PCLK (GPIO_NUM_11)
#define EXAMPLE_PIN_NUM_LCD_DATA0 (GPIO_NUM_4)
#define EXAMPLE_PIN_NUM_LCD_DATA1 (GPIO_NUM_5)
#define EXAMPLE_PIN_NUM_LCD_DATA2 (GPIO_NUM_6)
#define EXAMPLE_PIN_NUM_LCD_DATA3 (GPIO_NUM_7)
#define EXAMPLE_PIN_NUM_LCD_RST (-1)
#define EXAMPLE_PIN_NUM_BK_LIGHT (-1)

// The pixel number in horizontal and vertical
#define EXAMPLE_LCD_H_RES 368
#define EXAMPLE_LCD_V_RES 448

// --- Touch Pin Configuration (Always Enabled) ---
#define EXAMPLE_PIN_NUM_TOUCH_SCL (GPIO_NUM_14)
#define EXAMPLE_PIN_NUM_TOUCH_SDA (GPIO_NUM_15)
#define EXAMPLE_PIN_NUM_TOUCH_RST (-1)
#define EXAMPLE_PIN_NUM_TOUCH_INT (GPIO_NUM_21)

esp_lcd_touch_handle_t tp = NULL;
// --- End Touch Pin Configuration ---


#define EXAMPLE_LVGL_BUF_HEIGHT (EXAMPLE_LCD_V_RES / 12) // Keep buffer size reasonable
#define EXAMPLE_LVGL_TICK_PERIOD_MS 2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1
#define EXAMPLE_LVGL_TASK_STACK_SIZE (6 * 1024) // Increased stack for file I/O and parsing
#define EXAMPLE_LVGL_TASK_PRIORITY 2

static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

// --- ESP-Now Globals ---
#define ESP_NOW_BROADCAST_INTERVAL_MS 500 // Interval between broadcasts
#define ESP_NOW_CHANNEL 1 // Define a fixed channel
const uint8_t broadcast_mac_address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // ESP-NOW Broadcast MAC

static uint8_t my_mac_address[ESP_NOW_ETH_ALEN] = {0};   // Store own MAC
volatile uint8_t remote_modifier_mask = 0; // Made volatile

// Callback function when data is sent
static void esp_now_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "ESP-NOW Send Status: Fail");
    } else {
        ESP_LOGD(TAG, "ESP-NOW Send Status: Success");
    }
}

// Callback function when data is received
static void esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *incoming_data, int len) {
    if (recv_info == NULL || incoming_data == NULL || len == 0) {
        ESP_LOGW(TAG, "Invalid ESP-NOW data received (args)");
        return;
    }
    uint8_t *sender_mac = recv_info->src_addr;
    if (sender_mac == NULL) {
        ESP_LOGW(TAG, "Invalid ESP-NOW sender MAC");
        return;
    }
    if (memcmp(sender_mac, my_mac_address, ESP_NOW_ETH_ALEN) == 0) return;

    // Only handle modifier state messages
    if (len == sizeof(esp_now_modifier_state_t)) {
        const esp_now_modifier_state_t *state = (const esp_now_modifier_state_t *)incoming_data;
        remote_modifier_mask = state->modifier_mask;
        ESP_LOGD(TAG, "Received remote modifier update: 0x%02X", remote_modifier_mask);
    } else {
        ESP_LOGD(TAG, "Ignored message: len=%d", len);
    }
}

static void initialize_esp_now(void) {
    ESP_LOGI(TAG, "Initializing ESP-NOW on Channel %d", ESP_NOW_CHANNEL);
    remote_modifier_mask = 0; // Reset remote mask on init

    // 1. Initialize Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret == ESP_ERR_NO_MEM || ret == ESP_FAIL) {
        ESP_ERROR_CHECK(ret);
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_init returned: %s", esp_err_to_name(ret));
    }

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Enable promiscuous mode and set fixed channel
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    // Get own MAC address
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, my_mac_address));
    ESP_LOGI(TAG, "My ESP-NOW MAC: " MACSTR, MAC2STR(my_mac_address));

    // 2. Initialize ESP-NOW
    ret = esp_now_init();
    if (ret == ESP_ERR_ESPNOW_NOT_INIT) {
        ESP_LOGW(TAG, "ESP-NOW already initialized?");
    }
    if (ret != ESP_OK) {
         ESP_LOGE(TAG, "Failed to initialize ESP-NOW: %s", esp_err_to_name(ret));
         return;
    }
    ESP_ERROR_CHECK(esp_now_register_send_cb(esp_now_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(esp_now_recv_cb));

    // 3. Add Broadcast Peer
    esp_now_peer_info_t broadcast_peer_info = {};
    memcpy(broadcast_peer_info.peer_addr, broadcast_mac_address, ESP_NOW_ETH_ALEN);
    broadcast_peer_info.channel = ESP_NOW_CHANNEL;
    broadcast_peer_info.ifidx = WIFI_IF_STA;
    broadcast_peer_info.encrypt = false;
    if (!esp_now_is_peer_exist(broadcast_mac_address)) {
        ret = esp_now_add_peer(&broadcast_peer_info);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add broadcast peer: %s", esp_err_to_name(ret));
            return;
        }
        ESP_LOGI(TAG, "Broadcast peer added.");
    } else {
        ESP_LOGI(TAG, "Broadcast peer already exists.");
    }
    ESP_LOGI(TAG, "ESP-NOW initialized. All communication will use broadcast.");
}

// --- BLE HID Globals & Defines ---
uint16_t hid_conn_id = 0; // Made non-static, declared extern in .h
volatile bool sec_conn = false; // Made volatile, declared extern in .h
#define HIDD_DEVICE_NAME            "ThumbpadKB" // Changed device name

static uint8_t hidd_service_uuid128[] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

static esp_ble_adv_data_t hidd_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x03c1,       // HID Keyboard // 0x03c0 is Generic HID
    .manufacturer_len = 0,
    .p_manufacturer_data =  NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(hidd_service_uuid128),
    .p_service_uuid = hidd_service_uuid128,
    .flag = 0x6, // BR_EDR_NOT_SUPPORTED | LE General Discoverable Mode
};

static esp_ble_adv_params_t hidd_adv_params = {
    .adv_int_min        = 0x20, // 32 * 0.625ms = 20ms
    .adv_int_max        = 0x30, // 48 * 0.625ms = 30ms
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// --- LVGL Callbacks and Helpers ---

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
    const int offsetx1 = area->x1;
    const int offsetx2 = area->x2;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;

#if LCD_BIT_PER_PIXEL == 24 // Keep 24-bit color handling if needed
    uint8_t *to = (uint8_t *)color_map;
    uint8_t temp = 0;
    uint16_t pixel_num = (offsetx2 - offsetx1 + 1) * (offsety2 - offsety1 + 1);

    // Special dealing for first pixel
    temp = color_map[0].ch.blue;
    *to++ = color_map[0].ch.red;
    *to++ = color_map[0].ch.green;
    *to++ = temp;
    // Normal dealing for other pixels
    for (int i = 1; i < pixel_num; i++)
    {
        *to++ = color_map[i].ch.red;
        *to++ = color_map[i].ch.green;
        *to++ = color_map[i].ch.blue;
    }
#endif

    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

/* Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. */
static void example_lvgl_update_cb(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;

    switch (drv->rotated)
    {
    case LV_DISP_ROT_NONE:
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false); // Mirror X
        // Rotate Touch: Mirror Y only
        if (tp) { esp_lcd_touch_set_mirror_x(tp, false); esp_lcd_touch_set_mirror_y(tp, true); esp_lcd_touch_set_swap_xy(tp, false); } // Fix: Use set_swap_xy
        break;
    case LV_DISP_ROT_90:
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true); // Mirror X, Mirror Y
        // Rotate Touch: Mirror X, Mirror Y, Swap XY
        if (tp) { esp_lcd_touch_set_mirror_x(tp, true); esp_lcd_touch_set_mirror_y(tp, true); esp_lcd_touch_set_swap_xy(tp, true); } // Fix: Use set_swap_xy
        break;
    case LV_DISP_ROT_180:
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true); // Mirror Y
        // Rotate Touch: Swap XY only
        if (tp) { esp_lcd_touch_set_mirror_x(tp, false); esp_lcd_touch_set_mirror_y(tp, false); esp_lcd_touch_set_swap_xy(tp, true); } // Fix: Use set_swap_xy
        break;
    case LV_DISP_ROT_270:
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, false); // No mirroring
        // Rotate Touch: Mirror X only
        if (tp) { esp_lcd_touch_set_mirror_x(tp, true); esp_lcd_touch_set_mirror_y(tp, false); esp_lcd_touch_set_swap_xy(tp, false); } // Fix: Use set_swap_xy
        break;
    }
}

void example_lvgl_rounder_cb(struct _lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    // Rounding logic remains the same
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

// --- Touch Callback (Always Enabled) ---
static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp_local = (esp_lcd_touch_handle_t)drv->user_data; // Use local var
    assert(tp_local);

    uint16_t tp_x;
    uint16_t tp_y;
    uint8_t tp_cnt = 0;
    esp_lcd_touch_read_data(tp_local);
    bool tp_pressed = esp_lcd_touch_get_coordinates(tp_local, &tp_x, &tp_y, NULL, &tp_cnt, 1);
    if (tp_pressed && tp_cnt > 0)
    {
        data->point.x = tp_x;
        data->point.y = tp_y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
// --- End Touch Callback ---


static void example_increase_lvgl_tick(void *arg)
{
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

// Public LVGL lock/unlock functions (used by keyboard_layout.cpp indirectly via event system)
bool example_lvgl_lock(int timeout_ms)
{
    assert(lvgl_mux && "lvgl_mux must be created first");
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;
}

void example_lvgl_unlock(void)
{
    assert(lvgl_mux && "lvgl_mux must be created first");
    xSemaphoreGive(lvgl_mux);
}

static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
    while (1)
    {
        if (example_lvgl_lock(-1))
        {
            task_delay_ms = lv_timer_handler();
            example_lvgl_unlock();
        }
        if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

// Callback for HID events
static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch(event) {
        case ESP_HIDD_EVENT_REG_FINISH: {
            if (param->init_finish.state == ESP_HIDD_INIT_OK) {
                ESP_LOGI(TAG, "HID Profile Registered");
                // Name is set, now configure advertising data
                esp_ble_gap_set_device_name(HIDD_DEVICE_NAME);
                esp_ble_gap_config_adv_data(&hidd_adv_data);
                ESP_LOGI(TAG, "Advertising data configured");
            } else {
                ESP_LOGE(TAG, "HID Profile Register failed, error = %d", param->init_finish.state);
            }
            break;
        }
        case ESP_BAT_EVENT_REG: {
             ESP_LOGI(TAG, "Battery Service Registered (if included)");
            break;
        }
        case ESP_HIDD_EVENT_DEINIT_FINISH:
             ESP_LOGI(TAG, "HID Profile Deinitialized");
	     break;
		case ESP_HIDD_EVENT_BLE_CONNECT: {
            ESP_LOGI(TAG, "HID Connected, conn_id = %d", param->connect.conn_id);
            hid_conn_id = param->connect.conn_id;
            break;
        }
        case ESP_HIDD_EVENT_BLE_DISCONNECT: {
            sec_conn = false;
            hid_conn_id = 0; // Reset connection ID
            ESP_LOGI(TAG, "HID Disconnected");
            esp_ble_gap_start_advertising(&hidd_adv_params);
            ESP_LOGI(TAG, "Advertising restarted");
            break;
        }
        case ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT: {
            ESP_LOGI(TAG, "Vendor Report Write Event: conn_id=%d, report_id=%d, len=%d",
                     param->vendor_write.conn_id, param->vendor_write.report_id, param->vendor_write.length);
            ESP_LOG_BUFFER_HEX(TAG, param->vendor_write.data, param->vendor_write.length);
            break;
        }
        case ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT: {
            ESP_LOGI(TAG, "LED Report Write Event: conn_id=%d, report_id=%d, len=%d",
                     param->led_write.conn_id, param->led_write.report_id, param->led_write.length);
            ESP_LOG_BUFFER_HEX(TAG, param->led_write.data, param->led_write.length);
            break;
        }
        default:
            ESP_LOGD(TAG, "Unhandled HID Event: %d", event);
            break;
    }
    return;
}

// Callback for GAP events (advertising, security)
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Advertising data set complete, starting advertising.");
        esp_ble_gap_start_advertising(&hidd_adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Advertising started successfully");
        } else {
            ESP_LOGE(TAG, "Advertising start failed, error = %d", param->adv_start_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
         if (param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Advertising stopped successfully");
        } else {
            ESP_LOGE(TAG, "Advertising stop failed, error = %d", param->adv_stop_cmpl.status);
        }
        break;

     case ESP_GAP_BLE_SEC_REQ_EVT:
        ESP_LOGI(TAG, "Security Request received, accepting.");
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
	 break;
     case ESP_GAP_BLE_AUTH_CMPL_EVT:
        sec_conn = false; // Assume failure until success check
        esp_bd_addr_t bd_addr;
        memcpy(bd_addr, param->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));
        ESP_LOGI(TAG, "Authentication Complete: remote BD_ADDR: %02x:%02x:%02x:%02x:%02x:%02x",
                bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5]);
        ESP_LOGI(TAG, "Address type = %d", param->ble_security.auth_cmpl.addr_type);
        ESP_LOGI(TAG, "Pair status = %s", param->ble_security.auth_cmpl.success ? "Success" : "Fail");
        if(!param->ble_security.auth_cmpl.success) {
            ESP_LOGE(TAG, "Authentication Fail reason = 0x%x", param->ble_security.auth_cmpl.fail_reason);
        } else {
            ESP_LOGI(TAG, "Authentication Success! Link is now secure.");
            sec_conn = true; // Set flag indicating secure connection ready for HID reports
        }
        break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        ESP_LOGD(TAG, "Scan Response Data Set Complete");
        break;
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        ESP_LOGD(TAG, "Scan Parameters Set Complete");
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
         ESP_LOGI(TAG, "Connection Parameters Updated: status = %d, min_int = %d, max_int = %d, conn_int = %d, latency = %d, timeout = %d",
                  param->update_conn_params.status,
                  param->update_conn_params.min_int,
                  param->update_conn_params.max_int,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
        break;
    default:
        ESP_LOGD(TAG, "Unhandled GAP Event: %d", event);
        break;
    }
}

// --- Main Application ---

extern "C" void app_main(void)
{
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);
    esp_log_level_set("spi_master", ESP_LOG_WARN);
    esp_log_level_set("HID_LE_PRF", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_WARN);

    static lv_disp_draw_buf_t disp_buf;
    static lv_disp_drv_t disp_drv;
    esp_err_t ret;

    initialize_icon_map();
    initialize_keycode_map();

    ESP_LOGI(TAG, "Initializing NVS");
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS Initialized");

    ESP_LOGI(TAG, "Initializing LittleFS");
    esp_vfs_littlefs_conf_t fs_conf = {
        .base_path = FS_BASE_PATH,
        .partition_label = FS_PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    ret = esp_vfs_littlefs_register(&fs_conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition '%s'. Check partition table.", fs_conf.partition_label);
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        ESP_LOGE(TAG, "Halting execution.");
        while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
    size_t total = 0, used = 0;
    ret = esp_littlefs_info(fs_conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "LittleFS Partition size: total: %d, used: %d", total, used);
    }

    ESP_LOGI(TAG, "Initializing Bluetooth");
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "Initialize BT controller failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "Enable BT controller failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "Init Bluedroid failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "Enable Bluedroid failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_hidd_profile_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init HID Profile failed: %s", esp_err_to_name(ret));
        return;
    }

    esp_ble_gap_register_callback(gap_event_handler);
    esp_hidd_register_callbacks(hidd_event_callback);

    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_MITM_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

    ESP_LOGI(TAG, "Bluetooth Initialized and HID Profile Setup Started");

    initialize_esp_now();

    ESP_LOGI(TAG, "Initialize I2C bus");
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = EXAMPLE_PIN_NUM_TOUCH_SDA,
        .scl_io_num = EXAMPLE_PIN_NUM_TOUCH_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = 200 * 1000
        }
    };
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_HOST, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_HOST, i2c_conf.mode, 0, 0, 0));

    esp_io_expander_handle_t io_expander = NULL;
    ret = esp_io_expander_new_i2c_tca9554(TOUCH_HOST, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander);
    if (ret == ESP_OK && io_expander) {
        ESP_LOGI(TAG, "IO Expander TCA9554 Initialized");
        esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, IO_EXPANDER_OUTPUT);
        esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
    } else {
         ESP_LOGE(TAG, "Failed to initialize IO Expander: %s", esp_err_to_name(ret));
    }

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Configure Backlight Pin");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT};
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL);
#endif

    ESP_LOGI(TAG, "Initialize SPI bus (QSPI)");
    const spi_bus_config_t buscfg = {
        .data0_io_num = EXAMPLE_PIN_NUM_LCD_DATA0,
        .data1_io_num = EXAMPLE_PIN_NUM_LCD_DATA1,
        .sclk_io_num = EXAMPLE_PIN_NUM_LCD_PCLK,
        .data2_io_num = EXAMPLE_PIN_NUM_LCD_DATA2,
        .data3_io_num = EXAMPLE_PIN_NUM_LCD_DATA3,
        .max_transfer_sz = EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT * sizeof(lv_color_t) * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_CS,
                                                                                example_notify_lvgl_flush_ready,
                                                                                &disp_drv);
    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install SH8601 panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)TOUCH_HOST, &tp_io_config, &tp_io_handle));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = static_cast<gpio_num_t>(EXAMPLE_PIN_NUM_TOUCH_RST),
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_LOGI(TAG, "Initialize touch controller FT5x06");
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp));
    if (!tp) {
         ESP_LOGE(TAG, "Touch controller initialization failed!");
    }

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
#endif

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    lv_color_t *buf1 = static_cast<lv_color_t*>(heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    assert(buf1);
    lv_color_t *buf2 = static_cast<lv_color_t*>(heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    assert(buf2);
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = example_lvgl_flush_cb;
    disp_drv.rounder_cb = example_lvgl_rounder_cb;
    disp_drv.drv_update_cb = example_lvgl_update_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    disp_drv.rotated = LV_DISP_ROT_90;
    disp_drv.sw_rotate = 1;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    assert(disp);

    ESP_LOGI(TAG, "Register touch driver to LVGL");
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = example_lvgl_touch_cb;
    indev_drv.user_data = tp;
    lv_indev_t * indev = lv_indev_drv_register(&indev_drv);
    assert(indev);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"};
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);
    BaseType_t task_created = xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);
    assert(task_created == pdPASS);

    ESP_LOGI(TAG, "Adding screen event handler for layout loading");
    lv_obj_add_event_cb(lv_scr_act(), screen_event_cb, LV_EVENT_ALL, NULL);

    ESP_LOGI(TAG, "Loading initial UI from file: %s", KEYBOARD_LAYOUT_FILE);

    const char* initial_layout_base = strrchr(KEYBOARD_LAYOUT_FILE, '/');
    if (initial_layout_base == NULL) {
        initial_layout_base = KEYBOARD_LAYOUT_FILE;
    } else {
        initial_layout_base++;
    }

    char* initial_filename_event_param = strdup(initial_layout_base);
    if (initial_filename_event_param) {
        lv_event_send(lv_scr_act(), LV_EVENT_LOAD_LAYOUT, (void*)initial_filename_event_param);
    } else {
        ESP_LOGE(TAG, "Failed to allocate memory for initial layout filename!");
        lv_obj_t * label = lv_label_create(lv_scr_act());
        lv_label_set_text(label, "FATAL ERROR:\nMem alloc failed\nfor initial layout!");
        lv_obj_center(label);
    }

    ESP_LOGI(TAG, "Initialization complete. Starting tasks.");
}