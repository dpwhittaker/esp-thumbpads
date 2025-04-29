#include <stdio.h>
// Removed duplicate #include <stdio.h>
#include <string.h> // Added for string functions
#include <stdlib.h> // Added for malloc, free
#include <ctype.h>  // Added for isdigit, isprint
#include <errno.h>  // Added for errno

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

static const char *TAG = "example";
static SemaphoreHandle_t lvgl_mux = NULL;

#define LCD_HOST SPI2_HOST
#define TOUCH_HOST I2C_NUM_0

// --- Filesystem Configuration ---
#define FS_PARTITION_LABEL "storage"
#define FS_BASE_PATH "/fs"
#define KEYBOARD_LAYOUT_FILE FS_BASE_PATH "/menu.cfg"

#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL (16)
#endif

#define LV_EVENT_LOAD_LAYOUT ((lv_event_code_t)(_LV_EVENT_LAST + 0)) // Use first available user event

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
#define ESP_NOW_PAIRING_TIMEOUT_MS 5000 // How long to attempt pairing (milliseconds)
#define ESP_NOW_BROADCAST_INTERVAL_MS 500 // Interval between broadcasts
uint8_t pair_message[10] = "ThumbPair"; // Unique identifier for pairing messages
uint8_t pair_ack_message[10] = "ThumbPack"; // Unique identifier for pairing acknowledgment
// Shared data structure for ESP-NOW
typedef struct {
    uint8_t modifier_mask;
} esp_now_modifier_state_t;

typedef enum {
    PAIRING_STATUS_INIT,
    PAIRING_STATUS_WAITING, // Broadcasting and listening
    PAIRING_STATUS_DONE,
    PAIRING_STATUS_FAIL
} esp_now_pairing_status_t;

// Global variables for pairing
static volatile esp_now_pairing_status_t pairing_status = PAIRING_STATUS_INIT;
static uint8_t peer_mac_address[ESP_NOW_ETH_ALEN] = {0}; // Store the discovered peer MAC
static uint8_t my_mac_address[ESP_NOW_ETH_ALEN] = {0};   // Store own MAC
static const uint8_t broadcast_mac_address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // ESP-NOW Broadcast MAC

static volatile uint8_t remote_modifier_mask = 0;

// Callback function when data is sent
static void esp_now_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "ESP-NOW Send failed to " MACSTR ", Status: %d", MAC2STR(mac_addr), status);
    } else {
        ESP_LOGI(TAG, "ESP-NOW Send success to " MACSTR, MAC2STR(mac_addr));
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

    bool is_pair_message = (len == sizeof(pair_message) &&
        memcmp(incoming_data, pair_message, sizeof(pair_message)) == 0);
    bool is_pair_ack_message = (len == sizeof(pair_ack_message) &&
        memcmp(incoming_data, pair_ack_message, sizeof(pair_ack_message)) == 0);
    if (is_pair_message || is_pair_ack_message) {
        ESP_LOGI(TAG, "Received pairing message from " MACSTR, MAC2STR(sender_mac));

        // Check if this peer is already known or if we need to add them
        bool peer_exists = esp_now_is_peer_exist(sender_mac);

        if (!peer_exists) {
            esp_now_peer_info_t peer_info = {};
            memcpy(peer_info.peer_addr, sender_mac, ESP_NOW_ETH_ALEN);
            peer_info.channel = 0; // Use current channel (or specific channel if set)
            peer_info.ifidx = WIFI_IF_STA;
            peer_info.encrypt = false;

            if (esp_now_add_peer(&peer_info) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to add ESP-NOW peer " MACSTR, MAC2STR(sender_mac));
                // Don't change pairing status on failure to add peer
            } else {
                ESP_LOGI(TAG, "Successfully added ESP-NOW peer: " MACSTR, MAC2STR(sender_mac));
                peer_exists = true; // Mark as existing now
            }
        } else {
                ESP_LOGD(TAG, "Received pairing message from existing peer: " MACSTR, MAC2STR(sender_mac));
        }

        // If peer was added successfully OR already existed, update our state and send reply
        if (peer_exists) {
                // Store the peer MAC if not already set or if different (unlikely)
                if (pairing_status != PAIRING_STATUS_DONE || memcmp(peer_mac_address, sender_mac, ESP_NOW_ETH_ALEN) != 0) {
                    memcpy(peer_mac_address, sender_mac, ESP_NOW_ETH_ALEN);
                    ESP_LOGI(TAG, "Pairing partner set to: " MACSTR, MAC2STR(peer_mac_address));
                }
                pairing_status = PAIRING_STATUS_DONE; // Mark as done (or confirm done)

                // --- Send pairing message back to the sender (unicast) ---
                if (is_pair_message) { // don't reply to ACK messages
                    esp_err_t send_result = esp_now_send(sender_mac, pair_ack_message, sizeof(pair_ack_message));
                    if (send_result != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to send pairing reply to " MACSTR ": %s", MAC2STR(sender_mac), esp_err_to_name(send_result));
                    } else {
                        ESP_LOGI(TAG, "Sent pairing reply to " MACSTR, MAC2STR(sender_mac));
                    }
                }
                // --- End send reply ---
        }
        return; // Processed pairing message
    } // --- End Pairing Message Handling ---


    // --- Handle Modifier State Message (Only if paired and from the known peer) ---
    if (pairing_status == PAIRING_STATUS_DONE &&
        len == sizeof(esp_now_modifier_state_t) &&
        memcmp(sender_mac, peer_mac_address, ESP_NOW_ETH_ALEN) == 0) // Check if from known peer
    {
        esp_now_modifier_state_t received_state;
        memcpy(&received_state, incoming_data, sizeof(received_state));
        // Prevent rapid logging if modifier state hasn't actually changed
        if (remote_modifier_mask != received_state.modifier_mask) {
             remote_modifier_mask = received_state.modifier_mask;
             ESP_LOGI(TAG, "ESP-NOW Received modifier mask 0x%02X from peer", remote_modifier_mask); // Changed to INFO
        } else {
             ESP_LOGI(TAG, "ESP-NOW Received unchanged modifier mask 0x%02X from peer", remote_modifier_mask);
        }
    } else if (len == sizeof(esp_now_modifier_state_t) && memcmp(sender_mac, my_mac_address, ESP_NOW_ETH_ALEN) != 0) {
         // Log if we receive a modifier state from someone other than the paired peer (or before pairing)
         ESP_LOGW(TAG, "ESP-NOW Received modifier state data from unexpected MAC " MACSTR, MAC2STR(sender_mac));
    }
     // Ignore other messages
}


