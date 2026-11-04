# Minimum supported CMake version
cmake_minimum_required(VERSION 3.20.0)

# Enable SSD1306 shield before loading Zephyr
set(SHIELD ssd1306_128x64)

# Load Zephyr build system and toolchain
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Application name
project(zephyr_threads_oled)

# Compile our application source file
target_sources(app PRIVATE src/main.c)