#ifndef ESP_NOW_COMMON_H
#define ESP_NOW_COMMON_H

#include <stdint.h>
#include "esp_now.h" // For ESP_NOW_ETH_ALEN

// Shared data structure for ESP-NOW modifier state
typedef struct {
    uint8_t modifier_mask;
} esp_now_modifier_state_t;

// Extern declaration for remote modifier mask
extern volatile uint8_t remote_modifier_mask;

// Extern declaration for broadcast MAC
extern const uint8_t broadcast_mac_address[ESP_NOW_ETH_ALEN];

#endif // ESP_NOW_COMMON_H