static void initialize_esp_now(void) {
    ESP_LOGI(TAG, "Initializing ESP-NOW for pairing");
    pairing_status = PAIRING_STATUS_INIT; // Reset status
    memset(peer_mac_address, 0, ESP_NOW_ETH_ALEN); // Clear any previous peer MAC

    // 1. Initialize Wi-Fi (same as before)
    // Ensure Wi-Fi is initialized only once if other parts of your app use it.
    // If this is the only user, this is fine.
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret == ESP_ERR_NO_MEM || ret == ESP_FAIL) { // Check specific errors if needed
        ESP_ERROR_CHECK(ret); // Halt on critical init failure
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_init returned: %s", esp_err_to_name(ret)); // Log non-critical errors
    }

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    // Optional: Set a specific channel? esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    // Get own MAC address
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, my_mac_address));
    ESP_LOGI(TAG, "My ESP-NOW MAC: " MACSTR, MAC2STR(my_mac_address));

    // 2. Initialize ESP-NOW (same as before)
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(esp_now_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(esp_now_recv_cb));

    // 3. Add Broadcast Peer (Still needed for sending initial broadcasts)
    esp_now_peer_info_t broadcast_peer_info = {};
    memcpy(broadcast_peer_info.peer_addr, broadcast_mac_address, ESP_NOW_ETH_ALEN);
    broadcast_peer_info.channel = 0; // Use current channel
    broadcast_peer_info.ifidx = WIFI_IF_STA;
    broadcast_peer_info.encrypt = false;
    // Add peer if it doesn't exist
    if (!esp_now_is_peer_exist(broadcast_mac_address)) {
        if (esp_now_add_peer(&broadcast_peer_info) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add broadcast peer");
            pairing_status = PAIRING_STATUS_FAIL;
            // Consider if ESP-NOW should be de-initialized here
            return; // Cannot proceed without broadcast peer
        }
    } else {
         ESP_LOGI(TAG, "Broadcast peer already exists.");
    }


    // 4. Start Pairing Broadcast/Listen Loop (Initiation Phase)
    ESP_LOGI(TAG, "Starting ESP-NOW pairing initiation broadcast (Timeout: %d ms)", ESP_NOW_PAIRING_TIMEOUT_MS);
    pairing_status = PAIRING_STATUS_WAITING; // Start in waiting state
    int64_t start_time = esp_timer_get_time();

    // Prepare the simplified pairing message
    while (pairing_status == PAIRING_STATUS_WAITING) { // Loop only while waiting for initial contact
        // Check timeout
        if ((esp_timer_get_time() - start_time) / 1000 > ESP_NOW_PAIRING_TIMEOUT_MS) {
            ESP_LOGW(TAG, "ESP-NOW Pairing initiation timed out (no peer found). Will rely on receiving pairing msg.");
            // Don't set to FAIL, just stop broadcasting. Status remains WAITING or becomes DONE if msg received.
            break; // Exit broadcast loop on timeout
        }

        // Send broadcast message
        esp_err_t send_result = esp_now_send(broadcast_mac_address, (uint8_t *)&pair_message, sizeof(pair_message));
        if (send_result != ESP_OK) {
            ESP_LOGE(TAG, "ESP-NOW pairing broadcast send error: %s", esp_err_to_name(send_result));
        } else {
             ESP_LOGD(TAG, "Sent pairing broadcast");
        }

        // Wait before next broadcast (allow time for receiving)
        uint32_t random_delay = esp_random() % (ESP_NOW_BROADCAST_INTERVAL_MS / 5);
        vTaskDelay(pdMS_TO_TICKS(ESP_NOW_BROADCAST_INTERVAL_MS + random_delay));
    }

    // 5. Cleanup Broadcast Peer is NOT strictly necessary if we might receive broadcasts later,
    // but removing it might save a tiny bit of memory if we are sure we won't broadcast again.
    // Let's keep it for now to allow receiving broadcasts. If memory is tight, consider removing.
    // esp_now_del_peer(broadcast_mac_address);

    // 6. Final Status Check (Reflects if pairing completed during the broadcast window)
    if (pairing_status == PAIRING_STATUS_DONE) {
        ESP_LOGI(TAG, "ESP-NOW Pairing successful during init with peer: " MACSTR, MAC2STR(peer_mac_address));
    } else {
        ESP_LOGI(TAG, "ESP-NOW Initial pairing broadcast finished. Waiting to receive pairing message...");
        // Status remains PAIRING_STATUS_WAITING if timeout occurred without success
    }
}

