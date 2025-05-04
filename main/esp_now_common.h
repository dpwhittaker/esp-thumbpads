#ifndef ESP_NOW_COMMON_H
#define ESP_NOW_COMMON_H

#include <stdint.h>
#include "esp_now.h" // For ESP_NOW_ETH_ALEN

// Shared data structure for ESP-NOW modifier state
typedef struct {
    uint8_t modifier_mask;
} esp_now_modifier_state_t;

// Shared enum for ESP-NOW pairing status
typedef enum {
    PAIRING_STATUS_INIT,
    PAIRING_STATUS_WAITING, // Broadcasting and listening
    PAIRING_STATUS_DONE,
    PAIRING_STATUS_FAIL
} esp_now_pairing_status_t;

// Extern declarations for global variables defined in thumbpads.cpp
extern volatile esp_now_pairing_status_t pairing_status;
extern uint8_t peer_mac_address[ESP_NOW_ETH_ALEN];
extern volatile uint8_t remote_modifier_mask;

#endif // ESP_NOW_COMMON_H
