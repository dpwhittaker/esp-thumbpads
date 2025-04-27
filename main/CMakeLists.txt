set(LITTLEFS_FS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fs)

idf_component_register(
    SRCS "thumbpads.cpp"
         "ble_hid_keyboard.cpp"
    INCLUDE_DIRS "."
)

littlefs_create_partition_image(storage ${LITTLEFS_FS_DIR} FLASH_IN_PROJECT)