// --- Keyboard Action Data Structures ---
typedef enum {
    ACTION_TYPE_KEYCODE,
    ACTION_TYPE_TOGGLE,
    ACTION_TYPE_GOTO_LAYOUT
} action_type_t;

typedef struct {
    action_type_t type;
    union {
        uint8_t keycode;        // For KEYCODE and TOGGLE
        char*   layout_filename;  // For FUNCTION (dynamically allocated)
    } data;
    bool toggle_state;          // Current state for toggle keys
} button_action_t;

// --- BLE HID Globals & Defines ---
static uint16_t hid_conn_id = 0;
static bool sec_conn = false; // Is the connection secure (paired and encrypted)?
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
        // ESP_LOGD(TAG, "Touch position: %d,%d", tp_x, tp_y); // Can be noisy
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

static bool example_lvgl_lock(int timeout_ms)
{
    assert(lvgl_mux && "lvgl_mux must be created first");
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;
}

static void example_lvgl_unlock(void)
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
            // Security is automatically requested by the stack after connection
            break;
        }
        case ESP_HIDD_EVENT_BLE_DISCONNECT: {
            sec_conn = false;
            hid_conn_id = 0; // Reset connection ID
            ESP_LOGI(TAG, "HID Disconnected");
            // Restart advertising
            esp_ble_gap_start_advertising(&hidd_adv_params);
            ESP_LOGI(TAG, "Advertising restarted");
            break;
        }
        case ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT: {
            // Not used in standard keyboard, but good to have
            ESP_LOGI(TAG, "Vendor Report Write Event: conn_id=%d, report_id=%d, len=%d",
                     param->vendor_write.conn_id, param->vendor_write.report_id, param->vendor_write.length);
            ESP_LOG_BUFFER_HEX(TAG, param->vendor_write.data, param->vendor_write.length);
            break;
        }
        case ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT: {
            // Host writes to this to set keyboard LEDs (Caps Lock, Num Lock, etc.)
            ESP_LOGI(TAG, "LED Report Write Event: conn_id=%d, report_id=%d, len=%d",
                     param->led_write.conn_id, param->led_write.report_id, param->led_write.length);
            ESP_LOG_BUFFER_HEX(TAG, param->led_write.data, param->led_write.length);
            // TODO: Parse param->led_write.data to update LED status indicators in UI if needed
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
    // Advertising Events
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

    // Security Events
     case ESP_GAP_BLE_SEC_REQ_EVT:
        /* send the positive security response to the peer device to accept the security request.
        If not accept the security request, should send the security response with reject code*/
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

    // Other GAP events (less relevant for basic HID)
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        ESP_LOGD(TAG, "Scan Response Data Set Complete");
        break;
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        ESP_LOGD(TAG, "Scan Parameters Set Complete");
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        // Only relevant if scanning
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

// --- Keyboard Creation from File ---

// Forward declaration of the event handler
static void keyboard_event_cb(lv_event_t * e);
// Forward declaration of the cleanup handler
static void cleanup_keyboard_cb(lv_event_t * e);

// New function to load layout from file
static void create_keyboard_from_file(const char* filename) {
    lv_obj_t *scr = lv_scr_act();
    FILE* f = fopen(filename, "rb"); // Open in binary read mode

    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open keyboard layout file: %s", filename);
        // Optionally: Create a fallback default keyboard here or display error message
        // For now, just return
        return;
    }

    ESP_LOGI(TAG, "Loading keyboard layout from: %s", filename);

    // --- Read Grid Dimensions ---
    int grid_cols = 0, grid_rows = 0;
    if (fscanf(f, "%dx%d\n", &grid_cols, &grid_rows) != 2 || grid_cols <= 0 || grid_rows <= 0) {
        ESP_LOGE(TAG, "Invalid grid dimensions in file: %s", filename);
        fclose(f);
        example_lvgl_unlock();
        return;
    }
    ESP_LOGI(TAG, "Grid: %d x %d", grid_cols, grid_rows);

    // --- Create Grid Layout ---
    static lv_style_t grid_style; // Static to persist
    lv_style_init(&grid_style);
    lv_style_set_pad_row(&grid_style, 2);
    lv_style_set_pad_column(&grid_style, 2);
    lv_style_set_pad_all(&grid_style, 0);
    lv_style_set_border_width(&grid_style, 0);
    lv_style_set_outline_width(&grid_style, 0);
    lv_style_set_bg_opa(&grid_style, LV_OPA_TRANSP); // Make grid background transparent

    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
    lv_obj_center(grid);
    lv_obj_add_style(grid, &grid_style, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);

    // --- Define Grid Template Dynamically ---
    static lv_coord_t col_dsc[11];
    static lv_coord_t row_dsc[11];

    for (int i = 0; i < grid_cols; i++) col_dsc[i] = LV_GRID_FR(1);
    col_dsc[grid_cols] = LV_GRID_TEMPLATE_LAST;
    for (int i = 0; i < grid_rows; i++) row_dsc[i] = LV_GRID_FR(1);
    row_dsc[grid_rows] = LV_GRID_TEMPLATE_LAST;

    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);

    // --- Button Style ---
    static lv_style_t btn_style; // Static to persist
    lv_style_init(&btn_style);
    lv_style_set_bg_color(&btn_style, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_bg_opa(&btn_style, LV_OPA_COVER); // Ensure background is opaque
    // Optionally, add border/outline if desired for better visibility
    // lv_style_set_border_width(&btn_style, 1);
    // lv_style_set_border_color(&btn_style, lv_palette_main(LV_PALETTE_BLUE));
    lv_style_set_radius(&btn_style, 3);
    lv_style_set_pad_all(&btn_style, 0);

    static lv_style_t label_style;
    lv_style_init(&label_style);
    lv_style_set_text_color(&label_style, lv_color_black()); // Set text color to black
    lv_style_set_text_align(&label_style, LV_TEXT_ALIGN_CENTER); // Center
    lv_style_set_text_font(&label_style, &lv_font_montserrat_28_compressed);

    // --- Read Button Definitions ---
    char line_buffer[128];
    int line_num = 0; // Add line counter
    while (fgets(line_buffer, sizeof(line_buffer), f)) { // <<< START OF WHILE LOOP BLOCK
        line_num++; // Increment line counter

        // Basic validation and cleanup newline
        size_t len = strlen(line_buffer);
        if (len > 0 && line_buffer[len - 1] == '\n') line_buffer[--len] = '\0';
        if (len > 0 && line_buffer[len - 1] == '\r') line_buffer[--len] = '\0'; // Handle CRLF
        if (len < 7) continue; // Need at least C S R S L \t A (e.g., 0101Q\t0) -> 7 chars minimum

        char* tab_ptr = strchr(line_buffer, '\t');
        if (tab_ptr == NULL) {
            // Only log warning if it's NOT a comment or empty line
            if (line_buffer[0] != '#' && line_buffer[0] != '\0') {
                 ESP_LOGW(TAG, "L%d: Skipping line, no tab found: %s", line_num, line_buffer);
            } else {
                 ESP_LOGD(TAG, "L%d: Skipping comment/empty line", line_num); // Debug log for comments
            }
            continue; // Skip to next line
        }

        // --- If we reach here, a tab WAS found ---
        ESP_LOGI(TAG, "L%d: Processing line: %s", line_num, line_buffer); // Log the raw line before splitting

        // Parse grid info (simple char to int)
        if (!isdigit((unsigned char)line_buffer[0]) || !isdigit((unsigned char)line_buffer[1]) || !isdigit((unsigned char)line_buffer[2]) || !isdigit((unsigned char)line_buffer[3])) {
             ESP_LOGW(TAG, "L%d: Skipping line, invalid grid format: %s", line_num, line_buffer);
             continue; // Skip to next line
        }
        int col = (unsigned char)line_buffer[0] - '0';
        int col_span = (unsigned char)line_buffer[1] - '0';
        int row = (unsigned char)line_buffer[2] - '0';
        int row_span = (unsigned char)line_buffer[3] - '0';

        if (col_span < 1) col_span = 1;
        if (row_span < 1) row_span = 1;

        // Extract Label
        *tab_ptr = '\0'; // Temporarily terminate label string at tab position
        char* label_str = &line_buffer[4];

        // Extract Action String
        char* action_data_str = tab_ptr + 1; // String after the tab
        size_t action_data_len = strlen(action_data_str);

        // Log extracted data BEFORE creating button
        ESP_LOGI(TAG, "L%d: Parsed: Col=%d, Row=%d, Label='%s', Action='%s'",
                 line_num, col, row, label_str, action_data_str);

        // --- Create Button and Label ---
        lv_obj_t *btn = lv_btn_create(grid);
        lv_obj_add_style(btn, &btn_style, 0);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, col, col_span, LV_GRID_ALIGN_STRETCH, row, row_span);

        lv_obj_t *label_obj = lv_label_create(btn);
        lv_label_set_long_mode(label_obj, LV_LABEL_LONG_WRAP);
        lv_label_set_text(label_obj, label_str);
        lv_obj_set_width(label_obj, LV_PCT(100));
        lv_obj_add_style(label_obj, &label_style, 0);
        lv_obj_center(label_obj);

        // --- Process and Store Action Data ---
        button_action_t* action = static_cast<button_action_t*>(malloc(sizeof(button_action_t)));
        if (!action) {
            ESP_LOGE(TAG, "L%d: Failed to allocate memory for button action!", line_num);
            // Clean up button/label? Maybe not necessary if grid cleanup handles it.
            continue; // Skip this button
        }
        action->toggle_state = false;
        // Initialize union members to prevent garbage data issues
        action->data.layout_filename = NULL;
        action->data.keycode = 0; // Default to no-op

        bool parse_error = false;
        if (action_data_len > 0) {
            char* end_ptr = NULL;
            errno = 0;

            if (action_data_str[0] == 'T' && action_data_len > 1) {
                // --- TOGGLE Logic (remains the same) ---
                action->type = ACTION_TYPE_TOGGLE;
                long val = strtol(action_data_str + 1, &end_ptr, 16);
                if (errno != 0 || end_ptr == (action_data_str + 1) || *end_ptr != '\0' || val < 0 || val > 255) {
                    ESP_LOGE(TAG, "L%d: Invalid hex value for Toggle key '%s': %s", line_num, label_str, action_data_str + 1);
                    parse_error = true;
                } else {
                    action->data.keycode = (uint8_t)val;
                    ESP_LOGD(TAG, "L%d: Btn: '%s' -> TOGGLE Keycode: 0x%02X", line_num, label_str, action->data.keycode);
                }
            // --- NEW: GOTO_LAYOUT Logic ---
            } else if (action_data_str[0] == 'G' && action_data_len > 1) {
                action->type = ACTION_TYPE_GOTO_LAYOUT;
                const char* filename_src = action_data_str + 1;
                size_t name_len = strlen(filename_src);
                // Allocate memory for the filename (+1 for null terminator)
                action->data.layout_filename = static_cast<char*>(malloc(name_len + 1));
                if (action->data.layout_filename) {
                    strcpy(action->data.layout_filename, filename_src);
                    ESP_LOGD(TAG, "L%d: Btn: '%s' -> GOTO_LAYOUT File: %s", line_num, label_str, action->data.layout_filename);
                } else {
                    ESP_LOGE(TAG, "L%d: Failed to allocate memory for layout filename '%s'!", line_num, label_str);
                    parse_error = true;
                }
            // --- KEYCODE Logic (default case) ---
            } else {
                action->type = ACTION_TYPE_KEYCODE;
                long val = strtol(action_data_str, &end_ptr, 16);
                // Allow '0' or '00' as valid keycode (no-op)
                if (errno != 0 || end_ptr == action_data_str || *end_ptr != '\0' || val < 0 || val > 255) {
                     // Check if it's just '0' or '00' which is valid for no-op
                     if (!( (strcmp(action_data_str, "0") == 0 || strcmp(action_data_str, "00") == 0) && end_ptr != action_data_str && *end_ptr == '\0') ) {
                        ESP_LOGE(TAG, "L%d: Invalid hex value for Keycode '%s': %s", line_num, label_str, action_data_str);
                        parse_error = true;
                     } else {
                         val = 0; // Ensure it's treated as 0
                     }
                }
                // Only assign if no error occurred or if it was a valid '0'/'00'
                if (!parse_error) {
                    action->data.keycode = (uint8_t)val;
                    ESP_LOGD(TAG, "L%d: Btn: '%s' -> KEYCODE: 0x%02X ('%c')", line_num, label_str, action->data.keycode, isprint(action->data.keycode) ? action->data.keycode : '.');
                }
            }
        } else {
             ESP_LOGW(TAG, "L%d: Button '%s' has no action data. Setting to no-op.", line_num, label_str);
             action->type = ACTION_TYPE_KEYCODE;
             action->data.keycode = 0;
        }

        if (parse_error) {
            ESP_LOGE(TAG, "L%d: Parse error for button '%s', setting to no-op.", line_num, label_str);
            action->type = ACTION_TYPE_KEYCODE;
            action->data.keycode = 0;
            // Free filename memory if allocated during a failed GOTO parse
            if (action->data.layout_filename) {
                free(action->data.layout_filename);
                action->data.layout_filename = NULL;
            }
        }

        // Log AFTER attempting to parse action
        if (!parse_error) {
             ESP_LOGD(TAG, "L%d: Button '%s' created successfully.", line_num, label_str); // Changed level to DEBUG
        } else {
             // Error already logged during parsing attempt
        }

        // Store the action struct pointer in the button's user data
        lv_obj_set_user_data(btn, action);

        // Add event callback to the button
        lv_obj_add_event_cb(btn, keyboard_event_cb, LV_EVENT_ALL, NULL);

    }

    ESP_LOGI(TAG, "Finished processing %d lines from file.", line_num);

    fclose(f);

    // Add the cleanup callback to the grid itself
    lv_obj_add_event_cb(grid, cleanup_keyboard_cb, LV_EVENT_DELETE, NULL);

    ESP_LOGI(TAG, "Keyboard layout loaded.");

}

