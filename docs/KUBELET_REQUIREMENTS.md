# Kubernetes Kubelet Requirements

This document outlines what Kubernetes expects from a kubelet and how the Pico implementation meets (or doesn't meet) these requirements.

## What is a Kubelet?

The kubelet is the primary "node agent" that runs on each node in a Kubernetes cluster. It is responsible for:

1. **Registering the node** with the Kubernetes API server
2. **Reporting node status** via periodic heartbeats (typically every 10 seconds)
3. **Managing pod lifecycle** - starting, stopping, and monitoring containers
4. **Exposing HTTP endpoints** for health checks and metrics
5. **Reporting resource usage** - CPU, memory, disk, network
6. **Managing volumes** - mounting and unmounting storage
7. **Executing commands** - providing exec/attach/logs interfaces

## Standard Kubelet HTTP Endpoints

Real kubelets expose these endpoints on port 10250:

### Critical Endpoints (Required)
- **`GET /healthz`** - Health check endpoint
  - Returns: `ok` (200 OK) if healthy
  - Used by: Node lifecycle controller to determine node health
  - **Pico Status**: ✅ IMPLEMENTED

### Important Endpoints (Strongly Recommended)
- **`GET /pods`** - List all pods running on the node
  - Returns: JSON list of pods with their status
  - Used by: Kubernetes to verify pod placement
  - **Pico Status**: ❌ NOT IMPLEMENTED (returns 404)

- **`GET /metrics`** - Prometheus metrics
  - Returns: Prometheus-formatted metrics
  - Used by: Monitoring systems (optional but common)
  - **Pico Status**: ⚠️ PARTIAL (empty response with correct content-type)

- **`GET /spec`** - Machine specification
  - Returns: JSON with hardware capabilities
  - Used by: Scheduler for resource allocation
  - **Pico Status**: ❌ NOT IMPLEMENTED

### Optional/Advanced Endpoints
- **`GET /stats`** - Node and pod statistics
- **`GET /logs/{namespace}/{pod}/{container}`** - Container logs
- **`GET /exec/{namespace}/{pod}/{container}`** - Exec into containers
- **`POST /run/{namespace}/{pod}/{container}`** - Run commands
- **`GET /attach/{namespace}/{pod}/{container}`** - Attach to containers

**Pico Status**: ❌ NOT IMPLEMENTED (Pico has no containers)

## Node Registration & Status Updates

### Node Registration
When a kubelet starts, it registers the node with the API server:

**Request**: `POST /api/v1/nodes`
```json
{
  "kind": "Node",
  "apiVersion": "v1",
  "metadata": {
    "name": "node-name",
    "labels": {
      "kubernetes.io/hostname": "node-name",
      "kubernetes.io/arch": "arm",
      "kubernetes.io/os": "linux"
    }
  },
  "status": {
    "conditions": [...],
    "addresses": [...],
    "capacity": {...},
    "nodeInfo": {...}
  }
}
```

**Pico Status**: ✅ IMPLEMENTED in `node_status.c:node_status_register()`

### Node Status Heartbeats
Every 10 seconds (configurable), the kubelet sends a status update:

**Request**: `PATCH /api/v1/nodes/{name}/status`
```json
{
  "status": {
    "conditions": [
      {"type": "Ready", "status": "True", ...},
      {"type": "MemoryPressure", "status": "False", ...},
      {"type": "DiskPressure", "status": "False", ...}
    ],
    "addresses": [...],
    "capacity": {...}
  }
}
```

**Pico Status**: ✅ IMPLEMENTED in `node_status.c:node_status_report()`

### Node Conditions
Kubernetes tracks these node conditions:

| Condition | Meaning | Pico Reports |
|-----------|---------|--------------|
| **Ready** | Node is ready to accept pods | ✅ True |
| **MemoryPressure** | Node is low on memory | ✅ False |
| **DiskPressure** | Node is low on disk space | ✅ False |
| **PIDPressure** | Node has too many processes | ✅ False |
| **NetworkUnavailable** | Node network is not configured | ✅ False |

If a node doesn't send status updates for ~40 seconds (default), Kubernetes marks it as `NotReady`.

## Node Capacity & Allocatable Resources

Kubernetes needs to know what resources are available:

```json
{
  "capacity": {
    "cpu": "1",           // Number of CPU cores
    "memory": "256Ki",    // Total memory
    "pods": "0"           // Maximum number of pods
  },
  "allocatable": {
    "cpu": "1",
    "memory": "256Ki",
    "pods": "0"
  }
}
```

**Pico Reports**:
- CPU: `1` core (single-core RP2040)
- Memory: `256Ki` (264 KB SRAM)
- Pods: `0` (no container runtime)

**Note**: Since `pods: "0"`, the Kubernetes scheduler will **NOT** try to schedule workloads on this node. This is intentional for now.

## What Happens Without Full Kubelet Implementation?

### ✅ Will Work
1. **Node appears in cluster**: `kubectl get nodes` will show the Pico
2. **Node reports as Ready**: Status updates keep it alive
3. **ConfigMap watching**: Pico can poll API for ConfigMap changes
4. **Custom workloads**: Can use node labels/taints to store metadata

### ⚠️ May Cause Issues
1. **Node lifecycle controller**: May probe port 10250 for health
   - **Mitigation**: We implement `/healthz` endpoint
2. **Prometheus monitoring**: Expects `/metrics` endpoint
   - **Mitigation**: We return empty but valid Prometheus response
3. **Node problem detector**: May try to read node stats
   - **Impact**: Will get 404, but shouldn't fail the node

### ❌ Won't Work
1. **Pod scheduling**: Scheduler sees `pods: "0"` and won't schedule workloads
   - **This is intentional** - Pico has no container runtime
2. **kubectl logs/exec**: No container runtime to connect to
3. **DaemonSets**: Won't try to run on nodes with `pods: "0"`

## Minimal Requirements for "Ready" Node

To have Kubernetes accept a node as Ready:

| Requirement | Status | Implementation |
|-------------|--------|----------------|
| Register with API server | ✅ | `POST /api/v1/nodes` |
| Send periodic status updates | ✅ | `PATCH /api/v1/nodes/{name}/status` every 10-30s |
| Report "Ready" condition | ✅ | `conditions: [{type: Ready, status: True}]` |
| Expose kubelet port | ✅ | Port 10250 listening |
| Respond to `/healthz` | ✅ | Returns `ok` |
| Report capacity | ✅ | CPU, memory, pods in status |
| Have valid nodeInfo | ✅ | kubeletVersion, osImage, etc. |

**Pico Status**: ✅ **ALL MINIMAL REQUIREMENTS MET**

## Advanced Features (Not Required for Basic Node)

| Feature | Required? | Pico Status |
|---------|-----------|-------------|
| Container runtime | No (if pods=0) | ❌ None |
| Volume plugins | No (if pods=0) | ❌ None |
| CRI (Container Runtime Interface) | No (if pods=0) | ❌ None |
| CSI (Container Storage Interface) | No | ❌ None |
| CNI (Container Network Interface) | No (if pods=0) | ❌ None |
| Device plugins | No | ❌ None |
| Pod lifecycle | No (if pods=0) | ❌ None |
| Admission webhooks | No | ❌ None |

## Testing the Kubelet Implementation

### 1. Health Check Endpoint
```bash
# Test from network
curl http://192.168.1.100:10250/healthz

# Expected: "ok"
# HTTP 200 OK
```

### 2. Metrics Endpoint
```bash
curl http://192.168.1.100:10250/metrics

# Expected: Empty Prometheus response
# Content-Type: text/plain; version=0.0.4
# HTTP 200 OK
```

### 3. Pods Endpoint (Should Fail)
```bash
curl http://192.168.1.100:10250/pods

# Expected: 404 Not Found
# This is OK - we don't run pods
```

### 4. Node Registration
```bash
# Check if node registered
kubectl get nodes

# Expected:
# NAME          STATUS   ROLES    AGE   VERSION
# pico-node-1   Ready    <none>   10s   v1.34.0
```

### 5. Node Status Details
```bash
kubectl describe node pico-node-1

# Should show:
# - Conditions: Ready=True
# - Capacity: cpu=1, memory=256Ki, pods=0
# - Addresses: InternalIP
# - System Info: kubeletVersion, osImage, etc.
```

### 6. Node Heartbeats
```bash
# Watch node status updates
kubectl get node pico-node-1 -w

# Node should stay Ready (heartbeats every 10-30s)
# If no updates for ~40s, will become NotReady
```

## Recommendations for Pico Implementation

### Must Implement (Already Done ✅)
- [x] `/healthz` endpoint returning "ok"
- [x] Node registration via API
- [x] Periodic status updates (heartbeat)
- [x] Report Ready condition as True
- [x] Report capacity with pods=0

### Should Implement (Improves Compatibility)
- [ ] `/pods` endpoint returning empty list instead of 404
  ```json
  {
    "kind": "PodList",
    "apiVersion": "v1",
    "items": []
  }
  ```
- [ ] `/metrics` endpoint with real metrics:
  ```
  # HELP node_memory_free_bytes Free memory in bytes
  # TYPE node_memory_free_bytes gauge
  node_memory_free_bytes 229376

  # HELP node_cpu_usage_seconds_total CPU usage
  # TYPE node_cpu_usage_seconds_total counter
  node_cpu_usage_seconds_total 123.45
  ```
- [ ] `/spec` endpoint with hardware capabilities

### Could Implement (Nice to Have)
- [ ] Custom `/pico/status` endpoint with Pico-specific info
- [ ] `/pico/memory` endpoint showing memory manager state
- [ ] `/pico/gpio` endpoint for hardware control

## Kubernetes API Server Expectations

The API server expects:

1. **TLS on kubelet port** (10250)
   - **Pico**: Uses plain HTTP (acceptable for lab/development)
   - **Production**: Would need TLS, but Pico uses proxy architecture

2. **Authentication**
   - **Standard**: Client certificates or tokens
   - **Pico**: Currently none (relies on network security)
   - **Future**: Could use bootstrap tokens

3. **Authorization**
   - **Standard**: Kubelet API authorization
   - **Pico**: Not implemented (endpoints are public on port 10250)

4. **Node leases** (newer Kubernetes)
   - **Purpose**: More efficient heartbeat mechanism
   - **Pico**: Not implemented (uses status updates instead)
   - **Fallback**: Status updates work fine

## Summary: Does the Pico Meet Kubelet Requirements?

### Core Requirements (Node Registration & Health)
✅ **YES** - The Pico meets all requirements to be a valid Kubernetes node:
- Registers successfully
- Sends status updates (heartbeats)
- Responds to health checks
- Reports capacity honestly (pods=0)

### Pod Management Requirements
❌ **NO** - The Pico cannot run pods:
- No container runtime
- No pod lifecycle management
- No volume mounting
- This is **by design** - we report `pods: "0"`

### Monitoring & Observability
⚠️ **PARTIAL** - Basic monitoring works:
- `/healthz` works
- `/metrics` returns valid (but empty) response
- Missing detailed stats and logs (acceptable)

### Production Readiness
⚠️ **NOT PRODUCTION READY**:
- No TLS on kubelet port
- No authentication
- Limited error handling
- This is fine for lab/experimental deployment

## Conclusion

The Pico implementation meets the **minimum requirements** to be a valid Kubernetes node. It will:

- ✅ Appear in `kubectl get nodes`
- ✅ Show as "Ready"
- ✅ Accept ConfigMap-based configuration
- ✅ Respond to health probes
- ❌ NOT accept pod scheduling (intentional)
- ❌ NOT run containers

This is **sufficient for the intended use case**: a microcontroller that registers as a node for monitoring, configuration via ConfigMaps, and custom workload orchestration without traditional container runtime.
