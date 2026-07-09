# 递归搜索main目录下所有.c文件（包括ui子文件夹）
file(GLOB_RECURSE SOURCES_C 
    "*.c"          # main根目录的.c文件
    "ui/*.c"       # ui文件夹下的所有.c文件
    "ui/*/*.c"     # ui子文件夹（比如ui/screens）下的.c文件（可选，有就保留）
    "ui/*/*/*.c"
    "uart/*.c"
)

# 递归搜索main目录下所有.cpp文件（包括ui子文件夹）
file(GLOB_RECURSE SOURCES_CPP 
    "*.cpp"
    "ui/*.cpp"
    "ui/*/*.cpp"
    "ui/*/*/*.cpp"
    "uart/*.cpp"
)

# 确保关键文件被包含（双重保险）
list(APPEND SOURCES_C "utils.c")
list(APPEND SOURCES_C "wifi_udp/wifi.c")
list(APPEND SOURCES_C "wifi_udp/mqtt.c")
list(APPEND SOURCES_C "wifi_udp/llm_cloud.c")

# 注册ESP32组件，关键：INCLUDE_DIRS添加"ui"目录（解决头文件找不到）
idf_component_register(
    SRCS ${SOURCES_C} ${SOURCES_CPP}
    INCLUDE_DIRS "." "ui" "wifi_udp" # 必须加"ui"，否则#include "ui.h"会报错
)

# 保留你原来的spiffs注释（不需要可以删掉）
# spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)