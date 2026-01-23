# k3s-pico-node Test Suite

This directory contains tests for validating the Raspberry Pi Pico W firmware that allows it to operate as a Kubernetes node.

## Test Categories

### 1. Unit Tests
Tests for individual C functions in isolation.
- `test_http_client.c` - HTTP request/response building and parsing
- `test_node_status.c` - Node status JSON generation
- `test_tcp_connection.c` - TCP connection primitives

### 2. Integration Tests
Tests that validate communication with the actual k3s cluster.
- `test_proxy_connectivity.sh` - Verify nginx proxy is accessible
- `test_node_registration.sh` - Test node registration flow
- `test_configmap_polling.sh` - Test ConfigMap watching

### 3. Hardware Tests
Tests that run on actual Pico hardware.
- `hardware/test_wifi_connection.c` - WiFi connectivity
- `hardware/test_full_flow.c` - Complete registration flow

## Running Tests

### Unit Tests (x86/ARM64)
```bash
cd tests
mkdir -p build && cd build
cmake ..
make
./test_http_client
./test_node_status
```

### Integration Tests (requires k3s cluster)
```bash
cd tests
./test_proxy_connectivity.sh
./test_node_registration.sh
./test_configmap_polling.sh
```

### Hardware Tests (requires Pico W)
```bash
cd tests/hardware
./flash_and_monitor.sh test_wifi_connection
```

## Test Requirements

- **Unit tests**: gcc/clang, cmake
- **Integration tests**: kubectl, curl, jq, k3s cluster access
- **Hardware tests**: Raspberry Pi Pico W, USB connection

## Test Coverage Goals

- HTTP client: 90%+
- Node status generation: 100%
- K3s client functions: 80%+
- Error handling: 80%+
