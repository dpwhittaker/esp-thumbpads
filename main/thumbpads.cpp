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

#include "lvgl.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_ft5x06.h"

#include "esp_io_expander_tca9554.h"

#include "ble_hid_keyboard.h"
#include "HIDKeyboardTypes.h"
#include "HIDTypes.h"

static const char *TAG = "example";
static SemaphoreHandle_t lvgl_mux = NULL;

#define LCD_HOST SPI2_HOST
#define TOUCH_HOST I2C_NUM_0

// --- Filesystem Configuration ---
#define FS_PARTITION_LABEL "storage"
#define FS_BASE_PATH "/fs"
#define KEYBOARD_LAYOUT_FILE FS_BASE_PATH "/keyboard.cfg"

#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL (16)
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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


#define EXAMPLE_LVGL_BUF_HEIGHT (EXAMPLE_LCD_V_RES / 8) // Keep buffer size reasonable
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

// --- Keyboard Action Data Structures ---
typedef enum {
    ACTION_TYPE_KEYCODE,
    ACTION_TYPE_TOGGLE,
    ACTION_TYPE_FUNCTION
} action_type_t;

typedef struct {
    action_type_t type;
    union {
        uint8_t keycode;        // For KEYCODE and TOGGLE
        char*   function_name;  // For FUNCTION (dynamically allocated)
    } data;
    bool toggle_state;          // Current state for toggle keys
} button_action_t;


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

    // Task lock
    if (!example_lvgl_lock(5000)) { // Wait max 5s for lock
        ESP_LOGE(TAG, "Could not obtain LVGL lock to create keyboard");
        fclose(f);
        return;
    }

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

    static lv_style_t label_style;
    lv_style_init(&label_style);
    lv_style_set_text_color(&label_style, lv_color_black()); // Set text color to blue
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
        lv_label_set_text(label_obj, label_str);
        lv_obj_add_style(label_obj, &label_style, 0);
        lv_obj_center(label_obj);

        // --- Process and Store Action Data ---
        button_action_t* action = static_cast<button_action_t*>(malloc(sizeof(button_action_t)));
        if (!action) {
            ESP_LOGE(TAG, "L%d: Failed to allocate memory for button action!", line_num);
            continue; // Skip this button
        }
        action->toggle_state = false;
        action->data.function_name = NULL;
        action->data.keycode = 0; // Default to no-op

        bool parse_error = false; // <<< DEFINE parse_error HERE
        if (action_data_len > 0) {
            char* end_ptr = NULL;
            errno = 0;

            if (action_data_str[0] == 'T' && action_data_len > 1) {
                action->type = ACTION_TYPE_TOGGLE;
                long val = strtol(action_data_str + 1, &end_ptr, 16);
                if (errno != 0 || end_ptr == (action_data_str + 1) || *end_ptr != '\0' || val < 0 || val > 255) {
                    ESP_LOGE(TAG, "L%d: Invalid hex value for Toggle key '%s': %s", line_num, label_str, action_data_str + 1);
                    parse_error = true;
                } else {
                    action->data.keycode = (uint8_t)val;
                    ESP_LOGD(TAG, "L%d: Btn: '%s' -> TOGGLE Keycode: 0x%02X", line_num, label_str, action->data.keycode);
                }
            } else if (action_data_str[0] == 'F' && action_data_len > 1) {
                action->type = ACTION_TYPE_FUNCTION;
                const char* func_name_src = action_data_str + 1;
                size_t name_len = strlen(func_name_src);
                action->data.function_name = static_cast<char*>(malloc(name_len + 1));
                if (action->data.function_name) {
                    strcpy(action->data.function_name, func_name_src);
                    ESP_LOGD(TAG, "L%d: Btn: '%s' -> FUNCTION Name: %s", line_num, label_str, action->data.function_name);
                } else {
                    ESP_LOGE(TAG, "L%d: Failed to allocate memory for function name '%s'!", line_num, label_str);
                    parse_error = true;
                }
            } else {
                action->type = ACTION_TYPE_KEYCODE;
                long val = strtol(action_data_str, &end_ptr, 16);
                if (errno != 0 || end_ptr == action_data_str || *end_ptr != '\0' || val < 0 || val > 255) {
                    ESP_LOGE(TAG, "L%d: Invalid hex value for Keycode '%s': %s", line_num, label_str, action_data_str);
                    parse_error = true;
                } else {
                    action->data.keycode = (uint8_t)val;
                    ESP_LOGD(TAG, "L%d: Btn: '%s' -> KEYCODE: 0x%02X ('%c')", line_num, label_str, action->data.keycode, isprint(action->data.keycode) ? action->data.keycode : '.');
                }
            }
        } else {
             ESP_LOGW(TAG, "L%d: Button '%s' has no action data.", line_num, label_str);
             action->type = ACTION_TYPE_KEYCODE;
             action->data.keycode = 0;
        }

        if (parse_error) {
            ESP_LOGE(TAG, "L%d: Parse error for button '%s', setting to no-op.", line_num, label_str);
            action->type = ACTION_TYPE_KEYCODE;
            action->data.keycode = 0;
            if (action->data.function_name) {
                free(action->data.function_name);
                action->data.function_name = NULL;
            }
        }

        // Log AFTER attempting to parse action
        if (!parse_error) {
             ESP_LOGI(TAG, "L%d: Button '%s' created successfully.", line_num, label_str);
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

    // Task unlock
    example_lvgl_unlock();

    ESP_LOGI(TAG, "Keyboard layout loaded.");

} // <<< END OF create_keyboard_from_file FUNCTION BLOCK - Ensure this brace matches the function definition


