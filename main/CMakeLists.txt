set(LITTLEFS_FS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fs)

idf_component_register(
    SRCS "keyboard_layout.cpp" "thumbpads.cpp" "hid_keycodes.cpp" "thumbpads.cpp"
         "hid/esp_hidd_prf_api.c"
         "hid/hid_dev.c"
         "hid/hid_device_le_prf.c"
    INCLUDE_DIRS "." "hid"
    REQUIRES lvgl esp_lcd_touch esp_lcd_touch_ft5x06 esp_lcd_sh8601 esp_io_expander littlefs nvs_flash esp_wifi bt
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable)

littlefs_create_partition_image(storage ${LITTLEFS_FS_DIR})
