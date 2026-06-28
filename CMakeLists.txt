cmake_minimum_required(VERSION 3.16)

# esp_wifi_remote 1.5.x uses this value to select its ESP-IDF 5.5 Kconfig set.
set(ENV{ESP_IDF_VERSION} "5.5")

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/third_party/esp-dl/esp-dl"
    "${CMAKE_CURRENT_LIST_DIR}/third_party/esp-dl/dl_fft")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESP32P4-IDF_ST7701-MIPI-1LANE_480X640_TOUCH-CST826_LVGL)

set(yolo_model_file "${CMAKE_CURRENT_LIST_DIR}/models/food_yolo.espdl")
esptool_py_flash_to_partition(flash "yolo_model" "${yolo_model_file}")