// --- Event Handlers ---

// TODO: Implement proper modifier key state management
static key_mask_t current_modifier_mask = 0;

// Event handler for all keyboard buttons
static void keyboard_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e);
    button_action_t* action = (button_action_t*)lv_obj_get_user_data(btn); // Retrieve stored action

    if (!action) return; // Should not happen if setup is correct

    // --- Handle GOTO_LAYOUT separately first ---
    if (action->type == ACTION_TYPE_GOTO_LAYOUT) {
        if (code == LV_EVENT_CLICKED) {
            ESP_LOGI(TAG, "Layout Change Request: %s", action->data.layout_filename ? action->data.layout_filename : "<NULL>");
            if (action->data.layout_filename != NULL && strlen(action->data.layout_filename) > 0) {
                char* filename_copy = strdup(action->data.layout_filename);
                if (filename_copy) {
                    lv_event_send(lv_scr_act(), LV_EVENT_LOAD_LAYOUT, (void*)filename_copy);
                } else {
                    ESP_LOGE(TAG, "Failed to allocate memory for filename copy!");
                }
            } else {
                 ESP_LOGE(TAG, "GOTO_LAYOUT action has NULL or empty filename!");
            }
        }
        return; // Don't process BLE events for layout change buttons
    }

    // --- Handle Modifier Key Toggles (Update Mask Only) ---
    if (action->type == ACTION_TYPE_TOGGLE) {
        if (code == LV_EVENT_CLICKED) {
            key_mask_t mask_bit = action->data.keycode;

            // Toggle the state
            action->toggle_state = !action->toggle_state;

            // Update the global modifier mask
            if (action->toggle_state) { // Modifier is now ON
                current_modifier_mask |= mask_bit;
                lv_obj_add_state(btn, LV_STATE_CHECKED);
            } else { // Modifier is now OFF
                current_modifier_mask &= ~mask_bit;
                lv_obj_clear_state(btn, LV_STATE_CHECKED);
            }

            ESP_LOGI(TAG, "Modifier Toggle: Key 0x%02X -> State: %s -> Mask: 0x%02X",
                     action->data.keycode, action->toggle_state ? "ON" : "OFF", current_modifier_mask);

            // --- Send modifier state update via ESP-NOW (only if paired) ---
            if (pairing_status == PAIRING_STATUS_DONE) {
                esp_now_modifier_state_t current_state;
                current_state.modifier_mask = current_modifier_mask;
                esp_err_t send_result = esp_now_send(peer_mac_address, (uint8_t *)&current_state, sizeof(current_state));
                if (send_result != ESP_OK) {
                    ESP_LOGE(TAG, "ESP-NOW send error to peer: %s", esp_err_to_name(send_result));
                    // Optional: Handle repeated send failures? Maybe mark peer as lost?
                }
            } else {
                ESP_LOGD(TAG, "Not paired via ESP-NOW, skipping modifier send.");
            }
            // DO NOT send a report here. The mask is sent with the next regular key.
        }
        return; // Modifier toggles don't send reports directly
    }


    // --- Handle Regular Key Presses/Releases (Send Report with Current Mask) ---
    if (action->type == ACTION_TYPE_KEYCODE) {
        // Only send reports if connected and security is established
        if (!sec_conn) {
            if (code == LV_EVENT_PRESSED) { // Log only on initial press attempt
                ESP_LOGW(TAG, "Keycode 0x%02X action ignored: BLE not securely connected.", action->data.keycode);
            }
            return;
        }

        // --- Send Report ---
        uint8_t combined_mask = current_modifier_mask | remote_modifier_mask;
        if (code == LV_EVENT_PRESSED) {
            ESP_LOGI(TAG, "Key Press: 0x%02X with Modifiers: 0x%02X c: 0x%02X | r: 0x%02X", action->data.keycode, combined_mask, current_modifier_mask, remote_modifier_mask);
            if (action->data.keycode != 0) { // Don't send press for 'no key' (0x00)
                uint8_t keycode_array[1] = { action->data.keycode };
                esp_hidd_send_keyboard_value(hid_conn_id, combined_mask, keycode_array, 1);
            } else {
                // If keycode is 0 (no-op), still send a report with just the current modifiers
                // This might be needed if a modifier was toggled but no other key was pressed yet.
                // However, standard HID usually expects a key press/release cycle.
                // Let's stick to the standard: only send non-zero key presses.
                // If you need to send modifier changes without key presses, that's less standard.
            }
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            ESP_LOGD(TAG, "Key Release: 0x%02X with Modifiers: 0x%02X", action->data.keycode, combined_mask);
            // Send empty report (key released) but include the current modifier state
            esp_hidd_send_keyboard_value(hid_conn_id, combined_mask, NULL, 0);
        }
    }
}
// Cleanup callback to free allocated user data when the grid is deleted
static void cleanup_keyboard_cb(lv_event_t * e) {
    lv_obj_t * grid = lv_event_get_target(e);
    if (!grid) return;

    uint32_t child_cnt = lv_obj_get_child_cnt(grid);
    ESP_LOGI(TAG, "Cleaning up keyboard user data for %lu children", child_cnt);

    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t * child = lv_obj_get_child(grid, i);
        // Check if it's likely a button (has user data) before proceeding
        if (child && lv_obj_check_type(child, &lv_btn_class)) { // Check if it's a button
             button_action_t* action = static_cast<button_action_t*>(lv_obj_get_user_data(child));
             if (action) {
                 ESP_LOGD(TAG, "Freeing action data for child %lu (type %d)", i, action->type);
                 // Free layout filename if allocated
                 if (action->type == ACTION_TYPE_GOTO_LAYOUT && action->data.layout_filename) {
                     free(action->data.layout_filename);
                     action->data.layout_filename = NULL; // Prevent double free
                 }
                 free(action); // Free the action struct itself
                 lv_obj_set_user_data(child, NULL); // Clear pointer
             }
        }
    }
     ESP_LOGI(TAG, "Keyboard cleanup finished.");
}

