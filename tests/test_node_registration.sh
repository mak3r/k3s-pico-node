#!/bin/bash
# Integration tests for Kubernetes node registration
# Tests that the Pico can successfully register and maintain node status

set -e

# Configuration
NODE_NAME="${NODE_NAME:-pico-node-1}"
KUBECONFIG="${KUBECONFIG:-${HOME}/.kube/config}"

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
    echo -e "${BLUE}  ℹ${NC} $1"
}

# Check kubectl is available and cluster is accessible
check_prerequisites() {
    log_test "Prerequisites"

    if ! command -v kubectl &> /dev/null; then
        log_fail "kubectl not found in PATH"
        return 1
    fi
    log_pass "kubectl is installed"

    if ! kubectl cluster-info &> /dev/null; then
        log_fail "Cannot connect to Kubernetes cluster"
        log_info "Check your kubeconfig: $KUBECONFIG"
        return 1
    fi
    log_pass "Kubernetes cluster is accessible"

    # Check for required tools
    for tool in jq timeout; do
        if ! command -v $tool &> /dev/null; then
            log_fail "$tool not found in PATH"
            return 1
        fi
    done
    log_pass "Required tools available (jq, timeout)"

    return 0
}

# Test: Check if node exists
test_node_exists() {
    log_test "Node Existence"

    if kubectl get node "$NODE_NAME" &> /dev/null; then
        log_pass "Node '$NODE_NAME' exists in cluster"
        return 0
    else
        log_fail "Node '$NODE_NAME' not found in cluster"
        log_info "Run: kubectl get nodes"
        log_info "Make sure Pico has connected and registered"
        return 1
    fi
}

# Test: Node is in Ready state
test_node_ready() {
    log_test "Node Ready Status"

    status=$(kubectl get node "$NODE_NAME" -o jsonpath='{.status.conditions[?(@.type=="Ready")].status}' 2>/dev/null)

    if [ "$status" = "True" ]; then
        log_pass "Node is in Ready state"

        # Check last heartbeat time
        last_heartbeat=$(kubectl get node "$NODE_NAME" -o jsonpath='{.status.conditions[?(@.type=="Ready")].lastHeartbeatTime}')
        log_info "Last heartbeat: $last_heartbeat"
    else
        log_fail "Node is not Ready (status: $status)"

        # Get reason
        reason=$(kubectl get node "$NODE_NAME" -o jsonpath='{.status.conditions[?(@.type=="Ready")].reason}')
        message=$(kubectl get node "$NODE_NAME" -o jsonpath='{.status.conditions[?(@.type=="Ready")].message}')
        log_info "Reason: $reason"
        log_info "Message: $message"
    fi
}

# Test: Node has correct labels
test_node_labels() {
    log_test "Node Labels"

    labels=$(kubectl get node "$NODE_NAME" -o json | jq -r '.metadata.labels')

    # Check required labels
    required_labels=(
        "kubernetes.io/arch"
        "kubernetes.io/os"
        "kubernetes.io/hostname"
    )

    for label in "${required_labels[@]}"; do
        value=$(echo "$labels" | jq -r ".\"$label\"" 2>/dev/null)
        if [ "$value" != "null" ] && [ -n "$value" ]; then
            log_pass "Label '$label' = '$value'"
        else
            log_fail "Required label '$label' is missing"
        fi
    done

    # Check Pico-specific label
    instance_type=$(echo "$labels" | jq -r '.["node.kubernetes.io/instance-type"]' 2>/dev/null)
    if [ "$instance_type" = "rp2040-pico" ]; then
        log_pass "Instance type label correctly set to 'rp2040-pico'"
    else
        log_info "Instance type label: $instance_type (expected: rp2040-pico)"
    fi
}

