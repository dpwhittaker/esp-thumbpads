#include "ble_hid_keyboard.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h" // For vTaskDelay
#include "esp_log.h"
#include "nvs_flash.h" // Needed for NimBLE bonding

// NimBLE specific headers
#include "NimBLEDevice.h"
#include "NimBLEServer.h"
#include "NimBLEUtils.h"
#include "NimBLEHIDDevice.h"
#include "NimBLEConnInfo.h"
#include "HIDTypes.h"         // Standard HID definitions
#include "HIDKeyboardTypes.h" // Keyboard specific Usage IDs can be useful

static const char *TAG = "BLE_HID";

// --- Globals within this file ---
static NimBLEHIDDevice *pHid = nullptr;
static NimBLECharacteristic *pInputKeyboard = nullptr;
static bool _isConnected = false;
static std::string _deviceName = "Thumbpad"; // Default name

// --- HID Report Map ---
// Describes the keyboard to the host. This is a standard 101/102 key layout.
// Report ID is set to KEYBOARD_ID (defined in header)
static const uint8_t hidReportMap[] = {
    USAGE_PAGE(1), 0x01,       // Generic Desktop Ctrls
    USAGE(1), 0x06,            // Keyboard
    COLLECTION(1), 0x01,       // Application
    REPORT_ID(1), KEYBOARD_ID, // Report ID
    USAGE_PAGE(1), 0x07,       //   Kbrd/Keypad
    USAGE_MINIMUM(1), 0xE0,    //   Keyboard LeftControl
    USAGE_MAXIMUM(1), 0xE7,    //   Keyboard Right GUI
    LOGICAL_MINIMUM(1), 0x00,  //   Each bit is either 0 or 1
    LOGICAL_MAXIMUM(1), 0x01,
    REPORT_SIZE(1), 0x01,      //   1 byte (Modifier Keys)
    REPORT_COUNT(1), 0x08,
    HIDINPUT(1), 0x02,            //   Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position

    REPORT_COUNT(1), 0x01,     //   1 byte (Reserved)
    REPORT_SIZE(1), 0x08,
    HIDINPUT(1), 0x01,            //   Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position

    REPORT_COUNT(1), 0x06,     //   6 bytes (Keys)
    REPORT_SIZE(1), 0x08,
    LOGICAL_MINIMUM(1), 0x00,
    LOGICAL_MAXIMUM(1), 0xFF,  //   Allow any key code (0x00-0xFF defined by HID standard)
    USAGE_PAGE(1), 0x07,       //   Kbrd/Keypad
    USAGE_MINIMUM(1), 0x00,
    USAGE_MAXIMUM(1), 0xFF,    //   Use any key code index
    HIDINPUT(1), 0x00,            //   Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position

    // LEDs (Output Report - Optional but good practice)
    // REPORT_COUNT(1), 0x05,     //   5 bits (Num lock, Caps lock, Scroll lock, Compose, Kana)
    // REPORT_SIZE(1), 0x01,
    // USAGE_PAGE(1), 0x08,       //   LEDs
    // USAGE_MINIMUM(1), 0x01,    //   Num Lock
    // USAGE_MAXIMUM(1), 0x05,    //   Kana
    // OUTPUT(1), 0x02,          //   Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile
    // REPORT_COUNT(1), 0x01,     //   3 bits (Padding)
    // REPORT_SIZE(1), 0x03,
    // OUTPUT(1), 0x01,          //   Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile

    END_COLLECTION(0)
};

// --- BLE Server Callbacks ---
class ServerCallbacks : public NimBLEServerCallbacks {
    // Corrected signature using NimBLEConnInfo&
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo& connInfo) override {
        ESP_LOGI(TAG, "Client connected: %s", connInfo.getAddress().toString().c_str());
        _isConnected = true;
        vTaskDelay(pdMS_TO_TICKS(50));
        // Update connection parameters using connInfo.getConnHandle()
        // pServer->updateConnParams(connInfo.getConnHandle(), 8, 16, 0, 100);
    };

    // Corrected signature using NimBLEConnInfo& and adding reason
    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo& connInfo, int reason) override {
        ESP_LOGI(TAG, "Client disconnected: %s, reason=%d", connInfo.getAddress().toString().c_str(), reason);
        _isConnected = false;
        vTaskDelay(pdMS_TO_TICKS(100));
        NimBLEDevice::startAdvertising();
        ESP_LOGI(TAG, "Advertising restarted");
    };

    // Corrected signature using NimBLEConnInfo&
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
         ESP_LOGI(TAG, "Authentication Complete: %s", connInfo.getAddress().toString().c_str());
         // Check encryption status using connInfo methods
         if (!connInfo.isEncrypted()) {
             ESP_LOGW(TAG, "Link not encrypted!");
         } else {
             ESP_LOGI(TAG, "Link encrypted.");
         }
         vTaskDelay(pdMS_TO_TICKS(100)); // Add a delay AFTER authentication completes
        ESP_LOGI(TAG,"Post-authentication delay complete.");
    }
};

// --- Public Functions ---

