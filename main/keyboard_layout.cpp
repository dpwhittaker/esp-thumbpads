#include "keyboard_layout.h"
#include "hid_keycodes.h" // Include the keycode definitions map

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <vector>
#include <string>
#include <map>
#include <libgen.h> // For basename()

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include <sys/stat.h>
#include <unistd.h>
#include "esp_now.h" // For esp_now_send, esp_now_modifier_state_t
#include "esp_now_common.h" // Include shared ESP-NOW definitions

// --- Global Defaults ---
#define DEFAULT_ACTION_DELAY_MS 50
// Delays for string typing
#define TYPING_KEY_EVENT_DELAY_MS 10 // Short delay for individual key event (e.g., shift down, key down, key up, shift up)
#define INTER_CHAR_TYPING_DELAY_MS 20 // Delay between typing subsequent characters in a string


// --- Global Variable Definitions ---
// special_key_map and modifier_map are defined in hid_keycodes.h/cpp
uint32_t current_layout_default_delay = DEFAULT_ACTION_DELAY_MS;
key_mask_t current_modifier_mask = 0;
uint8_t current_held_keys[6] = {0};
uint8_t current_held_keys_count = 0;

// --- LVGL Event Code ---
#define LV_EVENT_LOAD_LAYOUT ((lv_event_code_t)(_LV_EVENT_LAST + 0)) // Use first available user event

// --- Filesystem Paths ---
#define FS_BASE_PATH "/fs"
#define KEYBOARD_LAYOUT_FILE FS_BASE_PATH "/menu.cfg"

// --- Forward Declarations for Static Helpers ---
static const lv_font_t* get_font_for_size(char size_char);
static const char* find_longest_icon_match(const char* text, int& match_len);
static void parse_label_text(const char* label_src, std::vector<label_part_t>& parts);
static void free_label_parts(std::vector<label_part_t>& parts);
static void free_action_sequence(std::vector<action_component_t>& sequence);
static bool parse_action_string(const char* action_str, button_type_t btn_type,
                                std::vector<action_component_t>& press_seq,
                                std::vector<action_component_t>& release_seq,
                                uint32_t& release_delay, char*& nav_filename);
static int parse_one_component(const char* start, const char* end, std::vector<action_component_t>& sequence, const char* full_action_str);
static grid_layout_state_t* init_grid_state(int cols, int rows);
static void free_grid_state(grid_layout_state_t* state);
static bool check_and_occupy(grid_layout_state_t* state, int r, int c, int rs, int cs);
static bool find_auto_placement_slot(grid_layout_state_t* state, int rs, int cs, int& out_r, int& out_c);
static bool add_held_key(uint8_t keycode);
static void remove_held_key(uint8_t keycode);
static void clear_held_keys();
static void send_current_hid_report();
static sequence_result_t execute_action_sequence(const std::vector<action_component_t>& sequence, bool is_press_sequence);
static bool load_layout_internal(const char* path_to_load, lv_obj_t* screen);
static char* generate_backup_path(const char* current_path);
static void send_esp_now_modifier_update(); // Forward declare the new helper
static bool is_cheat_sheet_file(FILE* f); // Forward declare cheat sheet detection
static bool parse_cheat_sheet_file(FILE* f, cheat_sheet_layout_t& layout); // Forward declare cheat sheet parsing
static void render_cheat_sheet_quadrant(lv_obj_t* parent, const quadrant_t& quad, int x, int y, int w, int h); // Forward declare cheat sheet rendering
static bool create_cheat_sheet_from_file(const char* filename); // Forward declare cheat sheet creation

// --- Cheat Sheet Quadrant Support ---

enum quadrant_type_t {
    QUADRANT_TYPE_GRID,
    QUADRANT_TYPE_GAMEPAD
};

struct quadrant_t {
    quadrant_type_t type;
    std::string header; // e.g. "TL", "TRGPR"
    std::vector<std::string> lines;
};

struct cheat_sheet_layout_t {
    std::map<std::string, quadrant_t> quadrants; // "TL", "TR", "BL", "BR"
};

// Detect if file is a cheat sheet (by quadrant header)
static bool is_cheat_sheet_file(FILE* f) {
    char line[64];
    rewind(f);
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0') continue;
        if ((strncmp(p, "TL", 2) == 0 || strncmp(p, "TR", 2) == 0 ||
             strncmp(p, "BL", 2) == 0 || strncmp(p, "BR", 2) == 0) &&
            (p[2] == '\0' || p[2] == '\n' || p[2] == 'G' || isdigit(p[2]))) {
            return true;
        }
        break;
    }
    rewind(f);
    return false;
}

// Parse cheat sheet file into quadrants
static bool parse_cheat_sheet_file(FILE* f, cheat_sheet_layout_t& layout) {
    rewind(f);
    std::string current_header;
    quadrant_t* current_quad = nullptr;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0') continue;
        if (strncmp(p, "---", 3) == 0) {
            current_header.clear();
            current_quad = nullptr;
            continue;
        }
        if ((strncmp(p, "TL", 2) == 0 || strncmp(p, "TR", 2) == 0 ||
             strncmp(p, "BL", 2) == 0 || strncmp(p, "BR", 2) == 0)) {
            std::string header;
            for (int i = 0; i < 6 && p[i] && !isspace((unsigned char)p[i]); ++i) header += p[i];
            current_header = header;
            quadrant_type_t qtype = (header.size() > 2 && (header[2] == 'G')) ? QUADRANT_TYPE_GAMEPAD : QUADRANT_TYPE_GRID;
            layout.quadrants[header.substr(0,2)] = {qtype, header, {}};
            current_quad = &layout.quadrants[header.substr(0,2)];
            continue;
        }
        if (current_quad) {
            current_quad->lines.push_back(std::string(p));
        }
    }
    return !layout.quadrants.empty();
}

// Render a cheat sheet quadrant (simple version)
static void render_cheat_sheet_quadrant(lv_obj_t* parent, const quadrant_t& quad, int x, int y, int w, int h) {
    if (quad.type == QUADRANT_TYPE_GRID) {
        // For grid: just show as a label (for now)
        std::string text;
        for (const auto& l : quad.lines) text += l;
        lv_obj_t* label = lv_label_create(parent);
        lv_label_set_text(label, text.c_str());
        lv_obj_set_pos(label, x, y);
        lv_obj_set_size(label, w, h);
    } else if (quad.type == QUADRANT_TYPE_GAMEPAD) {
        // For gamepad: parse ButtonName: Label lines and render in a fixed arrangement
        int btn_w = w / 3, btn_h = h / 4, i = 0;
        for (const auto& l : quad.lines) {
            const char* p = l.c_str();
            while (isspace((unsigned char)*p)) p++;
            const char* colon = strchr(p, ':');
            if (!colon) continue;
            std::string btn_name(p, colon - p);
            std::string label = colon + 1;
            lv_obj_t* btn = lv_btn_create(parent);
            lv_obj_set_size(btn, btn_w, btn_h);
            lv_obj_set_pos(btn, x + (i%2)*btn_w, y + (i/2)*btn_h);
            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, label.c_str());
            lv_obj_center(lbl);
            i++;
        }
    }
}

// Main: Create cheat sheet from file
static bool create_cheat_sheet_from_file(const char* filename) {
    lv_obj_t *scr = lv_scr_act();
    FILE* f = fopen(filename, "r");
    if (!f) return false;
    cheat_sheet_layout_t layout;
    if (!parse_cheat_sheet_file(f, layout)) {
        fclose(f);
        return false;
    }
    fclose(f);

    // Divide screen into quadrants and render each
    int scr_w = lv_obj_get_width(scr), scr_h = lv_obj_get_height(scr);
    int half_w = scr_w / 2, half_h = scr_h / 2;
    std::vector<std::pair<std::string, lv_area_t>> quads = {
        {"TL", {0, 0, half_w, half_h}},
        {"TR", {half_w, 0, half_w, half_h}},
        {"BL", {0, half_h, half_w, half_h}},
        {"BR", {half_w, half_h, half_w, half_h}}
    };
    for (const auto& q : quads) {
        auto it = layout.quadrants.find(q.first);
        if (it != layout.quadrants.end()) {
            render_cheat_sheet_quadrant(scr, it->second, q.second.x1, q.second.y1, q.second.x2, q.second.y2);
        }
    }
    return true;
}

