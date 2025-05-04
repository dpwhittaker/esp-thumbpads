#ifndef KEYBOARD_LAYOUT_H
#define KEYBOARD_LAYOUT_H

#include <vector>
#include <string>
#include <map>
#include <stdint.h>
#include "lvgl.h"
#include "hid_dev.h" // For key_mask_t, HID keycodes if needed directly
#include "hid_keycodes.h" // Include keycode/icon map declarations
#include "esp_now_common.h" // Include shared ESP-NOW definitions

// --- Keyboard Action Data Structures ---

// Represents a single component within an action sequence
typedef enum {
    ACTION_COMP_TYPE_KEYCODE,       // Single key press/release (uses keycode)
    ACTION_COMP_TYPE_SPECIAL_KEY,   // Special key press/release (uses keycode)
    ACTION_COMP_TYPE_STRING,        // Type a sequence of characters (uses string_data)
    ACTION_COMP_TYPE_MOD_PRESS,     // Press modifier(s) (uses modifier_mask)
    ACTION_COMP_TYPE_MOD_RELEASE,   // Release modifier(s) (uses modifier_mask)
    ACTION_COMP_TYPE_DELAY,         // Explicit delay (uses delay_ms)
    ACTION_COMP_TYPE_KEYS_SIMULT,   // Simultaneous key presses (uses keycode array + count)
} action_component_type_t;

typedef struct {
    action_component_type_t type;
    union {
        uint8_t keycode;            // For KEYCODE, SPECIAL_KEY
        key_mask_t modifier_mask;   // For MOD_PRESS, MOD_RELEASE
        uint32_t delay_ms;          // For DELAY
        struct {                    // For STRING
            char* str;
        } string_data;
        struct {                    // For KEYS_SIMULT
            uint8_t keycodes[6];    // Max 6 simultaneous non-modifier keys
            uint8_t count;
        } simult_keys;
    } data;
} action_component_t;

// Represents a segment of the button label (text or icon)
typedef enum {
    LABEL_PART_TEXT,
    LABEL_PART_ICON
} label_part_type_t;

typedef struct {
    label_part_type_t type;
    char* value; // Either literal text or the LV_SYMBOL_ string
} label_part_t;

// Structure to hold the result of executing an action sequence
typedef struct {
    key_mask_t modifier_delta;      // Modifiers added by the sequence
    std::vector<uint8_t> keys_pressed; // Keycodes pressed by the sequence
} sequence_result_t;

// Main button action structure
typedef enum {
    BUTTON_TYPE_MOMENTARY,
    BUTTON_TYPE_TOGGLE,
    BUTTON_TYPE_NAVIGATION
} button_type_t;

typedef struct {
    button_type_t type;
    char font_size_char; // 'S', 'M', 'L', 'J'
    std::vector<label_part_t> label_parts; // Parsed label
    std::vector<action_component_t> press_sequence;
    std::vector<action_component_t> release_sequence; // Empty if no '|'
    uint32_t release_delay_ms; // Delay *before* release sequence (0 if none specified)
    char* navigation_filename; // Only for BUTTON_TYPE_NAVIGATION
    bool toggle_state;         // Current state for toggle buttons
    key_mask_t toggle_modifier_mask; // Modifiers activated by this toggle's press sequence
    std::vector<uint8_t> keys_pressed_by_this_action; // Keys pressed by this action's press sequence (for implicit release)
    int grid_col, grid_row, grid_col_span, grid_row_span;
} button_action_t;

// Structure to hold grid state during layout
typedef struct {
    int cols;
    int rows;
    bool** occupied; // 2D array to track occupied cells
} grid_layout_state_t;

// --- Extern Declarations for Global Variables ---
// Defined in keyboard_layout.cpp
// special_key_map and modifier_map are defined in hid_keycodes.h/cpp
extern uint32_t current_layout_default_delay;
extern key_mask_t current_modifier_mask;
extern uint8_t current_held_keys[6];
extern uint8_t current_held_keys_count;

// Defined in thumbpads.cpp
extern const char *TAG; // Use the main TAG
extern volatile bool sec_conn;
extern uint16_t hid_conn_id;

// Declared in esp_now_common.h
extern volatile esp_now_pairing_status_t pairing_status;
extern uint8_t peer_mac_address[ESP_NOW_ETH_ALEN];
extern volatile uint8_t remote_modifier_mask;

// --- Function Prototypes ---

// Layout Creation and Management
bool create_keyboard_from_file(const char* filename);

// Event Handlers (Callbacks)
void keyboard_event_cb(lv_event_t * e);
void cleanup_keyboard_cb(lv_event_t * e);
void screen_event_cb(lv_event_t * e); // Handles LV_EVENT_LOAD_LAYOUT

// Helper Functions (if needed externally, otherwise keep static in .cpp)
// const lv_font_t* get_font_for_size(char size_char); // Example if needed

#endif // KEYBOARD_LAYOUT_H