// --- New Screen Event Handler ---
static void screen_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * scr = lv_event_get_target(e);

    if (code == LV_EVENT_LOAD_LAYOUT) {
        char* filename_to_load = (char*)lv_event_get_param(e);
        if (filename_to_load) {
            ESP_LOGI(TAG, "Screen Event: Received request to load layout '%s'", filename_to_load);

            // 1. Clean the current screen (triggers cleanup_keyboard_cb)
            lv_obj_clean(scr);

            // 2. Construct full path
            char full_path[128]; // Adjust size as needed
            snprintf(full_path, sizeof(full_path), "%s/%s", FS_BASE_PATH, filename_to_load);

            // 3. Load the new layout
            ESP_LOGI(TAG, "Loading new layout from: %s", full_path);
            create_keyboard_from_file(full_path); // This function handles its own file opening errors

            // 4. Free the filename string passed via the event
            free(filename_to_load);
        } else {
            ESP_LOGE(TAG, "Screen Event: Received LV_EVENT_LOAD_LAYOUT with NULL filename!");
        }
    }
    // Add other screen events here if needed
}

// --- Main Application ---

extern "C" void app_main(void)
{
    // Reduce log spam from drivers if desired
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);
    esp_log_level_set("spi_master", ESP_LOG_WARN);
    esp_log_level_set("HID_LE_PRF", ESP_LOG_INFO); // Set log level for the HID profile code
    // esp_log_level_set("gpio", ESP_LOG_WARN);

    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s)
    static lv_disp_drv_t disp_drv;      // contains callback functions
    esp_err_t ret; // Use for error checking

    // --- Initialize NVS ---
    // NVS is required for BLE bonding/pairing information storage.
    ESP_LOGI(TAG, "Initializing NVS");
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS Initialized");

    // --- Initialize LittleFS ---
    ESP_LOGI(TAG, "Initializing LittleFS");
    esp_vfs_littlefs_conf_t fs_conf = {
        .base_path = FS_BASE_PATH,
        .partition_label = FS_PARTITION_LABEL,
        .format_if_mount_failed = true, // Format on first boot or if corrupt
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
        while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); } // Halt
    }
    size_t total = 0, used = 0;
    ret = esp_littlefs_info(fs_conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "LittleFS Partition size: total: %d, used: %d", total, used);
    }
    // --- Filesystem Initialized ---

    // --- Start: Initialize BLE Stack and HID Profile ---
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

    ret = esp_hidd_profile_init(); // Initialize HID Device Profile
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init HID Profile failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register callbacks for GAP and HID events
    esp_ble_gap_register_callback(gap_event_handler);
    esp_hidd_register_callbacks(hidd_event_callback);

    /* Set the security parameters for pairing/bonding */
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_MITM_BOND; // Secure Connection + MITM + Bonding
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE; // No Input No Output capability
    uint8_t key_size = 16;      // the key size should be 7~16 bytes
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK; // Distribute encryption and identity keys
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;  // Accept encryption and identity keys
    //uint32_t passkey = 123456; // Example passkey if using IO_CAP_OUT or IO_CAP_IO

    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    /* If your BLE device act as a Slave, the init_key means you hope which types of key of the master should distribute to you,
    and the response key means which key you can distribute to the Master;
    If your BLE device act as a master, the response key means you hope which types of key of the slave should distribute to you,
    and the init key means which key you can distribute to the slave. */
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

    // Passkey setting (only needed if IO Cap requires it)
    // uint32_t passkey = 123456;
    // esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey, sizeof(uint32_t));

    ESP_LOGI(TAG, "Bluetooth Initialized and HID Profile Setup Started");
    // Advertising will start automatically via the GAP event handler after registration finishes

    // --- Initialize ESP-Now ---
    initialize_esp_now();

    // --- I2C Initialization (for Touch and IO Expander) ---
    ESP_LOGI(TAG, "Initialize I2C bus");
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = EXAMPLE_PIN_NUM_TOUCH_SDA,
        .scl_io_num = EXAMPLE_PIN_NUM_TOUCH_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = 200 * 1000 // Reduced speed slightly
        }
        // .clk_flags = 0 // Add if needed by your IDF version
    };
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_HOST, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_HOST, i2c_conf.mode, 0, 0, 0));

    // --- IO Expander Initialization ---
    esp_io_expander_handle_t io_expander = NULL;
    // Initialize only once
    ret = esp_io_expander_new_i2c_tca9554(TOUCH_HOST, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander);
    if (ret == ESP_OK && io_expander) {
        ESP_LOGI(TAG, "IO Expander TCA9554 Initialized");
        // Configure pins 0, 1, 2 as outputs and toggle them (likely power/reset control)
        esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, IO_EXPANDER_OUTPUT);
        esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, 0); // Set all low
        vTaskDelay(pdMS_TO_TICKS(200)); // Short delay
        esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, 1); // Set all high
        vTaskDelay(pdMS_TO_TICKS(200)); // Short delay
    } else {
         ESP_LOGE(TAG, "Failed to initialize IO Expander: %s", esp_err_to_name(ret));
    }