// --- Event Handlers ---

// Event handler for all keyboard buttons
static void keyboard_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e);
    button_action_t* action = (button_action_t*)lv_obj_get_user_data(btn); // Retrieve stored action

    if (!action) return; // Should not happen if setup is correct

    // --- Handle different action types ---
    switch (action->type) {
        case ACTION_TYPE_KEYCODE:
            if (code == LV_EVENT_PRESSED) {
                ESP_LOGI(TAG, "Key Press: 0x%02X ('%c')", action->data.keycode, isprint(action->data.keycode) ? action->data.keycode : '.');
                // ---> SEND KEY PRESS VIA BLE <---
                if (action->data.keycode != 0) { // Don't send press for 'no key' (0x00)
                    BleHidKeyboard::sendKeyPress(action->data.keycode);
                }
            } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
                 ESP_LOGI(TAG, "Key Release: 0x%02X", action->data.keycode);
                 // ---> SEND KEY RELEASE VIA BLE <---
                 // Send release for all keys, including 0x00 if it was pressed (though it shouldn't be)
                 BleHidKeyboard::sendKeyRelease();
            }
            break;

        case ACTION_TYPE_TOGGLE:
            if (code == LV_EVENT_CLICKED) { // Use CLICKED for toggles
                action->toggle_state = !action->toggle_state;
                ESP_LOGI(TAG, "Toggle Key: 0x%02X -> State: %s", action->data.keycode, action->toggle_state ? "ON" : "OFF");

                // Add visual feedback
                if (action->toggle_state) {
                    lv_obj_add_state(btn, LV_STATE_CHECKED);
                } else {
                    lv_obj_clear_state(btn, LV_STATE_CHECKED);
                }

                // ---> SEND TOGGLE STATE VIA BLE <---
                // This requires managing modifier state or sending press/release accordingly
                // For now, let's just send a press when ON and release when OFF
                // WARNING: This simple approach won't work correctly for standard modifiers like Shift/Ctrl.
                // A proper implementation needs a central state manager for modifiers.
                if (action->toggle_state) {
                     BleHidKeyboard::sendKeyPress(action->data.keycode);
                } else {
                     BleHidKeyboard::sendKeyRelease(); // Release all keys, including the toggled one
                }
            }
            break;

        case ACTION_TYPE_FUNCTION:
             if (code == LV_EVENT_CLICKED) {
                 ESP_LOGI(TAG, "Function Call Request: %s", action->data.function_name ? action->data.function_name : "<NULL>");
                 if (action->data.function_name == NULL) break;

                 // Example: Implement Backspace using BLE
                 if (strcmp(action->data.function_name, "Backspace") == 0) {
                     ESP_LOGI(TAG, "Executing 'Backspace'");
                     BleHidKeyboard::sendKeyPress(0x2a); // Send press
                     // Need a slight delay or mechanism to ensure release is sent after press
                     // For simplicity here, rely on the next key press/release to clear it,
                     // OR send release immediately (might be too fast for some OS)
                     // vTaskDelay(pdMS_TO_TICKS(20)); // Small delay - adjust as needed
                     BleHidKeyboard::sendKeyRelease(); // Send release
                 }
                 // ---> TODO: Implement other function calls <---
                 else if (strcmp(action->data.function_name, "Next") == 0) {
                     ESP_LOGI(TAG, "Executing 'Next' function placeholder");
                 } else {
                     ESP_LOGW(TAG, "Unknown function name in keyboard layout: %s", action->data.function_name);
                 }
             }
             break;
    }
}

