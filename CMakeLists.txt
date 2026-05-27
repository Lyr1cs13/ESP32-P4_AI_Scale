# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/esp-dl/esp-dl"
    "${CMAKE_CURRENT_LIST_DIR}/esp-dl/tools/dl_fft")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)


project(simple_video_server)
# ========== 添加模型烧录配置 ==========
set(model_file "C:/Dpan/Quantization/best160.espdl")
esptool_py_flash_to_partition(flash "model" "${model_file}")
# ===================================
