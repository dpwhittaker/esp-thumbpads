#ifndef HID_KEYCODES_H
#define HID_KEYCODES_H

#include <map>
#include <string>
#include <stdint.h>
#include "hid_dev.h" // Include base HID definitions if needed
#include "lvgl.h"    // Include LVGL for LV_SYMBOL_* definitions

// Map for special key names to HID keycodes (Appendix A)
extern std::map<std::string, uint8_t> special_key_map; // Declare as extern

// Map for modifier names to mask bits (Appendix A)
extern std::map<std::string, key_mask_t> modifier_map; // Declare as extern

// Map for Icon Code Name to LVGL Symbol String (Appendix B)
extern std::map<std::string, const char*> icon_map; // Declare as extern

void initialize_keycode_map(); // Keep function declaration
void initialize_icon_map();    // Declare icon map initializer

// Function to get base HID keycode for a printable ASCII character
// Returns 0 if not a standard mappable key. Handles case insensitivity.
uint8_t get_hid_keycode_for_char(char c);

#endif // HID_KEYCODES_H
