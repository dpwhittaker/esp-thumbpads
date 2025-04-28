set(LITTLEFS_FS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fs)

idf_component_register(
    SRCS "thumbpads.cpp"
         "hid/esp_hidd_prf_api.c"
         "hid/hid_dev.c"
         "hid/hid_device_le_prf.c"
    INCLUDE_DIRS "." "hid"
    REQUIRES bt nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable)

littlefs_create_partition_image(storage ${LITTLEFS_FS_DIR} FLASH_IN_PROJECT)
