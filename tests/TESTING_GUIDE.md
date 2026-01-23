# Testing Guide for k3s-pico-node

Complete guide for testing the Raspberry Pi Pico as a Kubernetes node.

## Quick Start

```bash
# Run all unit tests (no hardware required)
cd tests
./run_all_tests.sh

# Run all tests including integration (requires k8s cluster)
./run_all_tests.sh --all

# Run hardware tests (requires Pico connected to network)
./run_all_tests.sh --all --pico-ip 192.168.1.100
```

## Test Categories

### 1. Unit Tests (Host Machine)

Tests C functions in isolation without requiring Pico hardware or k8s cluster.

**Build and run:**
```bash
cd tests
mkdir -p build && cd build
cmake ..
make
./test_http_client
./test_node_status
```

**What's tested:**
- HTTP request building (GET, POST, PATCH)
- HTTP response parsing
- Header extraction
- Node status JSON generation
- Buffer overflow protection
- Edge cases and error handling

**Requirements:**
- gcc or clang
- cmake
- No network or hardware needed

---

### 2. Integration Tests (Kubernetes)

Tests that validate communication with real k8s cluster.

**Run:**
```bash
cd tests
./test_node_registration.sh
./test_configmap_polling.sh
```

**What's tested:**
- Node registration in cluster
- Node appears in `kubectl get nodes`
- Node reports as Ready
- Node conditions are correct
- Node capacity/allocatable resources
- ConfigMap creation and updates
- API endpoint formatting
- Resource version tracking

**Requirements:**
- kubectl installed and configured
- k3s cluster running and accessible
- jq (JSON parsing)
- Pico registered with cluster (or simulated)

---

### 3. Hardware Tests (Actual Pico)

Tests that run against actual Pico hardware connected to network.

**Run:**
```bash
cd tests
export PICO_IP=192.168.1.100
./test_kubelet_endpoints.sh
```

**What's tested:**
- Kubelet port connectivity
- `/healthz` endpoint
- `/metrics` endpoint
- `/pods` endpoint behavior
- HTTP protocol compliance
- Response times
- Concurrent connection handling
- Unknown endpoint handling (404s)

**Requirements:**
- Pico W connected to network
- Pico firmware flashed and running
- Pico IP address accessible from test machine
- curl, jq, timeout commands

---

## Running Tests

### Option 1: Run Everything
```bash
cd /home/projects/k3s-device/k3s-pico-node/tests
./run_all_tests.sh --all --pico-ip 192.168.1.100
```

### Option 2: Run by Category
```bash
# Unit tests only (fast, no dependencies)
./run_all_tests.sh --unit-only

# Integration tests only (requires k8s)
./run_all_tests.sh --integration-only

# Hardware tests only (requires Pico)
./run_all_tests.sh --hardware-only --pico-ip 192.168.1.100
```

### Option 3: Run Individual Test Scripts
```bash
# Node registration
./test_node_registration.sh

# ConfigMap polling
./test_configmap_polling.sh

# Kubelet endpoints
PICO_IP=192.168.1.100 ./test_kubelet_endpoints.sh
```

---

## Test Results

Tests output results in color:
- ✓ **Green**: Test passed
- ✗ **Red**: Test failed
- ⊘ **Yellow**: Test skipped (missing dependencies or optional)
- ℹ **Blue**: Informational message

Example output:
```
=========================================
TEST: Node Ready Status
=========================================
  ✓ PASS: Node is in Ready state
  ℹ Last heartbeat: 2025-01-23T10:30:00Z
```

---

## Interpreting Results

### All Tests Pass ✓
Your Pico implementation meets all tested requirements:
- HTTP client works correctly
- Node registration successful
- Kubelet endpoints responding
- ConfigMap polling functional

### Some Tests Fail ✗

**Common failures and fixes:**

1. **Connection Timeout**
   - Check Pico is powered on
   - Verify Pico IP address
   - Check firewall allows port 6080 and 10250

2. **Node Not Found**
   - Check Pico serial output for errors
   - Verify nginx proxy is running: `kubectl get pod -n kube-system -l app=pico-proxy`
   - Check k3s server is accessible

3. **HTTP Parse Error**
   - May indicate buffer overflow
   - Check response size vs buffer size
   - Review HTTP client code

4. **ConfigMap Not Found**
   - Create ConfigMap: `kubectl create configmap pico-config --from-literal=test=value`
   - Check namespace is correct (default)

### Tests Skipped ⊘
Usually means:
- Missing dependencies (kubectl, jq, etc.)
- Optional functionality not implemented
- Hardware not available

---

## Continuous Testing

### During Development
```bash
# Quick unit test after code changes
cd tests/build
make && ./test_http_client
```

