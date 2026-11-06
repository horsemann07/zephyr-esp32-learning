cmake_minimum_required(VERSION 3.20.0)

# Matches the OLED shield already used by our Heltec Zephyr examples.
set(SHIELD ssd1306_128x64)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ridecast_heltec_demo)

set(RC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_include_directories(app PRIVATE
    "${RC_ROOT}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_sources(app PRIVATE src/main.c src/heltec_graphic.c src/rc_err_sts.c)

if(CONFIG_RC_HELTEC_WIFI_DEMO)
    set(RC_CONNECTION_DIR "${RC_ROOT}/services/connection")
    target_include_directories(app PRIVATE "${RC_CONNECTION_DIR}")
    target_sources(app PRIVATE
        "${RC_CONNECTION_DIR}/rc_connection_wifi.c"
        "${RC_CONNECTION_DIR}/rc_connection_tcp.c"
    )
endif()
