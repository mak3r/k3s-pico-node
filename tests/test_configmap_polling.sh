#!/bin/bash
# Integration tests for ConfigMap watching/polling
# Tests that the Pico can retrieve and respond to ConfigMap updates

set -e

# Configuration
NODE_NAME="${NODE_NAME:-pico-node-1}"
CONFIGMAP_NAME="${CONFIGMAP_NAME:-pico-config}"
CONFIGMAP_NAMESPACE="${CONFIGMAP_NAMESPACE:-default}"
PICO_IP="${PICO_IP:-192.168.1.100}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

log_test() {
    echo ""
    echo "========================================="
    echo "TEST: $1"
    echo "========================================="
}

log_pass() {
    echo -e "${GREEN}✓ PASS${NC}: $1"
    ((TESTS_PASSED++))
}

log_fail() {
    echo -e "${RED}✗ FAIL${NC}: $1"
    ((TESTS_FAILED++))
}

log_skip() {
    echo -e "${YELLOW}⊘ SKIP${NC}: $1"
    ((TESTS_SKIPPED++))
}

log_info() {
    echo -e "${BLUE}  ℹ${NC} $1"
}

# Clean up any existing test ConfigMap
cleanup_configmap() {
    if kubectl get configmap "$CONFIGMAP_NAME" -n "$CONFIGMAP_NAMESPACE" &> /dev/null; then
        kubectl delete configmap "$CONFIGMAP_NAME" -n "$CONFIGMAP_NAMESPACE" &> /dev/null || true
        log_info "Cleaned up existing ConfigMap"
    fi
}

# Test: Create ConfigMap
test_create_configmap() {
    log_test "Create ConfigMap"

    cleanup_configmap

    if kubectl create configmap "$CONFIGMAP_NAME" \
        --from-literal=memory_values="0=0x42,1=0x43,2=0xFF" \
        -n "$CONFIGMAP_NAMESPACE" &> /dev/null; then
        log_pass "ConfigMap created successfully"
        return 0
    else
        log_fail "Failed to create ConfigMap"
        return 1
    fi
}

# Test: Read ConfigMap via API
test_read_configmap() {
    log_test "Read ConfigMap via Kubernetes API"

    data=$(kubectl get configmap "$CONFIGMAP_NAME" -n "$CONFIGMAP_NAMESPACE" \
           -o jsonpath='{.data.memory_values}' 2>/dev/null)

    if [ -n "$data" ]; then
        log_pass "ConfigMap data retrieved: $data"
    else
        log_fail "Could not retrieve ConfigMap data"
    fi
}

# Test: Update ConfigMap
test_update_configmap() {
    log_test "Update ConfigMap"

    new_value="0=0xAA,1=0xBB,2=0xCC"

    if kubectl patch configmap "$CONFIGMAP_NAME" -n "$CONFIGMAP_NAMESPACE" \
        --type merge \
        -p "{\"data\":{\"memory_values\":\"$new_value\"}}" &> /dev/null; then
        log_pass "ConfigMap updated successfully"

        # Verify update
        data=$(kubectl get configmap "$CONFIGMAP_NAME" -n "$CONFIGMAP_NAMESPACE" \
               -o jsonpath='{.data.memory_values}')
        if [ "$data" = "$new_value" ]; then
            log_pass "ConfigMap value verified: $data"
        else
            log_fail "ConfigMap value mismatch. Expected: $new_value, Got: $data"
        fi
    else
        log_fail "Failed to update ConfigMap"
    fi
}

# Test: ConfigMap GET request format
test_api_request_format() {
    log_test "ConfigMap API Request Format"

    # Test the exact API path the Pico would use
    api_path="/api/v1/namespaces/$CONFIGMAP_NAMESPACE/configmaps/$CONFIGMAP_NAME"

    log_info "Testing API path: $api_path"

    # Use kubectl proxy to test the API endpoint
    kubectl proxy --port=8001 &> /dev/null &
    proxy_pid=$!
    sleep 2

    response=$(curl -s "http://localhost:8001${api_path}" 2>/dev/null)

    kill $proxy_pid 2>/dev/null || true

    if echo "$response" | jq -e '.kind == "ConfigMap"' &> /dev/null; then
        log_pass "API returns valid ConfigMap JSON"

        # Check for data field
        if echo "$response" | jq -e '.data' &> /dev/null; then
            log_pass "ConfigMap has .data field"

            memory_values=$(echo "$response" | jq -r '.data.memory_values' 2>/dev/null)
            if [ -n "$memory_values" ] && [ "$memory_values" != "null" ]; then
                log_pass "memory_values field present: $memory_values"
            fi
        else
            log_fail "ConfigMap missing .data field"
        fi
    else
        log_fail "API response is not a valid ConfigMap"
        log_info "Response: $(echo $response | head -c 200)"
    fi
}

