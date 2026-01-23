#!/bin/bash
# Integration tests for kubelet HTTP endpoints
# Tests the actual Pico hardware or simulated kubelet server

set -e

# Configuration
PICO_IP="${PICO_IP:-192.168.1.100}"
KUBELET_PORT="${KUBELET_PORT:-10250}"
KUBELET_URL="http://${PICO_IP}:${KUBELET_PORT}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

# Helper functions
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
    echo "  ℹ $1"
}

# Check if kubelet is reachable
check_connectivity() {
    log_test "Kubelet Connectivity"

    if timeout 5 bash -c "echo > /dev/tcp/${PICO_IP}/${KUBELET_PORT}" 2>/dev/null; then
        log_pass "Kubelet port ${KUBELET_PORT} is reachable"
        return 0
    else
        log_fail "Cannot connect to ${KUBELET_URL}"
        log_info "Make sure Pico is powered on and connected to network"
        log_info "Check that PICO_IP is correct: ${PICO_IP}"
        return 1
    fi
}

# Test /healthz endpoint
test_healthz() {
    log_test "/healthz Endpoint"

    response=$(curl -s -w "\n%{http_code}" "${KUBELET_URL}/healthz" 2>/dev/null || echo "000")
    http_code=$(echo "$response" | tail -n 1)
    body=$(echo "$response" | head -n -1)

    if [ "$http_code" = "200" ]; then
        log_pass "HTTP 200 OK returned"

        if [ "$body" = "ok" ]; then
            log_pass "Response body is 'ok'"
        else
            log_fail "Response body should be 'ok', got: '$body'"
        fi
    else
        log_fail "Expected HTTP 200, got HTTP $http_code"
    fi

    # Check response headers
    headers=$(curl -s -I "${KUBELET_URL}/healthz" 2>/dev/null)
    if echo "$headers" | grep -q "Content-Type: text/plain"; then
        log_pass "Content-Type is text/plain"
    else
        log_fail "Content-Type header missing or incorrect"
    fi
}

# Test /metrics endpoint
test_metrics() {
    log_test "/metrics Endpoint"

    response=$(curl -s -w "\n%{http_code}" "${KUBELET_URL}/metrics" 2>/dev/null || echo "000")
    http_code=$(echo "$response" | tail -n 1)
    body=$(echo "$response" | head -n -1)

    if [ "$http_code" = "200" ]; then
        log_pass "HTTP 200 OK returned"
    else
        log_fail "Expected HTTP 200, got HTTP $http_code"
    fi

    # Check Content-Type header
    headers=$(curl -s -I "${KUBELET_URL}/metrics" 2>/dev/null)
    if echo "$headers" | grep -q "text/plain; version=0.0.4"; then
        log_pass "Content-Type is Prometheus format (text/plain; version=0.0.4)"
    else
        log_fail "Content-Type should be 'text/plain; version=0.0.4'"
        log_info "Got: $(echo "$headers" | grep Content-Type)"
    fi

    # Body can be empty for now, but should be valid Prometheus format
    if [ -z "$body" ]; then
        log_info "Metrics body is empty (acceptable for minimal implementation)"
    else
        log_info "Metrics returned ${#body} bytes"
    fi
}

# Test /pods endpoint
test_pods() {
    log_test "/pods Endpoint"

    response=$(curl -s -w "\n%{http_code}" "${KUBELET_URL}/pods" 2>/dev/null || echo "000")
    http_code=$(echo "$response" | tail -n 1)
    body=$(echo "$response" | head -n -1)

    if [ "$http_code" = "200" ]; then
        log_pass "HTTP 200 OK returned"

        # Should return JSON pod list (even if empty)
        if echo "$body" | jq -e '.kind == "PodList"' >/dev/null 2>&1; then
            log_pass "Response is valid PodList JSON"

            pod_count=$(echo "$body" | jq '.items | length')
            log_info "Pod count: $pod_count"
        else
            log_fail "Response should be PodList JSON format"
        fi
    elif [ "$http_code" = "404" ]; then
        log_skip "Endpoint not implemented (returns 404)"
        log_info "This is acceptable - Pico doesn't run pods"
    else
        log_fail "Unexpected HTTP code: $http_code"
    fi
}

