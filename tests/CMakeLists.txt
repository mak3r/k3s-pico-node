# CMakeLists.txt for k3s-pico-node unit tests
# These tests run on the host machine (x86/ARM64), not on Pico hardware

cmake_minimum_required(VERSION 3.13)

project(k3s_pico_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable testing
enable_testing()

# Include directories from main project
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Add include path for source files we're testing
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src)

# Test: HTTP Client
add_executable(test_http_client
    test_http_client.c
    ../src/http_client.c
)

# Test: Node Status
add_executable(test_node_status
    test_node_status.c
)

# Test: Time Sync
add_executable(test_time_sync
    test_time_sync.c
)

# Test: Node Status with Timestamps
add_executable(test_node_status_timestamps
    test_node_status_timestamps.c
)

# Add tests to CTest
add_test(NAME HttpClient COMMAND test_http_client)
add_test(NAME NodeStatus COMMAND test_node_status)
add_test(NAME TimeSync COMMAND test_time_sync)
add_test(NAME NodeStatusTimestamps COMMAND test_node_status_timestamps)

# Optional: Add compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_http_client PRIVATE -Wall -Wextra)
    target_compile_options(test_node_status PRIVATE -Wall -Wextra)
    target_compile_options(test_time_sync PRIVATE -Wall -Wextra)
    target_compile_options(test_node_status_timestamps PRIVATE -Wall -Wextra)
endif()

# Print test information
message(STATUS "")
message(STATUS "=== k3s-pico-node Tests ===")
message(STATUS "Unit tests will run on host machine")
message(STATUS "Integration tests require k3s cluster and kubectl")
message(STATUS "")
message(STATUS "To build and run tests:")
message(STATUS "  mkdir -p build && cd build")
message(STATUS "  cmake ..")
message(STATUS "  make")
message(STATUS "  ctest --verbose")
message(STATUS "")
message(STATUS "Or run individual tests:")
message(STATUS "  ./test_http_client")
message(STATUS "  ./test_node_status")
message(STATUS "  ./test_time_sync")
message(STATUS "  ./test_node_status_timestamps")
message(STATUS "")
message(STATUS "Integration tests:")
message(STATUS "  cd ../tests && ./test_node_timestamps.sh")
message(STATUS "===========================")
message(STATUS "")