// --- Keyboard Creation Helpers ---

// Map FontSize char to LVGL font pointer
static const lv_font_t* get_font_for_size(char size_char) {
    switch (size_char) {
        case 'S': return &lv_font_montserrat_14;
        case 'M': return &lv_font_montserrat_20;
        case 'L': return &lv_font_montserrat_28;
        case 'J': return &lv_font_montserrat_48;
        default:  return &lv_font_montserrat_28; // Default to Large
    }
    // Ensure these fonts are enabled in lv_conf.h
}

// Helper to find longest matching icon code at start of string
static const char* find_longest_icon_match(const char* text, int& match_len) {
    match_len = 0;
    const char* best_match_symbol = nullptr;

    // ** Simple map-based lookup (replace with proper longest match) **
    // This example iterates the map, which isn't efficient for longest match.
    // A real implementation would likely use a Trie or sorted list.
    for (const auto& pair : icon_map) {
        const std::string& code = pair.first;
        if (strncmp(text, code.c_str(), code.length()) == 0) {
            if (code.length() > match_len) {
                match_len = code.length();
                best_match_symbol = pair.second;
            }
        }
    }
    return best_match_symbol;
}


// Helper to parse label text into parts
static void parse_label_text(const char* label_src, std::vector<label_part_t>& parts) {
    const char* current = label_src;
    std::string current_text_part;

    while (*current != '\0') {
        if (*current == '$') {
            if (*(current + 1) == '$') { // Handle $$ escape
                current_text_part += '$';
                current += 2;
            } else {
                int match_len = 0;
                const char* icon_symbol = find_longest_icon_match(current, match_len);
                if (icon_symbol) { // Found an icon match
                    // Add any preceding text part
                    if (!current_text_part.empty()) {
                        label_part_t text_part;
                        text_part.type = LABEL_PART_TEXT;
                        text_part.value = strdup(current_text_part.c_str());
                        parts.push_back(text_part);
                        current_text_part.clear();
                    }
                    // Add the icon part
                    label_part_t icon_part;
                    icon_part.type = LABEL_PART_ICON;
                    icon_part.value = strdup(icon_symbol); // Store the LV_SYMBOL_ string
                    parts.push_back(icon_part);
                    current += match_len;
                } else { // No icon match, treat '$' as literal text
                    current_text_part += '$';
                    current++;
                }
            }
        } else if (*current == ' ' && *(current + 1) == ' ') {
            current_text_part += '\n';
            while (*current == ' ') current++;
        } else { // Regular character
            current_text_part += *current;
            current++;
        }
    }

    // Add any remaining text part
    if (!current_text_part.empty()) {
        label_part_t text_part;
        text_part.type = LABEL_PART_TEXT;
        text_part.value = strdup(current_text_part.c_str());
        parts.push_back(text_part);
    }
}

// Helper to free label parts memory
static void free_label_parts(std::vector<label_part_t>& parts) {
    for (auto& part : parts) {
        free(part.value);
    }
    parts.clear();
}

// Helper to free action component memory (needs careful handling of string_data.str)
static void free_action_sequence(std::vector<action_component_t>& sequence) {
    for (auto& comp : sequence) {
        if (comp.type == ACTION_COMP_TYPE_STRING && comp.data.string_data.str) {
            free(comp.data.string_data.str);
        }
        // Add freeing for other types if they allocate memory
    }
    sequence.clear();
}


// --- Keyboard Creation from File (Major Rework Needed) ---

// Function to initialize grid state
static grid_layout_state_t* init_grid_state(int cols, int rows) {
    if (cols <= 0 || rows <= 0) return nullptr;
    grid_layout_state_t* state = (grid_layout_state_t*)malloc(sizeof(grid_layout_state_t));
    if (!state) return nullptr;
    state->cols = cols;
    state->rows = rows;
    state->occupied = (bool**)malloc(rows * sizeof(bool*));
    if (!state->occupied) {
        free(state);
        return nullptr;
    }
    for (int r = 0; r < rows; ++r) {
        state->occupied[r] = (bool*)calloc(cols, sizeof(bool)); // Initialize to false (unoccupied)
        if (!state->occupied[r]) {
            // Cleanup allocated rows
            for (int i = 0; i < r; ++i) free(state->occupied[i]);
            free(state->occupied);
            free(state);
            return nullptr;
        }
    }
    return state;
}

// Function to free grid state
static void free_grid_state(grid_layout_state_t* state) {
    if (!state) return;
    if (state->occupied) {
        for (int r = 0; r < state->rows; ++r) {
            free(state->occupied[r]);
        }
        free(state->occupied);
    }
    free(state);
}

// Function to check if an area is free and mark it occupied
static bool check_and_occupy(grid_layout_state_t* state, int r, int c, int rs, int cs) {
    if (!state || r < 0 || c < 0 || rs <= 0 || cs <= 0 ||
        r + rs > state->rows || c + cs > state->cols) {
        return false; // Out of bounds
    }
    // Check if all cells are free
    for (int i = r; i < r + rs; ++i) {
        for (int j = c; j < c + cs; ++j) {
            if (state->occupied[i][j]) {
                return false; // Area overlaps with occupied cell
            }
        }
    }
    // Mark cells as occupied
    for (int i = r; i < r + rs; ++i) {
        for (int j = c; j < c + cs; ++j) {
            state->occupied[i][j] = true;
        }
    }
    return true;
}

// Function to find the next available slot for auto-placement
static bool find_auto_placement_slot(grid_layout_state_t* state, int rs, int cs, int& out_r, int& out_c) {
    if (!state || rs <= 0 || cs <= 0) return false;
    for (int r = 0; r <= state->rows - rs; ++r) {
        for (int c = 0; c <= state->cols - cs; ++c) {
            bool area_free = true;
            // Check if the rs x cs area starting at (r, c) is free
            for (int i = r; i < r + rs; ++i) {
                for (int j = c; j < c + cs; ++j) {
                    if (state->occupied[i][j]) {
                        area_free = false;
                        break;
                    }
                }
                if (!area_free) break;
            }

            if (area_free) {
                // Found a slot, mark it and return
                if (check_and_occupy(state, r, c, rs, cs)) {
                     out_r = r;
                     out_c = c;
                     return true;
                } else {
                    // This should not happen if the initial check passed, but log if it does
                    ESP_LOGE(TAG, "Layout Error: Failed to occupy supposedly free slot at (%d, %d)", r, c);
                    return false; // Treat as error
                }
            }
        }
    }
    return false; // No suitable slot found
}