// Cleanup callback to free allocated user data when the grid is deleted
static void cleanup_keyboard_cb(lv_event_t * e) {
    lv_obj_t * grid = lv_event_get_target(e);
    if (!grid) return;

    uint32_t child_cnt = lv_obj_get_child_cnt(grid);
    ESP_LOGI(TAG, "Cleaning up keyboard user data for %d children", child_cnt);

    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t * child = lv_obj_get_child(grid, i);
        // Check if it's likely a button (has user data) before proceeding
        if (child && lv_obj_check_type(child, &lv_btn_class)) { // Check if it's a button
             button_action_t* action = static_cast<button_action_t*>(lv_obj_get_user_data(child));
             if (action) {
                 ESP_LOGD(TAG, "Freeing action data for child %d", i);
                 if (action->type == ACTION_TYPE_FUNCTION && action->data.function_name) {
                     free(action->data.function_name); // Free allocated function name string
                     action->data.function_name = NULL; // Prevent double free
                 }
                 free(action); // Free the action struct itself
                 lv_obj_set_user_data(child, NULL); // Clear pointer
             }
        }
    }
     ESP_LOGI(TAG, "Keyboard cleanup finished.");
}


// --- Main Application ---

extern "C" void app_main(void)
{
    // Reduce log spam from drivers if desired
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);
    esp_log_level_set("spi_master", ESP_LOG_WARN);
    // esp_log_level_set("gpio", ESP_LOG_WARN);

    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s)
    static lv_disp_drv_t disp_drv;      // contains callback functions

    // --- Initialize LittleFS ---
    ESP_LOGI(TAG, "Initializing LittleFS");
    esp_vfs_littlefs_conf_t fs_conf = {
        .base_path = FS_BASE_PATH,
        .partition_label = FS_PARTITION_LABEL,
        .format_if_mount_failed = true, // Format on first boot or if corrupt
        .dont_mount = false,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&fs_conf);
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

    // --- Initialize BLE HID Keyboard ---
    // IMPORTANT: Call this *after* NVS is initialized (which happens in BleHidKeyboard::init)
    //            and *before* the UI tries to send key presses.
    if (!BleHidKeyboard::init("TpadKB")) { // Use a specific name
        ESP_LOGE(TAG, "Failed to initialize BLE HID Keyboard!");
        // Handle error appropriately - maybe display on screen or halt
        // For now, continue, but BLE won't work.
    } else {
        ESP_LOGI(TAG, "BLE HID Keyboard Initialized Successfully.");
    }
    // --- End BLE HID Keyboard Initialization ---

    // --- I2C Initialization (for Touch and IO Expander) ---
    ESP_LOGI(TAG, "Initialize I2C bus");
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = EXAMPLE_PIN_NUM_TOUCH_SDA,
        .scl_io_num = EXAMPLE_PIN_NUM_TOUCH_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = { // <-- Correctly initialize nested struct
            .clk_speed = 200 * 1000
        }
        // .clk_flags = 0 // Add if needed by your IDF version
    };
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_HOST, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_HOST, i2c_conf.mode, 0, 0, 0));

    // --- IO Expander Initialization ---
    esp_io_expander_handle_t io_expander = NULL;
    // Initialize only once
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(TOUCH_HOST, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander));
    if (io_expander) {
        ESP_LOGI(TAG, "IO Expander TCA9554 Initialized");
        // Configure pins 0, 1, 2 as outputs and toggle them (likely power/reset control)
        esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, IO_EXPANDER_OUTPUT);
        esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, 0); // Set all low
        vTaskDelay(pdMS_TO_TICKS(200)); // Short delay
        esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, 1); // Set all high
        vTaskDelay(pdMS_TO_TICKS(200)); // Short delay
    } else {
         ESP_LOGE(TAG, "Failed to initialize IO Expander");
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
        .max_transfer_sz = EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT * sizeof(lv_color_t) * 2,
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
    // NOTE: Rotation is handled in update_cb based on drv->rotated
    // Set the initial desired rotation here. update_cb will apply it.
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