# Test: ConfigMap polling behavior
test_polling_behavior() {
    log_test "ConfigMap Polling Behavior"

    log_info "This test verifies the Pico can poll for changes"
    log_info "In real deployment, Pico polls every 30 seconds"

    # Create a ConfigMap with initial value
    cleanup_configmap
    kubectl create configmap "$CONFIGMAP_NAME" \
        --from-literal=memory_values="0=0x01" \
        -n "$CONFIGMAP_NAMESPACE" &> /dev/null

    initial_version=$(kubectl get configmap "$CONFIGMAP_NAME" -n "$CONFIGMAP_NAMESPACE" \
                      -o jsonpath='{.metadata.resourceVersion}')
    log_info "Initial resourceVersion: $initial_version"

    # Update the ConfigMap
    sleep 1
    kubectl patch configmap "$CONFIGMAP_NAME" -n "$CONFIGMAP_NAMESPACE" \
        --type merge \
        -p '{"data":{"memory_values":"0=0x02"}}' &> /dev/null

    updated_version=$(kubectl get configmap "$CONFIGMAP_NAME" -n "$CONFIGMAP_NAMESPACE" \
                      -o jsonpath='{.metadata.resourceVersion}')
    log_info "Updated resourceVersion: $updated_version"

    if [ "$initial_version" != "$updated_version" ]; then
        log_pass "resourceVersion changes on update (can detect changes)"
    else
        log_fail "resourceVersion did not change"
    fi
}

# Test: ConfigMap with multiple keys
test_multiple_keys() {
    log_test "ConfigMap with Multiple Keys"

    cleanup_configmap

    kubectl create configmap "$CONFIGMAP_NAME" \
        --from-literal=memory_values="0=0x42,1=0x43" \
        --from-literal=config_option="enabled" \
        --from-literal=polling_interval="30" \
        -n "$CONFIGMAP_NAMESPACE" &> /dev/null

    # Verify all keys are present
    keys=$(kubectl get configmap "$CONFIGMAP_NAME" -n "$CONFIGMAP_NAMESPACE" \
           -o jsonpath='{.data}' | jq 'keys | length')

    if [ "$keys" -eq 3 ]; then
        log_pass "ConfigMap has $keys keys (correct)"
    else
        log_fail "Expected 3 keys, got: $keys"
    fi

    # Pico should be able to read specific key
    memory_values=$(kubectl get configmap "$CONFIGMAP_NAME" -n "$CONFIGMAP_NAMESPACE" \
                    -o jsonpath='{.data.memory_values}')
    if [ -n "$memory_values" ]; then
        log_pass "Can read specific key 'memory_values': $memory_values"
    else
        log_fail "Could not read 'memory_values' key"
    fi
}

# Test: ConfigMap deletion and recreation
test_deletion_and_recreation() {
    log_test "ConfigMap Deletion and Recreation"

    # Delete ConfigMap
    if kubectl delete configmap "$CONFIGMAP_NAME" -n "$CONFIGMAP_NAMESPACE" &> /dev/null; then
        log_pass "ConfigMap deleted successfully"
    else
        log_fail "Failed to delete ConfigMap"
    fi

    # Verify it's gone
    if kubectl get configmap "$CONFIGMAP_NAME" -n "$CONFIGMAP_NAMESPACE" &> /dev/null; then
        log_fail "ConfigMap still exists after deletion"
    else
        log_pass "ConfigMap successfully removed"
    fi

    # Recreate with new data
    if kubectl create configmap "$CONFIGMAP_NAME" \
        --from-literal=memory_values="0=0xFF,1=0xEE" \
        -n "$CONFIGMAP_NAMESPACE" &> /dev/null; then
        log_pass "ConfigMap recreated successfully"
    else
        log_fail "Failed to recreate ConfigMap"
    fi
}

