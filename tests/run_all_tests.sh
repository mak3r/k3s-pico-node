#!/bin/bash
# Master test runner for k3s-pico-node
# Runs all available tests: unit, integration, and hardware tests

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
PICO_IP="${PICO_IP:-}"
NODE_NAME="${NODE_NAME:-pico-node-1}"

# Test categories to run
RUN_UNIT_TESTS=1
RUN_INTEGRATION_TESTS=0
RUN_HARDWARE_TESTS=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --unit-only)
            RUN_INTEGRATION_TESTS=0
            RUN_HARDWARE_TESTS=0
            shift
            ;;
        --integration-only)
            RUN_UNIT_TESTS=0
            RUN_HARDWARE_TESTS=0
            shift
            ;;
        --hardware-only)
            RUN_UNIT_TESTS=0
            RUN_INTEGRATION_TESTS=0
            shift
            ;;
        --all)
            RUN_UNIT_TESTS=1
            RUN_INTEGRATION_TESTS=1
            RUN_HARDWARE_TESTS=1
            shift
            ;;
        --pico-ip)
            PICO_IP="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --unit-only          Run only unit tests (default)"
            echo "  --integration-only   Run only integration tests (requires k3s)"
            echo "  --hardware-only      Run only hardware tests (requires Pico)"
            echo "  --all                Run all test categories"
            echo "  --pico-ip IP         Set Pico IP address for hardware tests"
            echo "  --help               Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                           # Run unit tests only"
            echo "  $0 --all --pico-ip 192.168.1.100  # Run all tests"
            echo "  $0 --integration-only        # Test k8s integration"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Run with --help for usage"
            exit 1
            ;;
    esac
done

# Print header
echo -e "${CYAN}"
echo "========================================="
echo "  k3s-pico-node Test Suite"
echo "========================================="
echo -e "${NC}"
echo "Test Configuration:"
echo "  Unit Tests: $([ $RUN_UNIT_TESTS -eq 1 ] && echo 'Yes' || echo 'No')"
echo "  Integration Tests: $([ $RUN_INTEGRATION_TESTS -eq 1 ] && echo 'Yes' || echo 'No')"
echo "  Hardware Tests: $([ $RUN_HARDWARE_TESTS -eq 1 ] && echo 'Yes' || echo 'No')"
[ -n "$PICO_IP" ] && echo "  Pico IP: $PICO_IP"
echo ""

# Test results
TOTAL_PASSED=0
TOTAL_FAILED=0
TOTAL_SKIPPED=0

# Run unit tests
if [ $RUN_UNIT_TESTS -eq 1 ]; then
    echo -e "${CYAN}=========================================${NC}"
    echo -e "${CYAN} 1. Unit Tests${NC}"
    echo -e "${CYAN}=========================================${NC}"
    echo ""

    # Build tests if needed
    if [ ! -d "$BUILD_DIR" ]; then
        echo "Building unit tests..."
        mkdir -p "$BUILD_DIR"
        cd "$BUILD_DIR"
        cmake .. || {
            echo -e "${RED}✗ Failed to configure tests${NC}"
            exit 1
        }
        make || {
            echo -e "${RED}✗ Failed to build tests${NC}"
            exit 1
        }
        cd "$SCRIPT_DIR"
        echo ""
    fi

    # Run HTTP client tests
    echo "Running HTTP client tests..."
    if [ -f "$BUILD_DIR/test_http_client" ]; then
        if "$BUILD_DIR/test_http_client"; then
            echo -e "${GREEN}✓ HTTP client tests passed${NC}"
        else
            echo -e "${RED}✗ HTTP client tests failed${NC}"
            ((TOTAL_FAILED++))
        fi
    else
        echo -e "${YELLOW}⊘ HTTP client tests not built${NC}"
        ((TOTAL_SKIPPED++))
    fi
    echo ""

    # Run node status tests
    echo "Running node status tests..."
    if [ -f "$BUILD_DIR/test_node_status" ]; then
        if "$BUILD_DIR/test_node_status"; then
            echo -e "${GREEN}✓ Node status tests passed${NC}"
        else
            echo -e "${RED}✗ Node status tests failed${NC}"
            ((TOTAL_FAILED++))
        fi
    else
        echo -e "${YELLOW}⊘ Node status tests not built${NC}"
        ((TOTAL_SKIPPED++))
    fi
    echo ""
