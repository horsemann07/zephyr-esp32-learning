cmake_minimum_required(VERSION 3.20.0)
set(SHIELD ssd1306_128x64)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ridecast_08)

target_include_directories(app PRIVATE src)
target_sources(app PRIVATE
    src/main.c
    src/heltec_graphic.c
    src/rc_demo_manager.c
    src/rc_demo_store.c
)
if(CONFIG_RC_ENABLE_WIFI)
    target_sources(app PRIVATE
        src/rc_connection_wifi.c
        src/rc_connection_tcp.c
        src/rc_demo_wifi.c
    )
endif()
if(CONFIG_RC_ENABLE_BLE)
    target_sources(app PRIVATE src/rc_demo_ble.c)
endif()