// Helper function to parse a single component from an action string sequence
// Returns the number of characters consumed from 'start', or -1 on error.
static int parse_one_component(const char* start, const char* end, std::vector<action_component_t>& sequence, const char* full_action_str) {
    const char* p = start;
    key_mask_t prefix_mods_mask = 0; // Modifiers found as prefixes
    int prefix_len = 0;

    // 1. Tentatively parse modifier prefixes
    const char* after_prefix_p = p;
    while (after_prefix_p < end) {
        bool mod_found = false;
        if (after_prefix_p + 1 < end) {
            char mod_buf[3] = {after_prefix_p[0], after_prefix_p[1], '\0'};
            auto it = modifier_map.find(mod_buf);
            if (it != modifier_map.end()) {
                prefix_mods_mask |= it->second;
                after_prefix_p += 2;
                mod_found = true;
                // Skip potential whitespace after modifier
                while (after_prefix_p < end && isspace((unsigned char)*after_prefix_p)) after_prefix_p++;
            }
        }
        if (!mod_found) break; // No more modifiers found
    }
    prefix_len = after_prefix_p - p; // Length consumed by prefixes and subsequent whitespace

    // 2. Look ahead: Check if a main component follows the prefixes
    bool main_component_follows = false;
    if (after_prefix_p < end) {
        char next_char = *after_prefix_p;
        if (next_char == '\'' || next_char == '{' || next_char == '"' || next_char == '(' || next_char == '\\') {
            main_component_follows = true;
        }
    }

    // 3. Decide how to handle the parsed prefix
    action_component_t comp; // Reusable component struct

    if (prefix_mods_mask != 0) {
        if (main_component_follows) {
            // --- Case A: Prefix applies to a following main component ---
            comp.type = ACTION_COMP_TYPE_MOD_PRESS;
            comp.data.modifier_mask = prefix_mods_mask;
            sequence.push_back(comp);
            ESP_LOGD(TAG, "  Parsed: MOD_PRESS (Prefix) 0x%02X", prefix_mods_mask);
            p = after_prefix_p; // Advance p past the prefix to parse the main component
        } else {
            // --- Case B: Prefix is a standalone action ---
            comp.type = ACTION_COMP_TYPE_MOD_PRESS;
            comp.data.modifier_mask = prefix_mods_mask;
            sequence.push_back(comp);
            ESP_LOGD(TAG, "  Parsed: MOD_PRESS (Standalone) 0x%02X", prefix_mods_mask);
            return prefix_len; // Consume only the prefix length
        }
    } else if (prefix_len > 0) {
         // This case should ideally not happen if prefix_mods_mask is 0,
         // but handles potential whitespace consumption if no valid prefix was found.
         p = after_prefix_p;
    }

    // If we reached here, either there was no prefix, or the prefix was handled
    // and 'p' is now pointing at the start of the main component (or end of string).

    if (p >= end) {
        // If only whitespace was consumed, or nothing was left after a prefix
        return p - start;
    }

    // 4. Parse the main component type (if not handled as standalone prefix)
    int consumed_main = 0;

    if (*p == '\'') { // --- Simultaneous keys --- 'k' or 'keys'
        const char* literal_start = p + 1;
        const char* literal_end = literal_start;
        while (literal_end < end && *literal_end != '\'') {
            literal_end++;
        }

        if (literal_end >= end || *literal_end != '\'') {
             ESP_LOGE(TAG, "Unterminated key literal starting near '...%s' in '%s'", start, full_action_str);
             return -1;
        }

        comp.type = ACTION_COMP_TYPE_KEYS_SIMULT;
        comp.data.simult_keys.count = 0;
        std::string keys_str; // For logging
        for (const char* k = literal_start; k < literal_end && comp.data.simult_keys.count < 6; ++k) {
            uint8_t keycode = get_hid_keycode_for_char(*k);
            if (keycode != 0) {
                 comp.data.simult_keys.keycodes[comp.data.simult_keys.count++] = keycode;
                 keys_str += *k; // Append char to log string
            } else {
                 ESP_LOGW(TAG, "Ignoring invalid char '%c' (0x%02X) in literal near '...%s' in '%s'", *k, (unsigned char)*k, start, full_action_str);
            }
        }
        if (comp.data.simult_keys.count > 0) {
            sequence.push_back(comp);
            ESP_LOGD(TAG, "  Parsed: KEYS_SIMULT (%d keys: '%s')", comp.data.simult_keys.count, keys_str.c_str());
        } else if (literal_start == literal_end) {
             ESP_LOGW(TAG, "Empty key literal '' found near '...%s' in '%s'", start, full_action_str);
        }
        consumed_main = (literal_end - p) + 1;

    } else if (*p == '{') { // --- Special key --- {KEY_NAME}
        const char* name_start = p + 1;
        const char* name_end = name_start;
        while (name_end < end && *name_end != '}') {
            name_end++;
        }

        if (name_end >= end || *name_end != '}') {
            ESP_LOGE(TAG, "Unterminated special key name starting near '...%s' in '%s'", start, full_action_str);
            return -1;
        }

        std::string key_name(name_start, name_end - name_start);
        auto it_key = special_key_map.find(key_name);
        auto it_mod = modifier_map.find(key_name); // Also check if it's a modifier name like {LCTRL}

        if (it_key != special_key_map.end()) {
            comp.type = ACTION_COMP_TYPE_SPECIAL_KEY;
            comp.data.keycode = it_key->second;
            sequence.push_back(comp);
            ESP_LOGD(TAG, "  Parsed: SPECIAL_KEY {%s} (0x%02X)", key_name.c_str(), comp.data.keycode);
        } else if (it_mod != modifier_map.end()) {
             comp.type = ACTION_COMP_TYPE_MOD_PRESS;
             comp.data.modifier_mask = it_mod->second;
             sequence.push_back(comp);
             ESP_LOGD(TAG, "  Parsed: MOD_PRESS {%s} (0x%02X)", key_name.c_str(), comp.data.modifier_mask);
        } else {
             ESP_LOGE(TAG, "Unknown special key name '{%s}' near '...%s' in '%s'", key_name.c_str(), start, full_action_str);
             return -1;
        }
        consumed_main = (name_end - p) + 1;

    } else if (*p == '"') { // --- String literal --- "string content"
        const char* str_start = p + 1;
        const char* str_end = str_start;
        while (str_end < end && *str_end != '"') {
            str_end++;
        }

        if (str_end >= end || *str_end != '"') {
            ESP_LOGE(TAG, "Unterminated string literal starting near '...%s' in '%s'", start, full_action_str);
            return -1;
        }

        comp.type = ACTION_COMP_TYPE_STRING;
        comp.data.string_data.str = strndup(str_start, str_end - str_start);
        if (!comp.data.string_data.str) {
            ESP_LOGE(TAG, "Failed to allocate memory for string literal near '...%s' in '%s'", start, full_action_str);
            return -1;
        }
        sequence.push_back(comp);
        ESP_LOGD(TAG, "  Parsed: STRING \"%s\"", comp.data.string_data.str);
        consumed_main = (str_end - p) + 1;

    } else if (*p == '(') { // --- Delay --- (<ms>)
        char* end_delay_ptr;
        errno = 0;
        long delay_val = strtol(p + 1, &end_delay_ptr, 10);

        if (errno == 0 && end_delay_ptr > p + 1 && end_delay_ptr < end && *end_delay_ptr == ')') {
            comp.type = ACTION_COMP_TYPE_DELAY;
            comp.data.delay_ms = (delay_val >= 0) ? (uint32_t)delay_val : 0;
            sequence.push_back(comp);
            ESP_LOGD(TAG, "  Parsed: DELAY (%u ms)", comp.data.delay_ms);
            consumed_main = (end_delay_ptr - p) + 1;
        } else {
             ESP_LOGE(TAG, "Invalid delay format starting near '...%s' in '%s'", start, full_action_str);
             return -1;
        }

    } else if (*p == '\\') { // --- Modifier release --- \LC, \LS etc.
        if (p + 2 < end) {
            char mod_buf[3] = {p[1], p[2], '\0'};
            auto it = modifier_map.find(mod_buf);
            if (it != modifier_map.end()) {
                comp.type = ACTION_COMP_TYPE_MOD_RELEASE;
                comp.data.modifier_mask = it->second;
                sequence.push_back(comp);
                ESP_LOGD(TAG, "  Parsed: MOD_RELEASE \\%s (0x%02X)", mod_buf, comp.data.modifier_mask);
                consumed_main = 3;
            } else {
                 ESP_LOGE(TAG, "Unknown modifier release code '\\%c%c' near '...%s' in '%s'", p[1], p[2], start, full_action_str);
                 return -1;
            }
        } else {
             ESP_LOGE(TAG, "Incomplete modifier release code starting near '...%s' in '%s'", start, full_action_str);
             return -1;
        }

    } else {
        ESP_LOGE(TAG, "Unexpected character '%c' (0x%02X) in action string near '...%s' in '%s'", *p, (unsigned char)*p, start, full_action_str);
        return -1;
    }

    return (p - start) + consumed_main;
}