# Test /spec endpoint
test_spec() {
    log_test "/spec Endpoint"

    response=$(curl -s -w "\n%{http_code}" "${KUBELET_URL}/spec" 2>/dev/null || echo "000")
    http_code=$(echo "$response" | tail -n 1)
    body=$(echo "$response" | head -n -1)

    if [ "$http_code" = "200" ]; then
        log_pass "HTTP 200 OK returned"

        # Should return JSON machine spec
        if echo "$body" | jq -e '.num_cores' >/dev/null 2>&1; then
            log_pass "Response is valid machine spec JSON"
        else
            log_fail "Response should contain machine spec"
        fi
    elif [ "$http_code" = "404" ]; then
        log_skip "Endpoint not implemented (returns 404)"
        log_info "Optional endpoint - not critical"
    else
        log_fail "Unexpected HTTP code: $http_code"
    fi
}

# Test unknown endpoint (should return 404)
test_unknown_endpoint() {
    log_test "Unknown Endpoint (404 Handling)"

    response=$(curl -s -w "\n%{http_code}" "${KUBELET_URL}/invalid/endpoint" 2>/dev/null || echo "000")
    http_code=$(echo "$response" | tail -n 1)

    if [ "$http_code" = "404" ]; then
        log_pass "Unknown endpoint returns HTTP 404"
    else
        log_fail "Unknown endpoint should return 404, got $http_code"
    fi
}

# Test response time (should be fast)
test_response_time() {
    log_test "Response Time"

    start_time=$(date +%s%N)
    curl -s "${KUBELET_URL}/healthz" >/dev/null 2>&1
    end_time=$(date +%s%N)

    duration_ms=$(( (end_time - start_time) / 1000000 ))

    log_info "Response time: ${duration_ms}ms"

    if [ "$duration_ms" -lt 1000 ]; then
        log_pass "Response time is acceptable (< 1000ms)"
    else
        log_fail "Response time too slow (> 1000ms)"
    fi
}

# Test concurrent connections
test_concurrent_requests() {
    log_test "Concurrent Connections"

    log_info "Sending 5 concurrent requests..."

    for i in {1..5}; do
        curl -s "${KUBELET_URL}/healthz" > /tmp/test_kubelet_$i.txt &
    done

    wait

    # Check all responses
    success_count=0
    for i in {1..5}; do
        if [ -f "/tmp/test_kubelet_$i.txt" ] && [ "$(cat /tmp/test_kubelet_$i.txt)" = "ok" ]; then
            ((success_count++))
        fi
        rm -f "/tmp/test_kubelet_$i.txt"
    done

    if [ "$success_count" -eq 5 ]; then
        log_pass "All 5 concurrent requests succeeded"
    else
        log_fail "Only $success_count/5 concurrent requests succeeded"
    fi
}

# Test HTTP protocol compliance
test_http_compliance() {
    log_test "HTTP Protocol Compliance"

    # Test HTTP/1.1 version
    response=$(curl -s -I "${KUBELET_URL}/healthz" 2>/dev/null)

    if echo "$response" | head -n 1 | grep -q "HTTP/1.1"; then
        log_pass "Uses HTTP/1.1 protocol"
    else
        log_fail "Should use HTTP/1.1 protocol"
        log_info "Got: $(echo "$response" | head -n 1)"
    fi

    # Check for required headers
    if echo "$response" | grep -qi "Content-Type:"; then
        log_pass "Content-Type header present"
    else
        log_fail "Content-Type header missing"
    fi

    if echo "$response" | grep -qi "Content-Length:"; then
        log_pass "Content-Length header present"
    else
        log_info "Content-Length header missing (acceptable with Connection: close)"
    fi
}

# Main test execution
main() {
    echo "========================================="
    echo "  Kubelet Endpoint Tests"
    echo "========================================="
    echo "Target: ${KUBELET_URL}"
    echo ""

    # Check dependencies
    for cmd in curl jq timeout; do
        if ! command -v $cmd &> /dev/null; then
            echo "Error: $cmd is not installed"
            exit 1
        fi
    done

    # Run tests
    if ! check_connectivity; then
        echo ""
        echo "Cannot reach kubelet - skipping remaining tests"
        exit 1
    fi

    test_healthz
    test_metrics
    test_pods
    test_spec
    test_unknown_endpoint
    test_response_time
    test_concurrent_requests
    test_http_compliance

    # Summary
    echo ""
    echo "========================================="
    echo "  Test Summary"
    echo "========================================="
    echo -e "${GREEN}Passed: ${TESTS_PASSED}${NC}"
    echo -e "${RED}Failed: ${TESTS_FAILED}${NC}"
    echo -e "${YELLOW}Skipped: ${TESTS_SKIPPED}${NC}"
    echo "========================================="

    if [ "$TESTS_FAILED" -gt 0 ]; then
        exit 1
    else
        exit 0
    fi
}

# Run main if not sourced
if [ "${BASH_SOURCE[0]}" -eq "${0}" ]; then
    main "$@"
fi