bool BleHidKeyboard::init(const char* deviceName) {
    ESP_LOGI(TAG, "Initializing BLE HID Keyboard");
    _deviceName = deviceName;

    // Initialize NVS - NimBLE requires it for bonding information storage
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize NimBLE device
    NimBLEDevice::init(_deviceName);
    ESP_LOGI(TAG, "Device Name: %s", _deviceName.c_str());

    // Set security parameters for HID
    // Bonding = Store keys for reconnection
    // MITM = Man-In-The-Middle protection (requires pairing)
    // Secure Connection = Use LE Secure Connections pairing
    NimBLEDevice::setSecurityAuth(true, false, true);
    // Set IO capabilities - No Input/Output means "Just Works" pairing
    // If you had a display/button for passkey entry, you'd change this.
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    // Create the BLE Server
    NimBLEServer *pServer = NimBLEDevice::createServer();
    if (!pServer) {
        ESP_LOGE(TAG, "Failed to create BLE Server");
        return false;
    }
    pServer->setCallbacks(new ServerCallbacks()); // Set connection callbacks

    // Create the HID Device instance
    pHid = new NimBLEHIDDevice(pServer);
    if (!pHid) {
        ESP_LOGE(TAG, "Failed to create HID Device");
        return false; // Clean up server? NimBLE might handle this on deinit
    }

    // Get the input report characteristic handle (for sending key presses)
    // The ID must match the REPORT_ID in the report map
    pInputKeyboard = pHid->getInputReport(KEYBOARD_ID);
    if (!pInputKeyboard) {
        ESP_LOGE(TAG, "Failed to create Input Report characteristic");
        return false;
    }

    // Set HID Information
    // HID_INFO_FLAG_NORMALLY_CONNECTABLE = bit 0
    // HID_INFO_FLAG_REMOTE_WAKE = bit 1
    uint8_t hidInfoFlags = (1 << 0) | (1 << 1); // 0x03
    pHid->setHidInfo(0x00, hidInfoFlags);       // Use setHidInfo


    // Set the Report Map
    pHid->setReportMap((uint8_t *)hidReportMap, sizeof(hidReportMap));

    // Set Manufacturer Name (optional)
    pHid->setManufacturer("Espressif"); // Or your name
    // Set PnP Information (optional) - Vendor ID, Product ID, Version
    pHid->setPnp(0x02,    // Vendor ID Source: USB Implementers Forum
              0x1209,  // Vendor ID: pid.codes VID (example)
              0x0001,  // Product ID (example)
              0x0100); // Product Version (example: 1.00)

    // Start the required services (HID, Battery, Device Information)
    pHid->startServices();

    // Configure Advertising
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    if (!pAdvertising) {
         ESP_LOGE(TAG, "Failed to get Advertising object");
         return false;
    }
    pAdvertising->setAppearance(HID_KEYBOARD); // Set appearance to Keyboard
    pAdvertising->addServiceUUID(pHid->getHidService()->getUUID()); // Advertise HID service
    pAdvertising->setName(_deviceName);
    pAdvertising->addTxPower();
    pAdvertising->enableScanResponse(true);
    pAdvertising->setPreferredParams(0x06, 0x12);

    // Start advertising
    pAdvertising->start();
    ESP_LOGI(TAG, "Advertising started. Waiting for connection...");
    // Optional: Set power level after starting advertising
    // NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Example: Set lowest power

    return true;
}

bool BleHidKeyboard::isConnected() {
    return _isConnected;
}

void BleHidKeyboard::sendReport(uint8_t modifier, const uint8_t keycodes[6]) {
    if (!_isConnected) {
        // ESP_LOGW(TAG, "Cannot send report: Not connected");
        return;
    }
    if (!pInputKeyboard) {
         ESP_LOGE(TAG, "Cannot send report: Input characteristic is null");
         return;
    }

    // Report structure:
    // byte 0: Report ID (must match the one in the report map)
    // byte 1: Modifier keys bitmask
    // byte 2: Reserved (must be 0)
    // byte 3-8: Keycodes (up to 6 simultaneous keys)
    uint8_t report[9];
    report[0] = KEYBOARD_ID;
    report[1] = modifier;
    report[2] = 0x00; // Reserved
    memcpy(&report[3], keycodes, 6);

    // Send the report
    pInputKeyboard->setValue(report, sizeof(report));
    pInputKeyboard->notify();

    // ESP_LOGD(TAG, "Sent Report: Mod=0x%02X Keys=[%02X %02X %02X %02X %02X %02X]",
    //          modifier, keycodes[0], keycodes[1], keycodes[2], keycodes[3], keycodes[4], keycodes[5]);
}

// Simplified: Send single key press (no modifiers yet)
void BleHidKeyboard::sendKeyPress(uint8_t keycode) {
    if (keycode == 0) return; // Don't send press for 'no key'

    uint8_t keycodes[6] = {0};
    keycodes[0] = keycode; // Place the key in the first slot
    sendReport(0x00, keycodes); // No modifiers for now
}

// Simplified: Send key release (empty report)
void BleHidKeyboard::sendKeyRelease() {
    uint8_t keycodes[6] = {0};
    sendReport(0x00, keycodes); // No modifiers, no keys
}

