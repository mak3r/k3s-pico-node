cmake_minimum_required(VERSION 3.13)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

# Set board type to Pico W/WH
set(PICO_BOARD pico_w)

# Project name and version
project(pico_blink_test C CXX ASM)
set(PROJECT_VERSION "1.0.0")

# Set C/C++ standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

# Create the test executable
add_executable(pico_blink_test
    src/blink_test.c
)

# Include current directory for lwipopts.h
target_include_directories(pico_blink_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link against minimal pico libraries
target_link_libraries(pico_blink_test
    pico_stdlib               # Standard library (GPIO, timing, USB)
    pico_cyw43_arch_lwip_poll # WiFi chip driver with lwIP (for LED control)
)

# Enable USB output for debugging
pico_enable_stdio_usb(pico_blink_test 1)
pico_enable_stdio_uart(pico_blink_test 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(pico_blink_test)

# Print build information
message(STATUS "========================================")
message(STATUS "Pico WH Blink Test Configuration")
message(STATUS "========================================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Board: ${PICO_BOARD}")
message(STATUS "SDK Path: ${PICO_SDK_PATH}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "========================================")
