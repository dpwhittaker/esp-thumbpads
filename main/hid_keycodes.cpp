#include "hid_keycodes.h"
#include "hid_dev.h" // For HID_KEY_... definitions and key_mask_t
#include <esp_log.h>
// lvgl.h is included via hid_keycodes.h for LV_SYMBOL_*

#define TAG "hid_keycodes" // Define a tag for logging

// Define the global maps declared in the header
std::map<std::string, uint8_t> special_key_map;
std::map<std::string, key_mask_t> modifier_map;
std::map<std::string, const char*> icon_map; // Define icon_map

// Function to initialize the keycode maps
void initialize_keycode_map() {
    // --- Modifier Map ---
    modifier_map["LC"] = LEFT_CONTROL_KEY_MASK;
    modifier_map["LS"] = LEFT_SHIFT_KEY_MASK;
    modifier_map["LA"] = LEFT_ALT_KEY_MASK;
    modifier_map["LG"] = LEFT_GUI_KEY_MASK;
    modifier_map["LM"] = LEFT_GUI_KEY_MASK; // Alias
    modifier_map["RC"] = RIGHT_CONTROL_KEY_MASK;
    modifier_map["RS"] = RIGHT_SHIFT_KEY_MASK;
    modifier_map["RA"] = RIGHT_ALT_KEY_MASK;
    modifier_map["RG"] = RIGHT_GUI_KEY_MASK;
    modifier_map["RM"] = RIGHT_GUI_KEY_MASK; // Alias

    // --- Special Key Map ---
    special_key_map["ESC"] = HID_KEY_ESCAPE;
    special_key_map["F1"] = HID_KEY_F1;
    special_key_map["F2"] = HID_KEY_F2;
    special_key_map["F3"] = HID_KEY_F3;
    special_key_map["F4"] = HID_KEY_F4;
    special_key_map["F5"] = HID_KEY_F5;
    special_key_map["F6"] = HID_KEY_F6;
    special_key_map["F7"] = HID_KEY_F7;
    special_key_map["F8"] = HID_KEY_F8;
    special_key_map["F9"] = HID_KEY_F9;
    special_key_map["F10"] = HID_KEY_F10;
    special_key_map["F11"] = HID_KEY_F11;
    special_key_map["F12"] = HID_KEY_F12;
    special_key_map["PRTSC"] = HID_KEY_PRNT_SCREEN; // Corrected name
    special_key_map["SCROLL"] = HID_KEY_SCROLL_LOCK;
    special_key_map["PAUSE"] = HID_KEY_PAUSE;
    special_key_map["INS"] = HID_KEY_INSERT;
    special_key_map["HOME"] = HID_KEY_HOME;
    special_key_map["PGUP"] = HID_KEY_PAGE_UP;
    special_key_map["DEL"] = HID_KEY_DELETE_FWD;
    special_key_map["END"] = HID_KEY_END;
    special_key_map["PGDN"] = HID_KEY_PAGE_DOWN;
    special_key_map["RIGHT"] = HID_KEY_RIGHT_ARROW; // Corrected name
    special_key_map["LEFT"] = HID_KEY_LEFT_ARROW;   // Corrected name
    special_key_map["DOWN"] = HID_KEY_DOWN_ARROW;   // Corrected name
    special_key_map["UP"] = HID_KEY_UP_ARROW;     // Corrected name
    special_key_map["NUMLK"] = HID_KEY_NUM_LOCK;
    special_key_map["KP/"] = HID_KEY_DIVIDE;       // Corrected name
    special_key_map["KP*"] = HID_KEY_MULTIPLY;     // Corrected name
    special_key_map["KP-"] = HID_KEY_SUBTRACT;     // Corrected name
    special_key_map["KP+"] = HID_KEY_ADD;          // Corrected name
    special_key_map["KPENT"] = HID_KEY_ENTER;      // Use standard Enter for Keypad Enter? Check HID spec if specific KP Enter needed. Assuming standard for now.
    special_key_map["KP1"] = HID_KEYPAD_1;         // Corrected name
    special_key_map["KP2"] = HID_KEYPAD_2;         // Corrected name
    special_key_map["KP3"] = HID_KEYPAD_3;         // Corrected name
    special_key_map["KP4"] = HID_KEYPAD_4;         // Corrected name
    special_key_map["KP5"] = HID_KEYPAD_5;         // Corrected name
    special_key_map["KP6"] = HID_KEYPAD_6;         // Corrected name
    special_key_map["KP7"] = HID_KEYPAD_7;         // Corrected name
    special_key_map["KP8"] = HID_KEYPAD_8;         // Corrected name
    special_key_map["KP9"] = HID_KEYPAD_9;         // Corrected name
    special_key_map["KP0"] = HID_KEYPAD_0;         // Corrected name
    special_key_map["KP."] = HID_KEYPAD_DOT;       // Corrected name
    special_key_map["MUTE"] = HID_KEY_MUTE;
    special_key_map["VOLUP"] = HID_KEY_VOLUME_UP;
    special_key_map["VOLDN"] = HID_KEY_VOLUME_DOWN;
    special_key_map["ENTER"] = HID_KEY_ENTER;
    special_key_map["TAB"] = HID_KEY_TAB;
    special_key_map["SPACE"] = HID_KEY_SPACEBAR;
    special_key_map["BKSP"] = HID_KEY_DELETE; // Corrected name
    special_key_map["CAPS"] = HID_KEY_CAPS_LOCK;
    special_key_map["LCTRL"] = HID_KEY_LEFT_CTRL;   // As a key itself
    special_key_map["LSHIFT"] = HID_KEY_LEFT_SHIFT; // As a key itself
    special_key_map["LALT"] = HID_KEY_LEFT_ALT;     // As a key itself
    special_key_map["LGUI"] = HID_KEY_LEFT_GUI;     // As a key itself
    special_key_map["RCTRL"] = HID_KEY_RIGHT_CTRL;  // As a key itself
    special_key_map["RSHIFT"] = HID_KEY_RIGHT_SHIFT;// As a key itself
    special_key_map["RALT"] = HID_KEY_RIGHT_ALT;    // As a key itself
    special_key_map["RGUI"] = HID_KEY_RIGHT_GUI;    // As a key itself

    ESP_LOGI(TAG, "Keycode map initialized. Special keys: %zu, Modifiers: %zu", special_key_map.size(), modifier_map.size());
    
    // Add mappings for printable ASCII to their base keycodes if needed for string parsing,
    // although the HID functions might handle this implicitly.
    // Example:
    // special_key_map["a"] = HID_KEY_A; // Lowercase maps to base key
    // special_key_map["A"] = HID_KEY_A; // Uppercase also maps to base key (shift handled separately)
    // ... etc for all letters, numbers, symbols ...
    // This is often handled by a lookup table or function rather than a huge map.
    // For now, we rely on the ActionString parser to handle simple characters.
}

