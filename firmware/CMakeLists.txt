cmake_minimum_required(VERSION 3.22)

# environment variable requirements
if(NOT DEFINED ENV{SOLARNODE_WIFI_SSID})
    message(FATAL_ERROR "SOLARNODE_WIFI_SSID must be set")
endif()
if(NOT DEFINED ENV{SOLARNODE_WIFI_PASS})
    message(FATAL_ERROR "SOLARNODE_WIFI_PASS must be set")
endif()
if(NOT DEFINED ENV{SOLARNODE_SERVER_DATA_URL})
    message(FATAL_ERROR "SOLARNODE_SERVER_DATA_URL must be set")
endif()

add_compile_definitions(${COMPONENT_LIB} PRIVATE
    SOLARNODE_WIFI_SSID="$ENV{SOLARNODE_WIFI_SSID}"
    SOLARNODE_WIFI_PASS="$ENV{SOLARNODE_WIFI_PASS}"
    SOLARNODE_SERVER_DATA_URL="$ENV{SOLARNODE_SERVER_DATA_URL}"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(firmware)
