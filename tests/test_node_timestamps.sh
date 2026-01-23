#!/bin/bash
#
# Integration tests for node status timestamps
#
# Verifies that:
# - Node conditions include lastHeartbeatTime and lastTransitionTime
# - Timestamps are in valid ISO 8601 format
# - Timestamps update on each heartbeat
# - Node maintains Ready status with proper timestamps
#

set -e

NODE_NAME="${NODE_NAME:-pico-node-1}"
PASSED=0
FAILED=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

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
echo "  Node Timestamp Integration Tests"
echo "========================================"
echo "Testing node: $NODE_NAME"
echo ""

# Test 1: Node exists
echo "[TEST] Node Registration"
if kubectl get node "$NODE_NAME" &>/dev/null; then
    test_assert true "Node $NODE_NAME exists in cluster"
else
    test_assert false "Node $NODE_NAME exists in cluster"
    echo "ERROR: Node not found. Ensure Pico is running and registered."
    exit 1
fi

# Test 2: Check Ready condition has timestamps
echo ""
echo "[TEST] Ready Condition Timestamps"

ready_condition=$(kubectl get node "$NODE_NAME" -o json | jq -r '.status.conditions[] | select(.type=="Ready")')

lastHeartbeat=$(echo "$ready_condition" | jq -r '.lastHeartbeatTime')
lastTransition=$(echo "$ready_condition" | jq -r '.lastTransitionTime')
status=$(echo "$ready_condition" | jq -r '.status')
reason=$(echo "$ready_condition" | jq -r '.reason')

test_assert "$([ "$lastHeartbeat" != "null" ] && echo true || echo false)" \
    "lastHeartbeatTime is present"
test_assert "$([ "$lastTransition" != "null" ] && echo true || echo false)" \
    "lastTransitionTime is present"
test_assert "$([ "$status" = "True" ] && echo true || echo false)" \
    "Ready status is True"
test_assert "$([ "$reason" = "KubeletReady" ] && echo true || echo false)" \
    "Reason is KubeletReady"

# Test 3: Validate timestamp format (ISO 8601)
echo ""
echo "[TEST] Timestamp Format Validation"

# ISO 8601 regex: YYYY-MM-DDTHH:MM:SSZ
iso8601_regex='^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$'

if [[ "$lastHeartbeat" =~ $iso8601_regex ]]; then
    test_assert true "lastHeartbeatTime matches ISO 8601 format"
else
    test_assert false "lastHeartbeatTime matches ISO 8601 format (got: $lastHeartbeat)"
fi

if [[ "$lastTransition" =~ $iso8601_regex ]]; then
    test_assert true "lastTransitionTime matches ISO 8601 format"
else
    test_assert false "lastTransitionTime matches ISO 8601 format (got: $lastTransition)"
fi

# Test 4: All conditions have timestamps
echo ""
echo "[TEST] All Condition Timestamps"

conditions=("Ready" "MemoryPressure" "DiskPressure" "PIDPressure" "NetworkUnavailable")

for condition_type in "${conditions[@]}"; do
    condition=$(kubectl get node "$NODE_NAME" -o json | \
                jq -r ".status.conditions[] | select(.type==\"$condition_type\")")

    hb=$(echo "$condition" | jq -r '.lastHeartbeatTime')
    lt=$(echo "$condition" | jq -r '.lastTransitionTime')

    test_assert "$([ "$hb" != "null" ] && echo true || echo false)" \
        "$condition_type has lastHeartbeatTime"
    test_assert "$([ "$lt" != "null" ] && echo true || echo false)" \
        "$condition_type has lastTransitionTime"
done

# Test 5: Timestamp progression (heartbeat updates)
echo ""
echo "[TEST] Timestamp Progression"

echo "  Capturing initial timestamp..."
initial_heartbeat=$(kubectl get node "$NODE_NAME" -o json | \
                   jq -r '.status.conditions[] | select(.type=="Ready") | .lastHeartbeatTime')

echo "  Waiting 15 seconds for heartbeat update..."
sleep 15

echo "  Capturing updated timestamp..."
updated_heartbeat=$(kubectl get node "$NODE_NAME" -o json | \
                   jq -r '.status.conditions[] | select(.type=="Ready") | .lastHeartbeatTime')

if [ "$initial_heartbeat" != "$updated_heartbeat" ]; then
    test_assert true "Heartbeat timestamp updates over time"
    echo "    Initial:  $initial_heartbeat"
    echo "    Updated:  $updated_heartbeat"
else
    test_assert false "Heartbeat timestamp updates over time"
    echo "    WARNING: Timestamp did not change in 15 seconds"
fi

# Test 6: Node stays Ready
echo ""
echo "[TEST] Node Stability"

echo "  Monitoring node status for 30 seconds..."
stable=true
for i in {1..6}; do
    sleep 5
    current_status=$(kubectl get node "$NODE_NAME" --no-headers | awk '{print $2}')
    if [ "$current_status" != "Ready" ]; then
        stable=false
        echo "    Iteration $i: Status changed to $current_status"
        break
    fi
done

test_assert "$([ "$stable" = true ] && echo true || echo false)" \
    "Node maintains Ready status"

# Test 7: Timestamp reasonableness
echo ""
echo "[TEST] Timestamp Reasonableness"

current_heartbeat=$(kubectl get node "$NODE_NAME" -o json | \
                   jq -r '.status.conditions[] | select(.type=="Ready") | .lastHeartbeatTime')

# Convert to epoch seconds
if command -v date &> /dev/null; then
    # Linux date command
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        heartbeat_epoch=$(date -j -f "%Y-%m-%dT%H:%M:%SZ" "$current_heartbeat" "+%s" 2>/dev/null || echo "0")
        current_epoch=$(date "+%s")
    else
        # Linux
        heartbeat_epoch=$(date -d "$current_heartbeat" "+%s" 2>/dev/null || echo "0")
        current_epoch=$(date "+%s")
    fi

    if [ "$heartbeat_epoch" != "0" ]; then
        time_diff=$((current_epoch - heartbeat_epoch))
        # Timestamp should be within last 60 seconds
        if [ $time_diff -ge -5 ] && [ $time_diff -le 60 ]; then
            test_assert true "Timestamp is recent (within last 60 seconds)"
        else
            test_assert false "Timestamp is recent (diff: ${time_diff}s)"
        fi

        # Timestamp should not be in the future (allow 5 second clock skew)
        if [ $time_diff -ge -5 ]; then
            test_assert true "Timestamp is not in the future"
        else
            test_assert false "Timestamp is in the future (${time_diff}s ahead)"
        fi
    else
        echo "  ⚠ Skipping timestamp reasonableness check (date parse failed)"
    fi
else
    echo "  ⚠ Skipping timestamp reasonableness check (date command not available)"
fi

# Test 8: Check node events for errors
echo ""
echo "[TEST] Node Events"

# Check for recent errors
error_events=$(kubectl get events --field-selector involvedObject.name="$NODE_NAME",type=Warning \
               --sort-by='.lastTimestamp' 2>/dev/null | tail -n +2 | wc -l)

test_assert "$([ $error_events -eq 0 ] && echo true || echo false)" \
    "No warning events in last period"

if [ $error_events -gt 0 ]; then
    echo "  Recent warning events:"
    kubectl get events --field-selector involvedObject.name="$NODE_NAME",type=Warning \
            --sort-by='.lastTimestamp' | tail -5
fi

# Summary
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