// Parse the ActionString based on v3.12 specification
static bool parse_action_string(const char* action_str, button_type_t btn_type,
                         std::vector<action_component_t>& press_seq,
                         std::vector<action_component_t>& release_seq,
                         uint32_t& release_delay, char*& nav_filename)
{
    press_seq.clear();
    release_seq.clear();
    release_delay = 0;
    nav_filename = nullptr;

    if (!action_str) {
        ESP_LOGE(TAG, "NULL action string provided.");
        return false;
    }

    ESP_LOGD(TAG, "Parsing ActionString (Type %d): '%s'", btn_type, action_str);

    if (btn_type == BUTTON_TYPE_NAVIGATION) {
        bool valid_filename = true;
        const char* p = action_str;
        if (!*p) valid_filename = false;
        while(*p) {
            if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-') {
                 if (isspace((unsigned char)*p) || strchr("|'\"{}()<>", *p)) {
                    valid_filename = false;
                    break;
                 }
                 if (*p == '/') {
                     valid_filename = false;
                     break;
                 }
            }
            p++;
        }
        const char* dot = strrchr(action_str, '.');
        if (!dot || strcmp(dot, ".cfg") != 0) {
             ESP_LOGW(TAG, "Navigation filename '%s' does not end with .cfg", action_str);
        }

        if (valid_filename) {
            nav_filename = strdup(action_str);
            if (!nav_filename) {
                ESP_LOGE(TAG, "Failed to allocate memory for navigation filename: %s", action_str);
                return false;
            }
            ESP_LOGD(TAG, "  Parsed as Navigation Filename: '%s'", nav_filename);
            return true;
        } else {
            ESP_LOGE(TAG, "Invalid navigation filename format: '%s'", action_str);
            return false;
        }
    }

    const char* action_str_end = action_str + strlen(action_str);
    const char* pipe_pos = strchr(action_str, '|');
    const char* press_end = (pipe_pos != nullptr) ? pipe_pos : action_str_end;

    ESP_LOGD(TAG, " Parsing Press Sequence...");
    const char* current_pos = action_str;
    while (current_pos < press_end) {
        while (current_pos < press_end && isspace((unsigned char)*current_pos)) {
            current_pos++;
        }
        if (current_pos >= press_end) break;

        int consumed = parse_one_component(current_pos, press_end, press_seq, action_str);
        if (consumed <= 0) {
            ESP_LOGE(TAG, "Failed to parse press sequence in action string: '%s'", action_str);
            free_action_sequence(press_seq);
            return false;
        }
        current_pos += consumed;
    }

    if (pipe_pos != nullptr) {
        ESP_LOGD(TAG, " Parsing Release Sequence...");
        current_pos = pipe_pos + 1;

        while (current_pos < action_str_end && isspace((unsigned char)*current_pos)) {
            current_pos++;
        }
        if (current_pos < action_str_end && *current_pos == '(') {
            char* end_delay_ptr;
            errno = 0;
            long delay_val = strtol(current_pos + 1, &end_delay_ptr, 10);
            if (errno == 0 && end_delay_ptr > current_pos + 1 && end_delay_ptr < action_str_end && *end_delay_ptr == ')') {
                release_delay = (delay_val >= 0) ? (uint32_t)delay_val : 0;
                current_pos = end_delay_ptr + 1;
                ESP_LOGD(TAG, "  Parsed Release Delay: %u ms", release_delay);
            } else {
                ESP_LOGE(TAG, "Invalid release delay format near '|' in action string: '%s'", action_str);
                free_action_sequence(press_seq);
                return false;
            }
        }

        while (current_pos < action_str_end) {
            while (current_pos < action_str_end && isspace((unsigned char)*current_pos)) {
                current_pos++;
            }
            if (current_pos >= action_str_end) break;

            int consumed = parse_one_component(current_pos, action_str_end, release_seq, action_str);
            if (consumed <= 0) {
                ESP_LOGE(TAG, "Failed to parse release sequence in action string: '%s'", action_str);
                free_action_sequence(press_seq);
                free_action_sequence(release_seq);
                return false;
            }
            current_pos += consumed;
        }
    }

    ESP_LOGD(TAG, " ActionString parsing successful.");
    return true;
}

