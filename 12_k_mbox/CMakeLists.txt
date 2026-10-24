cmake_minimum_required(VERSION 3.20.0)

# Flash/debug through the ST-Link with OpenOCD (same as labs/01-blinky).
set(BOARD_FLASH_RUNNER openocd CACHE STRING "")
set(BOARD_DEBUG_RUNNER openocd CACHE STRING "")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lab12_k_mbox)

target_sources(app PRIVATE src/main2.c)