# Test: Node has correct capacity
test_node_capacity() {
    log_test "Node Capacity"

    capacity=$(kubectl get node "$NODE_NAME" -o json | jq -r '.status.capacity')

    cpu=$(echo "$capacity" | jq -r '.cpu')
    memory=$(echo "$capacity" | jq -r '.memory')
    pods=$(echo "$capacity" | jq -r '.pods')

    log_info "CPU: $cpu"
    log_info "Memory: $memory"
    log_info "Pods: $pods"

    if [ "$cpu" = "1" ]; then
        log_pass "CPU capacity is 1 (single core RP2040)"
    else
        log_fail "Expected CPU=1, got: $cpu"
    fi

    if [ "$memory" = "256Ki" ]; then
        log_pass "Memory capacity is 256Ki (264KB SRAM)"
    else
        log_fail "Expected memory=256Ki, got: $memory"
    fi

    if [ "$pods" = "0" ]; then
        log_pass "Pod capacity is 0 (no container runtime)"
    else
        log_fail "Expected pods=0, got: $pods"
    fi
}

# Test: Node has correct addresses
test_node_addresses() {
    log_test "Node Addresses"

    addresses=$(kubectl get node "$NODE_NAME" -o json | jq -r '.status.addresses[]')

    internal_ip=$(kubectl get node "$NODE_NAME" -o jsonpath='{.status.addresses[?(@.type=="InternalIP")].address}')
    hostname=$(kubectl get node "$NODE_NAME" -o jsonpath='{.status.addresses[?(@.type=="Hostname")].address}')

    if [ -n "$internal_ip" ]; then
        log_pass "InternalIP address: $internal_ip"

        # Validate IP format
        if [[ $internal_ip =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            log_pass "IP address format is valid"
        else
            log_fail "IP address format is invalid"
        fi
    else
        log_fail "InternalIP address is missing"
    fi

    if [ -n "$hostname" ]; then
        log_pass "Hostname address: $hostname"
    else
        log_fail "Hostname address is missing"
    fi
}

# Test: Node conditions are correct
test_node_conditions() {
    log_test "Node Conditions"

    conditions=$(kubectl get node "$NODE_NAME" -o json | jq -r '.status.conditions[]')

    # Check each condition type
    condition_types=("Ready" "MemoryPressure" "DiskPressure" "PIDPressure" "NetworkUnavailable")
    expected_status=("True" "False" "False" "False" "False")

    for i in "${!condition_types[@]}"; do
        type="${condition_types[$i]}"
        expected="${expected_status[$i]}"

        status=$(kubectl get node "$NODE_NAME" -o jsonpath="{.status.conditions[?(@.type=='$type')].status}")

        if [ "$status" = "$expected" ]; then
            log_pass "Condition '$type' = '$status' (correct)"
        else
            log_fail "Condition '$type' = '$status' (expected: $expected)"
        fi
    done
}

# Test: Node info is correct
test_node_info() {
    log_test "Node Info"

    node_info=$(kubectl get node "$NODE_NAME" -o json | jq -r '.status.nodeInfo')

    kubelet_version=$(echo "$node_info" | jq -r '.kubeletVersion')
    os_image=$(echo "$node_info" | jq -r '.osImage')
    architecture=$(echo "$node_info" | jq -r '.architecture')
    operating_system=$(echo "$node_info" | jq -r '.operatingSystem')

    log_info "kubeletVersion: $kubelet_version"
    log_info "osImage: $os_image"
    log_info "architecture: $architecture"
    log_info "operatingSystem: $operating_system"

    if [ -n "$kubelet_version" ] && [ "$kubelet_version" != "null" ]; then
        log_pass "kubeletVersion is set"
    else
        log_fail "kubeletVersion is missing"
    fi

    if [ "$architecture" = "arm" ]; then
        log_pass "Architecture is 'arm' (RP2040)"
    else
        log_fail "Expected architecture=arm, got: $architecture"
    fi

    if [ "$operating_system" = "linux" ]; then
        log_pass "Operating system is 'linux'"
    else
        log_fail "Expected operatingSystem=linux, got: $operating_system"
    fi
}

# Test: Node heartbeat is recent
test_node_heartbeat() {
    log_test "Node Heartbeat (Status Updates)"

    last_heartbeat=$(kubectl get node "$NODE_NAME" -o jsonpath='{.status.conditions[?(@.type=="Ready")].lastHeartbeatTime}')

    if [ -z "$last_heartbeat" ]; then
        log_fail "No heartbeat time available"
        return 1
    fi

    # Convert to Unix timestamp
    heartbeat_ts=$(date -d "$last_heartbeat" +%s 2>/dev/null || date -j -f "%Y-%m-%dT%H:%M:%SZ" "$last_heartbeat" +%s 2>/dev/null)
    current_ts=$(date +%s)
    age=$((current_ts - heartbeat_ts))

    log_info "Last heartbeat: $last_heartbeat"
    log_info "Age: ${age} seconds ago"

    if [ "$age" -lt 60 ]; then
        log_pass "Heartbeat is recent (< 60 seconds old)"
    else
        log_fail "Heartbeat is stale (> 60 seconds old)"
        log_info "Node may not be sending status updates"
    fi
}

# Test: Node can be described without errors
test_node_describe() {
    log_test "Node Describe"

    if kubectl describe node "$NODE_NAME" > /tmp/node_describe.txt 2>&1; then
        log_pass "kubectl describe node succeeds"

        # Check for common issues in output
        if grep -q "NotReady" /tmp/node_describe.txt; then
            log_fail "Node description shows NotReady status"
        fi

        if grep -q "Unschedulable" /tmp/node_describe.txt; then
            log_info "Node is marked Unschedulable (may be intentional)"
        fi
    else
        log_fail "kubectl describe node failed"
    fi

    rm -f /tmp/node_describe.txt
}

# Test: Scheduler won't schedule pods on this node
test_pod_scheduling() {
    log_test "Pod Scheduling (Should NOT Schedule)"

    pods_capacity=$(kubectl get node "$NODE_NAME" -o jsonpath='{.status.capacity.pods}')

    if [ "$pods_capacity" = "0" ]; then
        log_pass "Node reports pods=0, scheduler will skip this node"
        log_info "This is correct - Pico has no container runtime"
    else
        log_fail "Node reports pods=$pods_capacity, scheduler may try to schedule here"
        log_info "Should be pods=0 to prevent scheduling"
    fi
}

# Test: Node persists after waiting
test_node_persistence() {
    log_test "Node Persistence (Heartbeat Survival)"

    log_info "Waiting 15 seconds to verify node stays Ready..."
    sleep 15

    status=$(kubectl get node "$NODE_NAME" -o jsonpath='{.status.conditions[?(@.type=="Ready")].status}' 2>/dev/null)

    if [ "$status" = "True" ]; then
        log_pass "Node remained Ready after 15 seconds"
    else
        log_fail "Node became NotReady (status: $status)"
        log_info "Check that Pico is still running and sending status updates"
    fi
}

# Main execution
main() {
    echo "========================================="
    echo "  Kubernetes Node Registration Tests"
    echo "========================================="
    echo "Node Name: $NODE_NAME"
    echo "Kubeconfig: $KUBECONFIG"
    echo ""

    # Run prerequisite checks
    if ! check_prerequisites; then
        echo ""
        echo "Prerequisites failed - cannot run tests"
        exit 1
    fi

    # Run all tests
    if ! test_node_exists; then
        echo ""
        echo "Node doesn't exist - skipping remaining tests"
        echo ""
        echo "To debug:"
        echo "  1. Check Pico serial output for registration errors"
        echo "  2. Verify k3s server is accessible from Pico"
        echo "  3. Check nginx proxy is running: kubectl get pod -n kube-system -l app=pico-proxy"
        exit 1
    fi

    test_node_ready
    test_node_labels
    test_node_capacity
    test_node_addresses
    test_node_conditions
    test_node_info
    test_node_heartbeat
    test_node_describe
    test_pod_scheduling
    test_node_persistence

    # Summary
    echo ""
    echo "========================================="
    echo "  Test Summary"
    echo "========================================="
    echo -e "${GREEN}Passed: ${TESTS_PASSED}${NC}"
    echo -e "${RED}Failed: ${TESTS_FAILED}${NC}"
    echo -e "${YELLOW}Skipped: ${TESTS_SKIPPED}${NC}"
    echo "========================================="

    # Show node details
    echo ""
    echo "Node Status:"
    kubectl get node "$NODE_NAME" -o wide

    if [ "$TESTS_FAILED" -gt 0 ]; then
        exit 1
    else
        exit 0
    fi
}

# Run main
main "$@"
