cmake_minimum_required(VERSION 3.20.0)

set(SHIELD ssd1306_128x64)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(emoji_notification_center)

target_sources(app PRIVATE
    src/main.c
    src/input_thread.c
    src/notification_thread.c
    src/display_thread.c
    src/emoji_draw.c
)