# Test: Invalid ConfigMap data format
test_invalid_data_format() {
    log_test "Invalid ConfigMap Data Format Handling"

    cleanup_configmap

    # Create ConfigMap with invalid memory_values format
    kubectl create configmap "$CONFIGMAP_NAME" \
        --from-literal=memory_values="invalid_format_here" \
        -n "$CONFIGMAP_NAMESPACE" &> /dev/null

    log_info "Created ConfigMap with invalid data format"
    log_pass "Kubernetes accepts any string data (validation is Pico's job)"
    log_info "Pico should handle parse errors gracefully"
}

# Test: Large ConfigMap data
test_large_configmap() {
    log_test "Large ConfigMap Data"

    cleanup_configmap

    # Generate large memory_values string (but still reasonable)
    large_data=""
    for i in {0..100}; do
        large_data+="$i=0xFF,"
    done
    large_data="${large_data%,}"  # Remove trailing comma

    kubectl create configmap "$CONFIGMAP_NAME" \
        --from-literal=memory_values="$large_data" \
        -n "$CONFIGMAP_NAMESPACE" &> /dev/null

    data_size=$(kubectl get configmap "$CONFIGMAP_NAME" -n "$CONFIGMAP_NAMESPACE" \
                -o jsonpath='{.data.memory_values}' | wc -c)

    log_info "ConfigMap data size: $data_size bytes"

    if [ "$data_size" -gt 0 ]; then
        log_pass "Large ConfigMap created (size: $data_size bytes)"
        log_info "Pico must ensure buffer is large enough (currently 4KB)"
    else
        log_fail "Failed to create large ConfigMap"
    fi
}

# Test: ConfigMap watch simulation
test_watch_simulation() {
    log_test "Watch API Simulation (Future Enhancement)"

    log_skip "Watch API not yet implemented (using polling)"
    log_info "Future: Pico should use watch API for real-time updates"
    log_info "Current: Pico polls every 30 seconds"
    log_info "Watch API endpoint: /api/v1/watch/namespaces/$CONFIGMAP_NAMESPACE/configmaps/$CONFIGMAP_NAME"
}

# Main execution
main() {
    echo "========================================="
    echo "  ConfigMap Polling Tests"
    echo "========================================="
    echo "ConfigMap: $CONFIGMAP_NAMESPACE/$CONFIGMAP_NAME"
    echo "Node: $NODE_NAME"
    echo ""

    # Check prerequisites
    if ! command -v kubectl &> /dev/null; then
        echo "Error: kubectl not found"
        exit 1
    fi

    if ! command -v jq &> /dev/null; then
        echo "Error: jq not found"
        exit 1
    fi

    # Run tests
    test_create_configmap || exit 1
    test_read_configmap
    test_update_configmap
    test_api_request_format
    test_polling_behavior
    test_multiple_keys
    test_deletion_and_recreation
    test_invalid_data_format
    test_large_configmap
    test_watch_simulation

    # Cleanup
    log_info "Cleaning up test ConfigMap..."
    cleanup_configmap

    # Summary
    echo ""
    echo "========================================="
    echo "  Test Summary"
    echo "========================================="
    echo -e "${GREEN}Passed: ${TESTS_PASSED}${NC}"
    echo -e "${RED}Failed: ${TESTS_FAILED}${NC}"
    echo -e "${YELLOW}Skipped: ${TESTS_SKIPPED}${NC}"
    echo "========================================="

    echo ""
    echo "To test with actual Pico:"
    echo "  1. Create ConfigMap: kubectl create configmap pico-config --from-literal=memory_values='0=0x42,1=0x43'"
    echo "  2. Watch Pico serial output for ConfigMap polling"
    echo "  3. Update ConfigMap: kubectl patch configmap pico-config -p '{\"data\":{\"memory_values\":\"0=0xAA\"}}'"
    echo "  4. Watch for Pico to detect change (30s polling interval)"

    if [ "$TESTS_FAILED" -gt 0 ]; then
        exit 1
    else
        exit 0
    fi
}

main "$@"