// Update: create_keyboard_from_file to support cheat sheets
bool create_keyboard_from_file(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) return false;
    bool is_cheat = is_cheat_sheet_file(f);
    fclose(f);
    if (is_cheat) {
        return create_cheat_sheet_from_file(filename);
    }

    lv_obj_t *scr = lv_scr_act();
    FILE* f_normal = fopen(filename, "r");

    if (f_normal == NULL) {
        ESP_LOGE(TAG, "Failed to open keyboard layout file: %s (errno %d)", filename, errno);
        return false;
    }

    ESP_LOGI(TAG, "Loading keyboard layout from: %s", filename);

    int grid_cols = 0, grid_rows = 0;
    uint32_t file_default_delay = DEFAULT_ACTION_DELAY_MS;
    char first_line[64];
    if (!fgets(first_line, sizeof(first_line), f_normal)) {
        ESP_LOGE(TAG, "Failed to read first line from: %s", filename);
        fclose(f_normal);
        return false;
    }

    int n_scanned = sscanf(first_line, "%dx%d (%u)", &grid_cols, &grid_rows, &file_default_delay);
    if (n_scanned < 2) {
        n_scanned = sscanf(first_line, "%dx%d", &grid_cols, &grid_rows);
        if (n_scanned != 2) {
            ESP_LOGE(TAG, "Invalid grid dimensions format in first line: %s", first_line);
            fclose(f_normal);
            return false;
        }
        file_default_delay = DEFAULT_ACTION_DELAY_MS;
    } else if (n_scanned == 2) {
         file_default_delay = DEFAULT_ACTION_DELAY_MS;
    }
    if (n_scanned == 3) {
         file_default_delay = file_default_delay;
    }

    if (grid_cols <= 0 || grid_rows <= 0 || grid_cols > 10 || grid_rows > 10) {
        ESP_LOGE(TAG, "Invalid grid dimensions (%d x %d) in file: %s", grid_cols, grid_rows, filename);
        fclose(f_normal);
        return false;
    }
    current_layout_default_delay = file_default_delay;
    ESP_LOGI(TAG, "Grid: %d x %d, Default Delay: %d ms", grid_cols, grid_rows, current_layout_default_delay);

    grid_layout_state_t* layout_state = init_grid_state(grid_cols, grid_rows);
    if (!layout_state) {
        ESP_LOGE(TAG, "Failed to allocate grid layout state (%dx%d)", grid_cols, grid_rows);
        fclose(f_normal);
        return false;
    }

    static lv_style_t grid_style;
    lv_style_init(&grid_style);
    lv_style_set_pad_row(&grid_style, 2);
    lv_style_set_pad_column(&grid_style, 2);
    lv_style_set_pad_all(&grid_style, 0);
    lv_style_set_border_width(&grid_style, 1);
    lv_style_set_outline_width(&grid_style, 1);
    lv_style_set_bg_color(&grid_style, lv_color_black());
    lv_style_set_bg_opa(&grid_style, LV_OPA_COVER);

    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
    lv_obj_center(grid);
    lv_obj_add_style(grid, &grid_style, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);

    static lv_coord_t col_dsc_vec[11];
    static lv_coord_t row_dsc_vec[11];

    if (grid_cols > 10 || grid_rows > 10) {
        ESP_LOGE(TAG, "Grid dimensions (%dx%d) exceed static array limits (10x10). Max 10 allowed.", grid_cols, grid_rows);
        fclose(f_normal);
        free_grid_state(layout_state);
        return false;
    }

    for (int i = 0; i < grid_cols; i++) col_dsc_vec[i] = LV_GRID_FR(1);
    col_dsc_vec[grid_cols] = LV_GRID_TEMPLATE_LAST;
    for (int i = 0; i < grid_rows; i++) row_dsc_vec[i] = LV_GRID_FR(1);
    row_dsc_vec[grid_rows] = LV_GRID_TEMPLATE_LAST;

    lv_obj_set_grid_dsc_array(grid, col_dsc_vec, row_dsc_vec);

    static lv_style_t btn_style;
    lv_style_init(&btn_style);
    lv_style_set_bg_color(&btn_style, lv_palette_darken(LV_PALETTE_GREY, 3));
    lv_style_set_bg_opa(&btn_style, LV_OPA_COVER);
    lv_style_set_radius(&btn_style, 10);
    lv_style_set_pad_all(&btn_style, 2);
    lv_style_set_shadow_width(&btn_style, 0);

    static lv_style_t btn_style_toggled;
    lv_style_init(&btn_style_toggled);
    lv_style_set_bg_color(&btn_style_toggled, lv_palette_main(LV_PALETTE_LIGHT_BLUE));

    char line_buffer[256];
    int line_num = 1;
    bool layout_ok = true;

    while (fgets(line_buffer, sizeof(line_buffer), f_normal)) {
        line_num++;
        char* line_start = line_buffer;

        while (isspace((unsigned char)*line_start)) line_start++;

        if (line_start[0] == '#' || line_start[0] == '\0') continue;

        char* line_end = line_start + strlen(line_start) - 1;
        while (line_end >= line_start && isspace((unsigned char)*line_end)) {
            *line_end = '\0';
            line_end--;
        }
        if (strlen(line_start) == 0) continue;
        ESP_LOGI(TAG, "L%d: Read line: %s", line_num, line_start);

        const char* p = line_start;
        button_type_t button_type = BUTTON_TYPE_MOMENTARY;
        int grid_info_digits = 0;
        int col = -1, row = -1, col_span = 1, row_span = 1;
        char font_size = 'M';
        const char* label_text_start = nullptr;
        const char* action_string_start = nullptr;
        bool explicit_placement = false;

        if (*p == 'T') {
            button_type = BUTTON_TYPE_TOGGLE;
            p++;
        } else if (*p == 'G') {
            button_type = BUTTON_TYPE_NAVIGATION;
            p++;
        }

        const char* grid_info_start = p;
        while (isdigit((unsigned char)*p)) {
            grid_info_digits++;
            p++;
        }
        if (grid_info_digits == 2) {
            sscanf(grid_info_start, "%1d%1d", &col_span, &row_span);
            if (col_span < 1) col_span = 1;
            if (row_span < 1) row_span = 1;
            explicit_placement = false;
        } else if (grid_info_digits == 4) {
            sscanf(grid_info_start, "%1d%1d%1d%1d", &col, &row, &col_span, &row_span);
            if (col_span < 1) col_span = 1;
            if (row_span < 1) row_span = 1;
            if (col < 0 || row < 0 || col >= grid_cols || row >= grid_rows ||
                col + col_span > grid_cols || row + row_span > grid_rows) {
                ESP_LOGE(TAG, "L%d: Explicit placement out of bounds: %s", line_num, line_start);
                layout_ok = false; continue;
            }
            explicit_placement = true;
        } else if (grid_info_digits == 0) {
            col_span = 1; row_span = 1;
            explicit_placement = false;
        } else {
            ESP_LOGE(TAG, "L%d: Invalid GridInfo digit count (%d): %s", line_num, grid_info_digits, line_start);
            layout_ok = false; continue;
        }

        if (*p == 'S' || *p == 'M' || *p == 'L' || *p == 'J') {
            font_size = *p;
            p++;
        } else {
            ESP_LOGE(TAG, "L%d: Missing or invalid FontSize (S, M, L, J): %s", line_num, line_start);
            layout_ok = false; continue;
        }

        label_text_start = p;
        char* tab_ptr = strchr(p, '\t');
        if (tab_ptr == NULL) {
            ESP_LOGE(TAG, "L%d: Missing Tab separator: %s", line_num, line_start);
            layout_ok = false; continue;
        }
        *tab_ptr = '\0';
        action_string_start = tab_ptr + 1;

        while (isspace((unsigned char)*action_string_start)) action_string_start++;
        char* action_end = (char*)action_string_start + strlen(action_string_start) - 1;
        while (action_end >= action_string_start && isspace((unsigned char)*action_end)) {
            *action_end = '\0';
            action_end--;
        }

        ESP_LOGD(TAG, "L%d: Parsed: Type=%d, Grid=%d,%d %dx%d (Exp:%d), Font=%c, Label='%s', Action='%s'",
                 line_num, button_type, col, row, col_span, row_span, explicit_placement, font_size, label_text_start, action_string_start);

        if (explicit_placement) {
            if (!check_and_occupy(layout_state, row, col, row_span, col_span)) {
                ESP_LOGE(TAG, "L%d: Explicit placement (%d,%d %dx%d) overlaps existing button: %s",
                         line_num, col, row, col_span, row_span, line_start);
                layout_ok = false; continue;
            }
             ESP_LOGD(TAG, "L%d: Placed explicitly at (%d, %d)", line_num, col, row);
        } else {
            if (!find_auto_placement_slot(layout_state, row_span, col_span, row, col)) {
                 ESP_LOGE(TAG, "L%d: No space found for auto-placement (%dx%d): %s",
                          line_num, col_span, row_span, line_start);
                 layout_ok = false; continue;
            }
             ESP_LOGD(TAG, "L%d: Placed automatically at (%d, %d)", line_num, col, row);
        }

        button_action_t* action = new button_action_t();
        if (!action) {
            ESP_LOGE(TAG, "L%d: Failed to allocate memory for button action!", line_num);
            layout_ok = false; continue;
        }
        action->type = button_type;
        action->font_size_char = font_size;
        action->toggle_state = false;
        action->navigation_filename = nullptr;
        action->release_delay_ms = 0;
        action->grid_col = col; action->grid_row = row;
        action->grid_col_span = col_span; action->grid_row_span = row_span;

        parse_label_text(label_text_start, action->label_parts);

        if (!parse_action_string(action_string_start, action->type,
                                 action->press_sequence, action->release_sequence,
                                 action->release_delay_ms, action->navigation_filename))
        {
            ESP_LOGE(TAG, "L%d: Failed to parse action string: %s", line_num, action_string_start);
            free_label_parts(action->label_parts);
            if (action->navigation_filename) free(action->navigation_filename);
            delete action;
            layout_ok = false; continue;
        }

        lv_obj_t *btn = lv_btn_create(grid);
        lv_obj_add_style(btn, &btn_style, 0);
        lv_obj_add_style(btn, &btn_style_toggled, LV_STATE_CHECKED);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, col, col_span, LV_GRID_ALIGN_STRETCH, row, row_span);

        lv_obj_t *label_obj = lv_label_create(btn);
        lv_label_set_long_mode(label_obj, LV_LABEL_LONG_WRAP);

        std::string full_label_str;
        for(const auto& part : action->label_parts) {
            full_label_str += part.value;
        }
        lv_label_set_text(label_obj, full_label_str.c_str());

        const lv_font_t* font = get_font_for_size(action->font_size_char);
        lv_obj_set_style_text_font(label_obj, font, 0);
        lv_obj_set_style_text_align(label_obj, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label_obj, lv_color_black(), 0);

        lv_obj_set_width(label_obj, LV_PCT(100));
        lv_obj_center(label_obj);

        lv_obj_set_user_data(btn, action);

        lv_obj_add_event_cb(btn, keyboard_event_cb, LV_EVENT_ALL, NULL);

    }

    fclose(f_normal);
    free_grid_state(layout_state); // Free the layout tracker

    if (!layout_ok) {
        ESP_LOGE(TAG, "Errors encountered during layout parsing/placement for %s.", filename);
        return false;
    }

    lv_obj_add_event_cb(grid, cleanup_keyboard_cb, LV_EVENT_DELETE, NULL);

    ESP_LOGI(TAG, "Keyboard layout loaded successfully from %s.", filename);
    return true;
}


// --- Event Handlers ---

