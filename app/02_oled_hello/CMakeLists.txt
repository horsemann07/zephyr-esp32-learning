# ============================================================================
# File        : CMakeLists.txt
# Description : Zephyr OLED Hello Example
# ============================================================================

cmake_minimum_required(VERSION 3.20.0)

# Enable the SSD1306 128x64 OLED shield.
set(SHIELD ssd1306_128x64)

# Locate and initialize Zephyr.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Application project name.
project(oled_hello)

# Application source files.
target_sources(app PRIVATE src/main.c)