fi

# Run integration tests
if [ $RUN_INTEGRATION_TESTS -eq 1 ]; then
    echo -e "${CYAN}=========================================${NC}"
    echo -e "${CYAN} 2. Integration Tests${NC}"
    echo -e "${CYAN}=========================================${NC}"
    echo ""

    # Check kubectl is available
    if ! command -v kubectl &> /dev/null; then
        echo -e "${YELLOW}⊘ kubectl not found - skipping integration tests${NC}"
        ((TOTAL_SKIPPED+=4))
    elif ! kubectl cluster-info &> /dev/null; then
        echo -e "${YELLOW}⊘ k8s cluster not accessible - skipping integration tests${NC}"
        ((TOTAL_SKIPPED+=3))
    else
        # Run node registration tests
        echo "Running node registration tests..."
        if "${SCRIPT_DIR}/test_node_registration.sh"; then
            echo -e "${GREEN}✓ Node registration tests passed${NC}"
            ((TOTAL_PASSED++))
        else
            echo -e "${RED}✗ Node registration tests failed${NC}"
            ((TOTAL_FAILED++))
        fi
        echo ""

        # Run ConfigMap polling tests
        echo "Running ConfigMap polling tests..."
        if "${SCRIPT_DIR}/test_configmap_polling.sh"; then
            echo -e "${GREEN}✓ ConfigMap polling tests passed${NC}"
            ((TOTAL_PASSED++))
        else
            echo -e "${RED}✗ ConfigMap polling tests failed${NC}"
            ((TOTAL_FAILED++))
        fi
        echo ""

        # Run timestamp validation tests
        echo "Running node timestamp validation tests..."
        if "${SCRIPT_DIR}/test_node_timestamps_simple.sh"; then
            echo -e "${GREEN}✓ Timestamp validation tests passed${NC}"
            ((TOTAL_PASSED++))
        else
            echo -e "${RED}✗ Timestamp validation tests failed${NC}"
            ((TOTAL_FAILED++))
        fi
        echo ""
    fi
fi

# Run hardware tests
if [ $RUN_HARDWARE_TESTS -eq 1 ]; then
    echo -e "${CYAN}=========================================${NC}"
    echo -e "${CYAN} 3. Hardware Tests (Pico)${NC}"
    echo -e "${CYAN}=========================================${NC}"
    echo ""

    if [ -z "$PICO_IP" ]; then
        echo -e "${YELLOW}⊘ PICO_IP not set - skipping hardware tests${NC}"
        echo "  Set PICO_IP environment variable or use --pico-ip option"
        ((TOTAL_SKIPPED+=2))
    else
        # Run kubelet endpoint tests
        echo "Running kubelet endpoint tests..."
        export PICO_IP
        if "${SCRIPT_DIR}/test_kubelet_endpoints.sh"; then
            echo -e "${GREEN}✓ Kubelet endpoint tests passed${NC}"
            ((TOTAL_PASSED++))
        else
            echo -e "${RED}✗ Kubelet endpoint tests failed${NC}"
            ((TOTAL_FAILED++))
        fi
        echo ""
    fi
fi

# Final summary
echo ""
echo -e "${CYAN}=========================================${NC}"
echo -e "${CYAN} Final Test Summary${NC}"
echo -e "${CYAN}=========================================${NC}"

# Calculate total
TOTAL_TESTS=$((TOTAL_PASSED + TOTAL_FAILED + TOTAL_SKIPPED))

echo -e "${GREEN}Passed:  ${TOTAL_PASSED}${NC}"
echo -e "${RED}Failed:  ${TOTAL_FAILED}${NC}"
echo -e "${YELLOW}Skipped: ${TOTAL_SKIPPED}${NC}"
echo "Total:   ${TOTAL_TESTS}"
echo ""

# Exit code
if [ $TOTAL_FAILED -gt 0 ]; then
    echo -e "${RED}✗ Some tests failed${NC}"
    exit 1
else
    echo -e "${GREEN}✓ All tests passed${NC}"
    exit 0
fi