// Function to add a key to the held list (checks duplicates and limit)
static bool add_held_key(uint8_t keycode) {
    if (keycode == 0) return true;
    for (int i = 0; i < current_held_keys_count; ++i) {
        if (current_held_keys[i] == keycode) return true;
    }
    if (current_held_keys_count >= 6) {
        ESP_LOGW(TAG, "HID key limit (6) reached, cannot press keycode 0x%02X", keycode);
        return false;
    }
    current_held_keys[current_held_keys_count++] = keycode;
    return true;
}

// Function to remove a key from the held list
static void remove_held_key(uint8_t keycode) {
     if (keycode == 0) return;
     int found_idx = -1;
     for (int i = 0; i < current_held_keys_count; ++i) {
         if (current_held_keys[i] == keycode) {
             found_idx = i;
             break;
         }
     }
     if (found_idx != -1) {
         for (int i = found_idx; i < current_held_keys_count - 1; ++i) {
             current_held_keys[i] = current_held_keys[i + 1];
         }
         current_held_keys_count--;
         current_held_keys[current_held_keys_count] = 0;
     }
}

// Function to clear all held keys
static void clear_held_keys() {
    memset(current_held_keys, 0, sizeof(current_held_keys));
    current_held_keys_count = 0;
}

// Function to send the current HID report
static void send_current_hid_report() {
    if (!sec_conn) return;

    uint8_t combined_mask = current_modifier_mask | remote_modifier_mask;

    uint8_t keys_to_send[6];
    uint8_t keys_to_send_count = 0;
    for(int i=0; i<current_held_keys_count; ++i) {
        if (current_held_keys[i] != 0) {
            keys_to_send[keys_to_send_count++] = current_held_keys[i];
        }
    }

    esp_hidd_send_keyboard_value(hid_conn_id, combined_mask, keys_to_send_count > 0 ? keys_to_send : NULL, keys_to_send_count);
    ESP_LOGD(TAG, "Sent HID Report: Mod=0x%02X, Keys=[%d] 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X",
             combined_mask, keys_to_send_count,
             keys_to_send_count > 0 ? keys_to_send[0] : 0,
             keys_to_send_count > 1 ? keys_to_send[1] : 0,
             keys_to_send_count > 2 ? keys_to_send[2] : 0,
             keys_to_send_count > 3 ? keys_to_send[3] : 0,
             keys_to_send_count > 4 ? keys_to_send[4] : 0,
             keys_to_send_count > 5 ? keys_to_send[5] : 0);
}

// Helper function to send the current modifier state via ESP-NOW
static void send_esp_now_modifier_update() {
    esp_now_modifier_state_t current_state;
    current_state.modifier_mask = current_modifier_mask;
    esp_err_t send_result = esp_now_send(broadcast_mac_address, (uint8_t *)&current_state, sizeof(current_state));
    if (send_result != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW send error updating mods: %s", esp_err_to_name(send_result));
    } else {
        ESP_LOGD(TAG, "Sent ESP-NOW modifier update: 0x%02X", current_modifier_mask);
    }
}

// Function to execute an action sequence and return the net modifier mask change and keys pressed
static sequence_result_t execute_action_sequence(const std::vector<action_component_t>& sequence, bool is_press_sequence) {
    ESP_LOGD(TAG, "Executing sequence (count: %zu, press: %d)", sequence.size(), is_press_sequence);
    sequence_result_t result = {0}; // Initialize result
    key_mask_t initial_mask = current_modifier_mask;
    key_mask_t current_sequence_mask = initial_mask; // Track mask changes within this sequence execution

    for (const auto& comp : sequence) {
        bool key_added_in_this_comp = false; // Track if a key was added by the current component

        switch (comp.type) {
            case ACTION_COMP_TYPE_KEYCODE:
            case ACTION_COMP_TYPE_SPECIAL_KEY:
                ESP_LOGD(TAG, "  Comp: Key 0x%02X (%s)", comp.data.keycode, is_press_sequence ? "Press" : "Release");
                if (is_press_sequence) {
                    if (add_held_key(comp.data.keycode)) {
                        result.keys_pressed.push_back(comp.data.keycode); // Store keycode if successfully added
                        key_added_in_this_comp = true;
                    }
                } else {
                    remove_held_key(comp.data.keycode);
                }
                send_current_hid_report(); // Send report after key change
                break;

            case ACTION_COMP_TYPE_MOD_PRESS:
                 ESP_LOGD(TAG, "  Comp: Mod Press 0x%02X", comp.data.modifier_mask);
                 current_sequence_mask |= comp.data.modifier_mask; // Update local tracker
                 current_modifier_mask |= comp.data.modifier_mask;    // Update global state
                 send_current_hid_report(); // Send report immediately for modifier changes
                 send_esp_now_modifier_update(); // <<< ADDED: Send ESP-NOW update
                 break;

            case ACTION_COMP_TYPE_MOD_RELEASE:
                 ESP_LOGD(TAG, "  Comp: Mod Release 0x%02X", comp.data.modifier_mask);
                 current_sequence_mask &= ~comp.data.modifier_mask; // Update local tracker
                 current_modifier_mask &= ~comp.data.modifier_mask; // Update global state
                 send_current_hid_report(); // Send report immediately for modifier changes
                 send_esp_now_modifier_update(); // <<< ADDED: Send ESP-NOW update
                 break;

            case ACTION_COMP_TYPE_DELAY:
                 ESP_LOGD(TAG, "  Comp: Delay %u ms", comp.data.delay_ms); // Use %u for uint32_t
                 if (comp.data.delay_ms > 0) {
                     vTaskDelay(pdMS_TO_TICKS(comp.data.delay_ms));
                 }
                 break;

            case ACTION_COMP_TYPE_STRING:
                 ESP_LOGD(TAG, "  Comp: String '%s'", comp.data.string_data.str ? comp.data.string_data.str : "<null_str>");
                if (comp.data.string_data.str) {
                    const char* str_to_type = comp.data.string_data.str;
                    for (int i = 0; str_to_type[i] != '\0'; ++i) {
                        char char_to_type = str_to_type[i];
                        ESP_LOGD(TAG, "    Typing char: '%c'", char_to_type);

                        key_mask_t original_persistent_mods = current_modifier_mask; // Global mods state before this char's events
                        key_mask_t mods_for_this_char_event = original_persistent_mods; // Working copy for events of this char

                        uint8_t base_keycode = get_hid_keycode_for_char(char_to_type);
                        if (base_keycode == 0) {
                            ESP_LOGW(TAG, "    Cannot type char '%c' (0x%02X), no HID keycode.", char_to_type, (unsigned char)char_to_type);
                            vTaskDelay(pdMS_TO_TICKS(INTER_CHAR_TYPING_DELAY_MS)); // Still wait a bit before next char
                            continue; // Skip to next character
                        }

                        // Determine if the character itself requires SHIFT (e.g., 'A' or '!')
                        bool char_should_be_shifted = (isalpha(char_to_type) && isupper(char_to_type)) ||
                                                      (strchr("!@#$%^&*()_+{}|:\"~<>?", char_to_type) != NULL);

                        // Check current SHIFT state from persistent modifiers
                        bool shift_is_currently_active_persistently = (original_persistent_mods & (LEFT_SHIFT_KEY_MASK | RIGHT_SHIFT_KEY_MASK)) != 0;
                        
                        // Does the effective shift state need to change for this character?
                        bool need_to_toggle_shift_for_char = (char_should_be_shifted != shift_is_currently_active_persistently);

                        if (need_to_toggle_shift_for_char) {
                            if (char_should_be_shifted) { // Need to turn ON shift
                                mods_for_this_char_event |= LEFT_SHIFT_KEY_MASK;
                            } else { // Need to turn OFF shift
                                mods_for_this_char_event &= ~(LEFT_SHIFT_KEY_MASK | RIGHT_SHIFT_KEY_MASK);
                            }
                            current_modifier_mask = mods_for_this_char_event; // Apply temporary shift change
                            send_current_hid_report();
                            vTaskDelay(pdMS_TO_TICKS(TYPING_KEY_EVENT_DELAY_MS));
                        }
                        // If no toggle needed, mods_for_this_char_event already reflects the correct persistent state

                        // Press the base key (with potentially modified shift)
                        add_held_key(base_keycode);
                        current_modifier_mask = mods_for_this_char_event; // Ensure current_modifier_mask has the right shift state for this event
                        send_current_hid_report();
                        vTaskDelay(pdMS_TO_TICKS(TYPING_KEY_EVENT_DELAY_MS));

                        // Release the base key
                        remove_held_key(base_keycode);
                        // current_modifier_mask (still mods_for_this_char_event) is correct for this event
                        send_current_hid_report();
                        vTaskDelay(pdMS_TO_TICKS(TYPING_KEY_EVENT_DELAY_MS));

                        // Restore original persistent modifier state (current_modifier_mask)
                        current_modifier_mask = original_persistent_mods;
                        if (need_to_toggle_shift_for_char) { // If we had temporarily changed shift, send a report reflecting its restoration
                            send_current_hid_report();
                            vTaskDelay(pdMS_TO_TICKS(TYPING_KEY_EVENT_DELAY_MS));
                        }
                        // At this point, current_modifier_mask is back to original_persistent_mods

                        vTaskDelay(pdMS_TO_TICKS(INTER_CHAR_TYPING_DELAY_MS));
                    }
                }
                 break;

            case ACTION_COMP_TYPE_KEYS_SIMULT:
                 ESP_LOGD(TAG, "  Comp: Simult Keys (%d) (%s)", comp.data.simult_keys.count, is_press_sequence ? "Press" : "Release");
                 if (is_press_sequence) {
                     for(int i=0; i<comp.data.simult_keys.count; ++i) {
                         if (add_held_key(comp.data.simult_keys.keycodes[i])) {
                             result.keys_pressed.push_back(comp.data.simult_keys.keycodes[i]); // Store keycode
                             key_added_in_this_comp = true;
                         }
                     }
                 } else {
                     for(int i=0; i<comp.data.simult_keys.count; ++i) {
                         remove_held_key(comp.data.simult_keys.keycodes[i]);
                     }
                 }
                 // Only send report if keys were actually added/removed or if releasing
                 if (key_added_in_this_comp || !is_press_sequence) {
                    send_current_hid_report(); // Send report after key changes
                 }
                 break;

            default:
                 ESP_LOGW(TAG, "  Comp: Unknown type %d", comp.type);
                 break;
        }
        // Apply default delay AFTER component execution (unless it was a delay itself)
        if (comp.type != ACTION_COMP_TYPE_DELAY && current_layout_default_delay > 0) {
             vTaskDelay(pdMS_TO_TICKS(current_layout_default_delay));
        }

    }
    ESP_LOGD(TAG, "Sequence finished.");
    // Calculate the modifiers effectively ADDED by this sequence execution
    result.modifier_delta = current_sequence_mask & (~initial_mask);
    return result;
}

