#!/bin/bash
# Test to verify correct status update format

set -e

NODE_NAME="pico-node-1"

echo "========================================="
echo "  Testing Node Status Update Format"
echo "========================================="
echo ""

# Get current node
if ! kubectl get node "$NODE_NAME" &> /dev/null; then
    echo "Node $NODE_NAME not found"
    exit 1
fi

echo "Current node status:"
kubectl get node "$NODE_NAME" -o jsonpath='{.status.conditions[?(@.type=="Ready")]}'  | jq .
echo ""

# Show what the PATCH should look like
echo "Correct PATCH format for /api/v1/nodes/$NODE_NAME/status should be:"
echo "{"
echo "  \"status\": {"
echo "    \"conditions\": [...],"
echo "    \"addresses\": [...],"
echo "    \"capacity\": {...},"
echo "    \"allocatable\": {...},"
echo "    \"daemonEndpoints\": {...},"
echo "    \"nodeInfo\": {...}"
echo "  }"
echo "}"
echo ""
echo "NOT the full node object with kind/apiVersion/metadata!"
echo ""

# Test manual status update
echo "Testing manual status update..."
kubectl patch node "$NODE_NAME" --subresource=status --type=merge -p '{
  "status": {
    "conditions": [
      {"type": "Ready", "status": "True", "reason": "KubeletReady", "message": "Test update"}
    ]
  }
}' 2>&1

echo ""
echo "Check if heartbeat updated:"
kubectl get node "$NODE_NAME" -o jsonpath='{.status.conditions[?(@.type=="Ready")]}' | jq .
