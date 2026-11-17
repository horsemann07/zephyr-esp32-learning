
cmake_minimum_required(VERSION 3.20.0)

set(SHIELD ssd1306_128x64)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ridecast_08)

# =========================================================
# Include directories
# =========================================================

target_include_directories(app PRIVATE
    src
    sim
)

# =========================================================
# Common RideCast firmware
# =========================================================

target_sources(app PRIVATE
    src/main.c
    src/heltec_graphic.c
    src/rc_ui_text.c
    src/rc_demo_manager.c
    src/rc_demo_store.c
    src/rc_demo_nav.c
    src/rc_demo_content.c
    src/rc_app_message.c
    src/rc_h7_bridge.c
    src/rc_ble_framing.c
)

# =========================================================
# Simulator configuration from C header
# =========================================================

set(RC_SIM_HEADER
    "${CMAKE_CURRENT_SOURCE_DIR}/sim/rc_app_sim_config.h"
)

set_property(DIRECTORY APPEND PROPERTY
    CMAKE_CONFIGURE_DEPENDS "${RC_SIM_HEADER}"
)

file(STRINGS "${RC_SIM_HEADER}" RC_SIM_LINES
    REGEX "^#define RC_APP_SIM_MODE [012]$"
)

list(LENGTH RC_SIM_LINES RC_SIM_COUNT)

if(NOT RC_SIM_COUNT EQUAL 1)
    message(FATAL_ERROR
        "Define RC_APP_SIM_MODE as 0, 1 or 2 in sim/rc_app_sim_config.h"
    )
endif()

string(REGEX MATCH "RC_APP_SIM_MODE ([012])"
    RC_SIM_MATCH "${RC_SIM_LINES}"
)

set(RC_APP_SIM_MODE "${CMAKE_MATCH_1}")

message(STATUS "RideCast Simulator Mode: ${RC_APP_SIM_MODE}")

# =========================================================
# Radio implementations
# =========================================================

if(RC_APP_SIM_MODE STREQUAL "1")

    # Standalone simulation - no physical radios
    target_sources(app PRIVATE
        sim/rc_app_sim_radio_stubs.c
    )

else()

    # Normal or live simulation - real Wi-Fi
    if(CONFIG_RC_ENABLE_WIFI)
        target_sources(app PRIVATE
            src/rc_connection_ap.c
            src/rc_connection_tcp.c
            src/rc_demo_wifi.c
        )
    endif()

    # Normal or live simulation - real BLE
    if(CONFIG_RC_ENABLE_BLE)
        target_sources(app PRIVATE
            src/rc_demo_ble.c
        )
    endif()

endif()

# =========================================================
# App integration simulator
# =========================================================

if(NOT RC_APP_SIM_MODE STREQUAL "0")
    target_sources(app PRIVATE
        sim/rc_app_simulator.c
        sim/rc_ble_frame_sim.c
    )
endif()