### Before Flashing to Pico
```bash
# Verify code changes don't break unit tests
./run_all_tests.sh --unit-only
```

### After Flashing to Pico
```bash
# Verify hardware behavior
PICO_IP=<pico-ip> ./test_kubelet_endpoints.sh

# Verify k8s integration
./test_node_registration.sh
```

### Regular Health Check
```bash
# Quick check that everything still works
./run_all_tests.sh --all --pico-ip <pico-ip>
```

---

## Debugging Failed Tests

### Enable Verbose Output
Most test scripts support verbose mode via environment variables:
```bash
DEBUG=1 ./test_node_registration.sh
```

### Check Pico Serial Output
```bash
screen /dev/ttyACM0 115200
```
Look for error messages during registration or API calls.

### Check k3s Logs
```bash
# API server logs
kubectl logs -n kube-system -l component=kube-apiserver

# Proxy logs
kubectl logs -n kube-system -l app=pico-proxy
```

### Manual API Testing
```bash
# Test node registration path
kubectl get node pico-node-1 -o yaml

# Test kubelet endpoint
curl http://<pico-ip>:10250/healthz

# Test ConfigMap
kubectl get configmap pico-config -o yaml
```

### Network Debugging
```bash
# Check connectivity
ping <pico-ip>

# Check port is open
nc -zv <pico-ip> 10250

# Check firewall
sudo firewall-cmd --list-all
```

---

## Test Coverage

### What's Tested ✅
- HTTP protocol implementation
- Node registration flow
- Status update mechanism
- Kubelet health endpoint
- Metrics endpoint
- ConfigMap retrieval
- JSON generation
- Error handling
- Concurrent connections

### What's Not Tested ❌
- TLS connection (using HTTP-only proxy)
- Pod lifecycle (no container runtime)
- Volume management (not implemented)
- Container runtime interface (not applicable)
- Long-running stability (requires extended test)
- Memory leak detection (requires profiling)
- WiFi reconnection (requires network simulation)

---

## Writing New Tests

### Unit Test Template
```c
// tests/test_myfeature.c
#include <stdio.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) \
    if (cond) { \
        printf("✓ %s\n", msg); \
        tests_passed++; \
    } else { \
        printf("✗ %s\n", msg); \
        tests_failed++; \
    }

void test_my_feature() {
    printf("\n[TEST] My Feature\n");
    // Test code here
    TEST_ASSERT(1 == 1, "Basic test");
}

int main() {
    test_my_feature();
    return tests_failed == 0 ? 0 : 1;
}
```

### Integration Test Template
```bash
#!/bin/bash
# tests/test_myfeature.sh
set -e

log_test() { echo "TEST: $1"; }
log_pass() { echo "✓ $1"; }
log_fail() { echo "✗ $1"; exit 1; }

log_test "My Feature"

# Test code here
if kubectl get nodes > /dev/null; then
    log_pass "Can access cluster"
else
    log_fail "Cannot access cluster"
fi
```

---

## CI/CD Integration

### GitHub Actions Example
```yaml
name: Tests
on: [push, pull_request]

jobs:
  unit-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build and run unit tests
        run: |
          cd tests
          mkdir build && cd build
          cmake ..
          make
          ctest --verbose

  integration-tests:
    runs-on: ubuntu-latest
    needs: unit-tests
    steps:
      - uses: actions/checkout@v2
      - name: Setup k3s
        run: |
          curl -sfL https://get.k3s.io | sh -
      - name: Run integration tests
        run: |
          cd tests
          ./test_node_registration.sh
```

---

## Performance Benchmarks

Track these metrics over time:

```bash
# Response time
time curl http://<pico-ip>:10250/healthz

# Memory usage (from serial output or metrics)
curl http://<pico-ip>:10250/metrics | grep node_memory_free

# Node registration time (from Pico serial logs)
# Look for "Node registered successfully" timestamp

# Heartbeat interval (from k8s)
kubectl get node pico-node-1 -o jsonpath='{.status.conditions[?(@.type=="Ready")].lastHeartbeatTime}' --watch
```

---

## Next Steps

After validating with tests:

1. **Fix any failing tests** before deploying to more Picos
2. **Implement improvements** from KUBELET_IMPROVEMENTS.md
3. **Add new tests** for new features
4. **Run tests regularly** to catch regressions
5. **Automate testing** in your workflow

---

## Support

For issues with tests:
1. Check test output for specific error messages
2. Review KUBELET_REQUIREMENTS.md for expectations
3. Check Pico serial output for runtime errors
4. Verify network connectivity and firewall rules
5. Test against local k3s cluster first

Happy testing! 🧪
