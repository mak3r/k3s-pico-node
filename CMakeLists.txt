cmake_minimum_required(VERSION 3.13)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

# Set board type to Pico W/WH
set(PICO_BOARD pico_w)

# Project name
project(k3s_pico_node C CXX ASM)

# Set C/C++ standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

# Create the executable
add_executable(k3s_pico_node
    src/main.c
    src/kubelet_server.c
    src/k3s_client.c
    src/node_status.c
    src/configmap_watcher.c
    src/memory_manager.c
)

# Include directories
target_include_directories(k3s_pico_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link against pico libraries
target_link_libraries(k3s_pico_node
    pico_stdlib               # Standard library
    pico_cyw43_arch_lwip_poll # WiFi + lwIP in poll mode
    pico_lwip_mbedtls         # lwIP with mbedtls integration
    pico_mbedtls              # mbedtls crypto library
    pico_rand                 # Hardware random number generator
    hardware_flash            # Flash memory access
)

# Enable USB output for debugging
pico_enable_stdio_usb(k3s_pico_node 1)
pico_enable_stdio_uart(k3s_pico_node 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(k3s_pico_node)

# Compiler definitions
target_compile_definitions(k3s_pico_node PRIVATE
    # WiFi/lwIP configuration
    PICO_CYW43_ARCH_POLL=1
    CYW43_LWIP=1

    # lwIP configuration
    LWIP_IPV4=1
    LWIP_IPV6=0
    LWIP_TCP=1
    LWIP_UDP=1
    NO_SYS=1

    # mbedtls configuration file
    MBEDTLS_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/mbedtls_config.h"
)

# Compiler flags
target_compile_options(k3s_pico_node PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-unused-function
)

# Print build information
message(STATUS "========================================")
message(STATUS "K3s Pico Node Configuration")
message(STATUS "========================================")
message(STATUS "Board: ${PICO_BOARD}")
message(STATUS "SDK Path: ${PICO_SDK_PATH}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "========================================")