#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0 // Backlight GPIO setup (if used)
    ESP_LOGI(TAG, "Configure Backlight Pin");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT};
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL); // Start with backlight off
#endif

    // --- SPI (QSPI) Initialization for LCD ---
    ESP_LOGI(TAG, "Initialize SPI bus (QSPI)");
    const spi_bus_config_t buscfg = {
        // Ensure this order matches spi_bus_config_t definition!
        .data0_io_num = EXAMPLE_PIN_NUM_LCD_DATA0,
        .data1_io_num = EXAMPLE_PIN_NUM_LCD_DATA1,
        .sclk_io_num = EXAMPLE_PIN_NUM_LCD_PCLK,
        .data2_io_num = EXAMPLE_PIN_NUM_LCD_DATA2,
        .data3_io_num = EXAMPLE_PIN_NUM_LCD_DATA3,
        .max_transfer_sz = EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT * sizeof(lv_color_t) * 2, // Increased buffer size slightly
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // --- LCD Panel IO and Driver Installation ---
    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_CS,
                                                                                example_notify_lvgl_flush_ready,
                                                                                &disp_drv); // Pass disp_drv context
    // Vendor config for SH8601
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
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, // Adjust if color order is different
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true)); // Turn display on

    // --- Touch Panel Initialization (Always Enabled) ---
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)TOUCH_HOST, &tp_io_config, &tp_io_handle));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = static_cast<gpio_num_t>(EXAMPLE_PIN_NUM_TOUCH_RST),
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT,
        .levels = {
            .reset = 0,       // Reset level low
            .interrupt = 0,   // Interrupt level low
        },
        .flags = { // Adjust flags based on touch orientation relative to display
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_LOGI(TAG, "Initialize touch controller FT5x06");
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp)); // Assign to global tp handle
    if (!tp) {
         ESP_LOGE(TAG, "Touch controller initialization failed!");
         // Consider halting or other error handling if touch is critical
    }
    // --- End Touch Panel Initialization ---


    // --- Turn on Backlight (if used) ---
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
#endif

    // --- LVGL Initialization ---
    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    // Allocate LVGL draw buffers in DMA capable memory
    lv_color_t *buf1 = static_cast<lv_color_t*>(heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    assert(buf1);
    lv_color_t *buf2 = static_cast<lv_color_t*>(heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    assert(buf2);
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT);

    // --- Register LVGL Display Driver ---
    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = example_lvgl_flush_cb;
    disp_drv.rounder_cb = example_lvgl_rounder_cb; // Add rounder callback
    disp_drv.drv_update_cb = example_lvgl_update_cb; // Add update callback for rotation
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    // Set initial rotation and enable software rotation
    disp_drv.rotated = LV_DISP_ROT_90; // Example: Start rotated 90 degrees
    disp_drv.sw_rotate = 1; // Enable LVGL software rotation
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    assert(disp);

    // --- Register LVGL Input Driver (Touch - Always Enabled) ---
    ESP_LOGI(TAG, "Register touch driver to LVGL");
    static lv_indev_drv_t indev_drv; // Input device driver (needs to be static or global)
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = example_lvgl_touch_cb;
    indev_drv.user_data = tp; // Pass touch handle to callback
    lv_indev_t * indev = lv_indev_drv_register(&indev_drv); // Get handle if needed later
    assert(indev);
    // --- End Register LVGL Input Driver ---


    // --- LVGL Tick Timer and Task ---
    ESP_LOGI(TAG, "Install LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"};
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    // Create LVGL task mutex and task
    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);
    BaseType_t task_created = xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);
    assert(task_created == pdPASS);

    ESP_LOGI(TAG, "Adding screen event handler");
    lv_obj_add_event_cb(lv_scr_act(), screen_event_cb, LV_EVENT_ALL, NULL); // Add handler to screen

    // --- Create UI ---
    ESP_LOGI(TAG, "Create UI from file");
    // Ensure the layout file exists before calling
    FILE *test_f = fopen(KEYBOARD_LAYOUT_FILE, "r");
    if (test_f) {
        fclose(test_f);
        create_keyboard_from_file(KEYBOARD_LAYOUT_FILE);
    } else {
        ESP_LOGE(TAG, "Keyboard layout file '%s' not found!", KEYBOARD_LAYOUT_FILE);
        ESP_LOGE(TAG, "Please ensure the file exists on the LittleFS partition.");
        // Optionally display an error message on screen
        if (example_lvgl_lock(-1)) {
            lv_obj_t * label = lv_label_create(lv_scr_act());
            lv_label_set_text_fmt(label, "Error:\n%s\nnot found!", KEYBOARD_LAYOUT_FILE);
            lv_obj_center(label);
            example_lvgl_unlock();
        }
    }

    // --- Main loop (implicit via FreeRTOS scheduler) ---
    ESP_LOGI(TAG, "Initialization complete. Starting tasks.");
    // No infinite loop needed here in app_main with FreeRTOS
}