// Function to initialize the icon map
void initialize_icon_map() {
    // Populate the map based on Appendix B (ensure LV_USE_SYMBOLS is enabled)
    icon_map["$BULLET"] = LV_SYMBOL_BULLET;
    icon_map["$AUDIO"] = LV_SYMBOL_AUDIO;
    icon_map["$VIDEO"] = LV_SYMBOL_VIDEO;
    icon_map["$LIST"] = LV_SYMBOL_LIST;
    icon_map["$OK"] = LV_SYMBOL_OK;
    icon_map["$CLOSE"] = LV_SYMBOL_CLOSE;
    icon_map["$POWER"] = LV_SYMBOL_POWER;
    icon_map["$SETTINGS"] = LV_SYMBOL_SETTINGS;
    icon_map["$TRASH"] = LV_SYMBOL_TRASH;
    icon_map["$HOME"] = LV_SYMBOL_HOME;
    icon_map["$DOWNLOAD"] = LV_SYMBOL_DOWNLOAD;
    icon_map["$DRIVE"] = LV_SYMBOL_DRIVE;
    icon_map["$REFRESH"] = LV_SYMBOL_REFRESH;
    icon_map["$MUTE"] = LV_SYMBOL_MUTE;
    icon_map["$VOLUME_MID"] = LV_SYMBOL_VOLUME_MID;
    icon_map["$VOLUME_MAX"] = LV_SYMBOL_VOLUME_MAX;
    icon_map["$IMAGE"] = LV_SYMBOL_IMAGE;
    icon_map["$EDIT"] = LV_SYMBOL_EDIT;
    icon_map["$PREV"] = LV_SYMBOL_PREV;
    icon_map["$PLAY"] = LV_SYMBOL_PLAY;
    icon_map["$PAUSE"] = LV_SYMBOL_PAUSE;
    icon_map["$STOP"] = LV_SYMBOL_STOP;
    icon_map["$NEXT"] = LV_SYMBOL_NEXT;
    icon_map["$EJECT"] = LV_SYMBOL_EJECT;
    icon_map["$LEFT"] = LV_SYMBOL_LEFT;
    icon_map["$RIGHT"] = LV_SYMBOL_RIGHT;
    icon_map["$PLUS"] = LV_SYMBOL_PLUS;
    icon_map["$MINUS"] = LV_SYMBOL_MINUS;
    icon_map["$EYE_OPEN"] = LV_SYMBOL_EYE_OPEN;
    icon_map["$EYE_CLOSE"] = LV_SYMBOL_EYE_CLOSE;
    icon_map["$WARNING"] = LV_SYMBOL_WARNING;
    icon_map["$SHUFFLE"] = LV_SYMBOL_SHUFFLE;
    icon_map["$UP"] = LV_SYMBOL_UP;
    icon_map["$DOWN"] = LV_SYMBOL_DOWN;
    icon_map["$LOOP"] = LV_SYMBOL_LOOP;
    icon_map["$DIRECTORY"] = LV_SYMBOL_DIRECTORY;
    icon_map["$UPLOAD"] = LV_SYMBOL_UPLOAD;
    icon_map["$CALL"] = LV_SYMBOL_CALL;
    icon_map["$CUT"] = LV_SYMBOL_CUT;
    icon_map["$COPY"] = LV_SYMBOL_COPY;
    icon_map["$SAVE"] = LV_SYMBOL_SAVE;
    icon_map["$CHARGE"] = LV_SYMBOL_CHARGE;
    icon_map["$PASTE"] = LV_SYMBOL_PASTE;
    icon_map["$BELL"] = LV_SYMBOL_BELL;
    icon_map["$KEYBOARD"] = LV_SYMBOL_KEYBOARD;
    icon_map["$GPS"] = LV_SYMBOL_GPS;
    icon_map["$FILE"] = LV_SYMBOL_FILE;
    icon_map["$WIFI"] = LV_SYMBOL_WIFI;
    icon_map["$BATTERY_FULL"] = LV_SYMBOL_BATTERY_FULL;
    icon_map["$BATTERY_3"] = LV_SYMBOL_BATTERY_3;
    icon_map["$BATTERY_2"] = LV_SYMBOL_BATTERY_2;
    icon_map["$BATTERY_1"] = LV_SYMBOL_BATTERY_1;
    icon_map["$BATTERY_EMPTY"] = LV_SYMBOL_BATTERY_EMPTY;
    icon_map["$USB"] = LV_SYMBOL_USB;
    icon_map["$BLUETOOTH"] = LV_SYMBOL_BLUETOOTH;
    icon_map["$BACKSPACE"] = LV_SYMBOL_BACKSPACE;
    icon_map["$SD_CARD"] = LV_SYMBOL_SD_CARD;
    icon_map["$NEW_LINE"] = LV_SYMBOL_NEW_LINE;
    // Add all other symbols from Appendix B if needed
}

