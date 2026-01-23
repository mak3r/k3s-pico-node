#!/bin/bash
#
# Quick timestamp validation test
# Verifies critical timestamp functionality without lengthy monitoring
#

NODE_NAME="${NODE_NAME:-pico-node-1}"
PASSED=0
FAILED=0

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

test_assert() {
    local condition=$1
    local message=$2

    if [ "$condition" = "true" ]; then
        echo -e "  ${GREEN}✓${NC} $message"
        ((PASSED++))
    else
        echo -e "  ${RED}✗${NC} $message"
        ((FAILED++))
    fi
}

echo "========================================"
echo "  Node Timestamp Validation"
echo "========================================"
echo "Testing node: $NODE_NAME"
echo ""

# Test 1: Node exists and is Ready
echo "[TEST] Node Status"
status=$(kubectl get node "$NODE_NAME" --no-headers 2>/dev/null | awk '{print $2}')
test_assert "$([ "$status" = "Ready" ] && echo true || echo false)" \
    "Node is Ready"

# Test 2: Ready condition has timestamps
echo ""
echo "[TEST] Ready Condition Timestamps"

ready_json=$(kubectl get node "$NODE_NAME" -o json 2>/dev/null | \
             jq -r '.status.conditions[] | select(.type=="Ready")')

lastHeartbeat=$(echo "$ready_json" | jq -r '.lastHeartbeatTime')
lastTransition=$(echo "$ready_json" | jq -r '.lastTransitionTime')
ready_status=$(echo "$ready_json" | jq -r '.status')

test_assert "$([ "$lastHeartbeat" != "null" ] && [ "$lastHeartbeat" != "" ] && echo true || echo false)" \
    "lastHeartbeatTime is present"
test_assert "$([ "$lastTransition" != "null" ] && [ "$lastTransition" != "" ] && echo true || echo false)" \
    "lastTransitionTime is present"
test_assert "$([ "$ready_status" = "True" ] && echo true || echo false)" \
    "Ready status is True"

# Test 3: Timestamp format
echo ""
echo "[TEST] Timestamp Format"

iso8601_regex='^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$'

if [[ "$lastHeartbeat" =~ $iso8601_regex ]]; then
    test_assert true "lastHeartbeatTime is ISO 8601 format"
else
    test_assert false "lastHeartbeatTime is ISO 8601 format (got: $lastHeartbeat)"
fi

# Test 4: All conditions have timestamps
echo ""
echo "[TEST] All Conditions Have Timestamps"

conditions=("Ready" "MemoryPressure" "DiskPressure" "PIDPressure" "NetworkUnavailable")

for condition_type in "${conditions[@]}"; do
    hb=$(kubectl get node "$NODE_NAME" -o json 2>/dev/null | \
         jq -r ".status.conditions[] | select(.type==\"$condition_type\") | .lastHeartbeatTime")

    test_assert "$([ "$hb" != "null" ] && [ "$hb" != "" ] && echo true || echo false)" \
        "$condition_type has timestamp"
done

# Test 5: Critical regression test - no null timestamps
echo ""
echo "[TEST] Regression: No NULL Timestamps"

# Check that no timestamps are null
null_timestamps=$(kubectl get node "$NODE_NAME" -o json 2>/dev/null | \
                  jq -r '.status.conditions[] | select(.lastHeartbeatTime == null) | .type' | wc -l)

test_assert "$([ "$null_timestamps" = "0" ] && echo true || echo false)" \
    "No null lastHeartbeatTime values"

echo ""
echo "========================================"
echo "  Test Results"
echo "========================================"
echo "  Passed: $PASSED"
echo "  Failed: $FAILED"
echo "========================================"

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
fi