// Event handler for all keyboard buttons
void keyboard_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e);
    button_action_t* action = (button_action_t*)lv_obj_get_user_data(btn);

    if (!action) return;

    if (action->type == BUTTON_TYPE_NAVIGATION) {
        if (code == LV_EVENT_CLICKED) {
            ESP_LOGI(TAG, "Navigation Request: %s", action->navigation_filename ? action->navigation_filename : "<NULL>");
            if (action->navigation_filename != NULL && strlen(action->navigation_filename) > 0) {
                ESP_LOGI(TAG, "Performing Implicit Reset before navigation.");
                clear_held_keys();
                current_modifier_mask = 0;
                send_current_hid_report();
                lv_obj_t* grid = lv_obj_get_parent(btn);
                if (grid) {
                    uint32_t child_cnt = lv_obj_get_child_cnt(grid);
                    for (uint32_t i = 0; i < child_cnt; i++) {
                        lv_obj_t * child = lv_obj_get_child(grid, i);
                        if (child && lv_obj_check_type(child, &lv_btn_class)) {
                            button_action_t* child_action = (button_action_t*)lv_obj_get_user_data(child);
                            if (child_action && child_action->type == BUTTON_TYPE_TOGGLE && child_action->toggle_state) {
                                child_action->toggle_state = false;
                                lv_obj_clear_state(child, LV_STATE_CHECKED);
                                ESP_LOGD(TAG, "Resetting toggle state for button %d", i);
                            }
                        }
                    }
                }
                char* filename_copy = strdup(action->navigation_filename);
                if (filename_copy) {
                    lv_event_send(lv_scr_act(), LV_EVENT_LOAD_LAYOUT, (void*)filename_copy);
                } else {
                    ESP_LOGE(TAG, "Failed to allocate memory for filename copy!");
                }
            } else {
                 ESP_LOGE(TAG, "Navigation action has NULL or empty filename!");
            }
        }
        return;
    }

    if (!sec_conn && (code == LV_EVENT_PRESSED || code == LV_EVENT_RELEASED || code == LV_EVENT_CLICKED)) {
         ESP_LOGW(TAG, "Action ignored: BLE not securely connected.");
         return;
    }

    if (action->type == BUTTON_TYPE_MOMENTARY) {
        if (code == LV_EVENT_PRESSED) {
            ESP_LOGI(TAG, "Momentary Press: Executing Press Sequence");
            // Execute and store results temporarily for implicit release
            sequence_result_t press_result = execute_action_sequence(action->press_sequence, true);
            action->keys_pressed_by_this_action = press_result.keys_pressed; // Store keys
            action->toggle_modifier_mask = press_result.modifier_delta; // Store modifier delta (reusing field)
            ESP_LOGD(TAG, "  Momentary stored keys: %zu, mods: 0x%02X", action->keys_pressed_by_this_action.size(), action->toggle_modifier_mask);
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            ESP_LOGI(TAG, "Momentary Release:");
            if (action->release_sequence.empty()) {
                // --- Implicit Momentary Release ---
                ESP_LOGI(TAG, "  Implicit momentary release: Releasing %zu keys and mods 0x%02X", action->keys_pressed_by_this_action.size(), action->toggle_modifier_mask);
                bool changed = false;
                // Release keys pressed by this action
                for(uint8_t keycode : action->keys_pressed_by_this_action) {
                    remove_held_key(keycode);
                    changed = true;
                }
                // Release modifiers added by this action
                if (action->toggle_modifier_mask != 0) {
                    current_modifier_mask &= ~action->toggle_modifier_mask;
                    changed = true;
                    send_esp_now_modifier_update(); // <<< ADDED: Send ESP-NOW update
                }
                // Clear temporary storage
                action->keys_pressed_by_this_action.clear();
                action->toggle_modifier_mask = 0;
                // Send report only if something changed
                if (changed) {
                    send_current_hid_report();
                }
            } else {
                 // --- Explicit Momentary Release ---
                 ESP_LOGI(TAG, "  Executing Explicit Release Sequence (Delay: %d ms)", action->release_delay_ms);
                 if (action->release_delay_ms > 0) {
                     vTaskDelay(pdMS_TO_TICKS(action->release_delay_ms));
                 }
                 // Clear temporary storage as explicit sequence handles release
                 action->keys_pressed_by_this_action.clear();
                 action->toggle_modifier_mask = 0;
                 execute_action_sequence(action->release_sequence, false); // Explicit sequence handles keys/mods
            }
        }
    } else if (action->type == BUTTON_TYPE_TOGGLE) {
        if (code == LV_EVENT_CLICKED) {
            action->toggle_state = !action->toggle_state;
            ESP_LOGI(TAG, "Toggle Click: New State: %s", action->toggle_state ? "ON" : "OFF");

            if (action->toggle_state) {
                // --- Toggle ON ---
                lv_obj_add_state(btn, LV_STATE_CHECKED);
                ESP_LOGI(TAG, "  Executing Press Sequence");
                // Execute and store results persistently for this toggle instance
                sequence_result_t press_result = execute_action_sequence(action->press_sequence, true);
                action->keys_pressed_by_this_action = press_result.keys_pressed; // Store keys
                action->toggle_modifier_mask = press_result.modifier_delta; // Store modifier delta
                ESP_LOGD(TAG, "  Toggle ON stored keys: %zu, mods: 0x%02X", action->keys_pressed_by_this_action.size(), action->toggle_modifier_mask);
            } else {
                // --- Toggle OFF ---
                lv_obj_clear_state(btn, LV_STATE_CHECKED);
                if (action->release_sequence.empty()) {
                    // --- Implicit Toggle Release ---
                    ESP_LOGI(TAG, "  Implicit toggle release: Releasing %zu keys and mods 0x%02X", action->keys_pressed_by_this_action.size(), action->toggle_modifier_mask);
                    bool changed = false;
                    // Release keys pressed by this action
                    for(uint8_t keycode : action->keys_pressed_by_this_action) {
                        remove_held_key(keycode);
                        changed = true;
                    }
                    // Release modifiers added by this action
                    if (action->toggle_modifier_mask != 0) {
                        current_modifier_mask &= ~action->toggle_modifier_mask;
                        changed = true;
                        send_esp_now_modifier_update(); // <<< ADDED: Send ESP-NOW update
                    }
                    // Clear persistent storage
                    action->keys_pressed_by_this_action.clear();
                    action->toggle_modifier_mask = 0;
                    // Send report only if something changed
                    if (changed) {
                        send_current_hid_report();
                    }
                } else {
                    // --- Explicit Toggle Release ---
                    ESP_LOGI(TAG, "  Executing Explicit Release Sequence (Delay: %d ms)", action->release_delay_ms);
                    if (action->release_delay_ms > 0) {
                        vTaskDelay(pdMS_TO_TICKS(action->release_delay_ms));
                    }
                    // Clear persistent storage as explicit sequence handles release
                    action->keys_pressed_by_this_action.clear();
                    action->toggle_modifier_mask = 0;
                    execute_action_sequence(action->release_sequence, false); // Explicit sequence handles keys/mods
                }
            }
        }
    }
}