// Function to get base HID keycode for a printable ASCII character
// Returns 0 if not a standard mappable key. Handles case insensitivity.
uint8_t get_hid_keycode_for_char(char c) {
    // Based on USB HID Usage Tables 1.12, Section 10: Keyboard/Keypad Page (0x07)
    // This covers common US QWERTY layout characters.
    // Full international support would require a more complex mapping based on layout.

    if (c >= 'a' && c <= 'z') {
        return HID_KEY_A + (c - 'a');
    }
    if (c >= 'A' && c <= 'Z') {
        return HID_KEY_A + (c - 'A');
    }
    if (c >= '1' && c <= '9') {
        return HID_KEY_1 + (c - '1');
    }
    if (c == '0') {
        return HID_KEY_0;
    }

    switch (c) {
        case ' ': return HID_KEY_SPACEBAR;
        case '!': return HID_KEY_1; // Requires Shift
        case '@': return HID_KEY_2; // Requires Shift
        case '#': return HID_KEY_3; // Requires Shift
        case '$': return HID_KEY_4; // Requires Shift
        case '%': return HID_KEY_5; // Requires Shift
        case '^': return HID_KEY_6; // Requires Shift
        case '&': return HID_KEY_7; // Requires Shift
        case '*': return HID_KEY_8; // Requires Shift
        case '(': return HID_KEY_9; // Requires Shift
        case ')': return HID_KEY_0; // Requires Shift
        case '-': return HID_KEY_MINUS;
        case '_': return HID_KEY_MINUS; // Requires Shift
        case '=': return HID_KEY_EQUAL;
        case '+': return HID_KEY_EQUAL; // Requires Shift
        case '[': return HID_KEY_LEFT_BRKT;
        case '{': return HID_KEY_LEFT_BRKT; // Requires Shift
        case ']': return HID_KEY_RIGHT_BRKT;
        case '}': return HID_KEY_RIGHT_BRKT; // Requires Shift
        case '\\': return HID_KEY_BACK_SLASH;
        case '|': return HID_KEY_BACK_SLASH; // Requires Shift
        case ';': return HID_KEY_SEMI_COLON;
        case ':': return HID_KEY_SEMI_COLON; // Requires Shift
        case '\'': return HID_KEY_SGL_QUOTE;
        case '"': return HID_KEY_SGL_QUOTE; // Requires Shift
        case '`': return HID_KEY_GRV_ACCENT;
        case '~': return HID_KEY_GRV_ACCENT; // Requires Shift
        case ',': return HID_KEY_COMMA;
        case '<': return HID_KEY_COMMA; // Requires Shift
        case '.': return HID_KEY_DOT;
        case '>': return HID_KEY_DOT; // Requires Shift
        case '/': return HID_KEY_FWD_SLASH;
        case '?': return HID_KEY_FWD_SLASH; // Requires Shift
        default: return 0; // Not found or requires special handling
    }
}
