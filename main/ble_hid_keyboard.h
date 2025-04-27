#ifndef BLE_HID_KEYBOARD_H
#define BLE_HID_KEYBOARD_H

#include <stdint.h>

// Define the Report ID for the keyboard
#define KEYBOARD_ID 1

namespace BleHidKeyboard {

    /**
     * @brief Initializes the BLE HID Keyboard service.
     *        Must be called once after NVS initialization and before sending reports.
     *
     * @param deviceName The name the keyboard will advertise as.
     * @return true on success, false on failure.
     */
    bool init(const char* deviceName = "Thumbpad Keyboard");

    /**
     * @brief Sends a keyboard report over BLE.
     *
     * @param modifier Modifier keys state (bitmask: bit 0: L CTRL, 1: L SHIFT, 2: L ALT, 3: L GUI, 4: R CTRL, 5: R SHIFT, 6: R ALT, 7: R GUI)
     * @param keycodes Array of up to 6 pressed keycodes (USB HID Usage IDs). Use 0 for unused slots.
     * @param numKeys Number of valid keycodes in the array (0-6).
     */
    void sendReport(uint8_t modifier, const uint8_t keycodes[6]);

    /**
     * @brief Sends a key press event (simplified version of sendReport).
     *        Assumes only one key is pressed at a time without modifiers for now.
     *
     * @param keycode The USB HID Usage ID of the key to press.
     */
    void sendKeyPress(uint8_t keycode);

    /**
     * @brief Sends a key release event (sends an empty report).
     */
    void sendKeyRelease();

    /**
     * @brief Checks if a BLE host is currently connected.
     *
     * @return true if connected, false otherwise.
     */
    bool isConnected();

    // You could add a deinit() function here if needed

} // namespace BleHidKeyboard

#endif // BLE_HID_KEYBOARD_H