// Cleanup callback to free allocated user data when the grid is deleted
void cleanup_keyboard_cb(lv_event_t * e) {
    lv_obj_t * grid = lv_event_get_target(e);
    if (!grid) return;

    uint32_t child_cnt = lv_obj_get_child_cnt(grid);
    ESP_LOGI(TAG, "Cleaning up keyboard user data for %lu children", child_cnt);

    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t * child = lv_obj_get_child(grid, i);
        if (child && lv_obj_check_type(child, &lv_btn_class)) {
             button_action_t* action = static_cast<button_action_t*>(lv_obj_get_user_data(child));
             if (action) {
                 ESP_LOGD(TAG, "Freeing action data for child %lu (type %d)", i, action->type);
                 free_label_parts(action->label_parts);
                 free_action_sequence(action->press_sequence);
                 free_action_sequence(action->release_sequence);
                 // No need to explicitly free action->keys_pressed_by_this_action (std::vector handles itself)
                 if (action->navigation_filename) {
                     free(action->navigation_filename);
                 }
                 delete action;
                 lv_obj_set_user_data(child, NULL);
             }
        }
    }
     ESP_LOGI(TAG, "Keyboard cleanup finished.");
}

// Helper to generate backup path (.bkp) from a given path (.cfg)
static char* generate_backup_path(const char* current_path) {
    if (!current_path) return nullptr;
    size_t len = strlen(current_path);
    if (len < 4 || strcmp(current_path + len - 4, ".cfg") != 0) {
        ESP_LOGW(TAG, "Cannot generate backup path for non .cfg file: %s", current_path);
        return nullptr;
    }

    char* backup_path = (char*)malloc(len + 1);
    if (!backup_path) {
        ESP_LOGE(TAG, "Failed to allocate memory for backup path");
        return nullptr;
    }
    strcpy(backup_path, current_path);
    strcpy(backup_path + len - 3, "bkp");
    return backup_path;
}

// Helper function to attempt loading a layout and clean screen on failure before next attempt
static bool load_layout_internal(const char* path_to_load, lv_obj_t* screen) {
    if (!path_to_load || !screen) return false;

    ESP_LOGI(TAG, "Attempting load: %s", path_to_load);
    struct stat st;
    if (stat(path_to_load, &st) != 0) {
        ESP_LOGW(TAG, "Layout file not found: %s", path_to_load);
        return false;
    }

    lv_obj_clean(screen);

    if (create_keyboard_from_file(path_to_load)) {
        ESP_LOGI(TAG, "Successfully loaded layout: %s", path_to_load);
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to load layout from: %s", path_to_load);
        return false;
    }
}

// --- Screen Event Handler (Handles Layout Loading - REFACTORED) ---
void screen_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * scr = lv_event_get_target(e);

    static char* previous_layout_path_static = nullptr;
    if (previous_layout_path_static == nullptr) {
        previous_layout_path_static = strdup(KEYBOARD_LAYOUT_FILE);
        if (!previous_layout_path_static) {
             ESP_LOGE(TAG, "CRITICAL: Failed to allocate memory for initial previous_layout_path!");
        }
    }


    if (code == LV_EVENT_LOAD_LAYOUT) {
        char* filename_to_load_base = (char*)lv_event_get_param(e);
        if (!filename_to_load_base) {
            ESP_LOGE(TAG, "Screen Event: Received LV_EVENT_LOAD_LAYOUT with NULL filename!");
            return;
        }

        ESP_LOGI(TAG, "Screen Event: Request to load layout '%s'", filename_to_load_base);

        char current_layout_path[128];
        snprintf(current_layout_path, sizeof(current_layout_path), "%s/%s", FS_BASE_PATH, filename_to_load_base);

        char* backup_layout_path = generate_backup_path(current_layout_path);

        bool load_success = false;

        if (load_layout_internal(current_layout_path, scr)) {
            load_success = true;
            if (previous_layout_path_static && strcmp(previous_layout_path_static, current_layout_path) != 0) {
                free(previous_layout_path_static);
                previous_layout_path_static = strdup(current_layout_path);
                if (!previous_layout_path_static) {
                     ESP_LOGE(TAG, "CRITICAL: Failed to update previous_layout_path!");
                } else {
                     ESP_LOGD(TAG, "Updated previous layout path to: %s", previous_layout_path_static);
                }
            }
        }

        if (!load_success && backup_layout_path != nullptr) {
            ESP_LOGW(TAG, "Load failed for %s. Trying backup: %s", current_layout_path, backup_layout_path);
            if (load_layout_internal(backup_layout_path, scr)) {
                load_success = true;
                 if (previous_layout_path_static && strcmp(previous_layout_path_static, backup_layout_path) != 0) {
                    free(previous_layout_path_static);
                    previous_layout_path_static = strdup(backup_layout_path);
                     if (!previous_layout_path_static) {
                         ESP_LOGE(TAG, "CRITICAL: Failed to update previous_layout_path (from backup)!");
                     } else {
                         ESP_LOGD(TAG, "Updated previous layout path to backup: %s", previous_layout_path_static);
                     }
                }
            }
        }

        if (!load_success && previous_layout_path_static != nullptr) {
            ESP_LOGW(TAG, "Load failed for %s and its backup. Trying previous: %s", current_layout_path, previous_layout_path_static);
             if (load_layout_internal(previous_layout_path_static, scr)) {
                load_success = true;
                ESP_LOGI(TAG, "Successfully loaded previous layout: %s", previous_layout_path_static);
            }
        }

        if (!load_success) {
            ESP_LOGE(TAG, "CRITICAL: All attempts to load a layout failed (requested: %s, backup: %s, previous: %s)",
                     current_layout_path,
                     backup_layout_path ? backup_layout_path : "N/A",
                     previous_layout_path_static ? previous_layout_path_static : "N/A");
            lv_obj_t * label = lv_label_create(scr);
            lv_label_set_text(label, "FATAL ERROR:\nNo keyboard layout\ncould be loaded!");
            lv_obj_center(label);
        }

        if (backup_layout_path) {
            free(backup_layout_path);
        }
        free(filename_to_load_base);

    }
}
