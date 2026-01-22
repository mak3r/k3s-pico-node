# K3s Pico Node - Revised Architecture Plan

## Key Insights

### Hardware Reality
- **SRAM:** 264 KB (active execution memory)
- **Flash:** 2 GB QSPI (persistent storage - HUGE resource!)
- **CPU:** Dual-core ARM Cortex-M0+ @ 133 MHz
- **Network:** 2.4GHz WiFi (CYW43)

### Architectural Principle

**The Pico is a Kubernetes Resource Broker**, not just a node that runs pods.

It can represent various K8s resource types:
- **Pods** (most common)
- **Services** (network endpoints)
- **Deployments** (managed pod collections)
- **Ingress** (routing rules)
- **Custom Resources** (anything!)

Each firmware build is purpose-specific and implements one or more resources.

## Node Status Reporting (Answering Your Question)

### What K8s Expects from Node Status

From the Kubernetes API, node status includes:

```yaml
status:
  capacity:
    cpu: "2"                    # Number of CPU cores (we have 2!)
    memory: "264Ki"             # Total SRAM
    pods: "10"                  # Max pods we'll support
    ephemeral-storage: "2Gi"    # The 2GB flash storage!

  allocatable:
    cpu: "1800m"                # Millicores available (90% of 2 cores)
    memory: "200Ki"             # Usable SRAM (leave ~64KB for system)
    pods: "10"
    ephemeral-storage: "1900Mi" # Usable flash (leave ~100MB overhead)

  conditions:
    - type: Ready
      status: "True"
    - type: MemoryPressure
      status: "False"           # True if >85% SRAM used
    - type: DiskPressure
      status: "False"           # True if >85% flash used
    - type: PIDPressure
      status: "False"           # N/A for us
    - type: NetworkUnavailable
      status: "False"           # True if WiFi down

  nodeInfo:
    machineID: "pico-<serial-number>"
    systemUUID: "<unique-id>"
    bootID: "<changes-on-reboot>"
    kernelVersion: "pico-sdk-1.5.1"
    osImage: "Pico-K8s-v1.0"
    containerRuntimeVersion: "pico-runtime://1.0.0"
    kubeletVersion: "v1.28.0"
    operatingSystem: "baremetal"
    architecture: "arm"
```

### Resource Utilization (What to Report)

**SRAM Utilization:**
```c
// Real values we can report
uint32_t total_sram = 264 * 1024;  // 264KB
uint32_t used_sram = get_used_heap();  // Track actual usage
uint32_t free_sram = total_sram - used_sram;

// Report to K8s
"memory": {
    "capacity": "264Ki",
    "allocatable": "200Ki",
    "used": "<calculated>Ki",
    "available": "<calculated>Ki"
}
```

**Flash Utilization:**
```c
// Flash partition layout
#define FIRMWARE_SIZE     (4 * 1024 * 1024)    // 4MB
#define MANIFEST_SIZE     (4 * 1024 * 1024)    // 4MB
#define IMAGE_SIZE        (8 * 1024 * 1024)    // 8MB
#define DATA_SIZE         (2048 - 16) * 1024 * 1024  // Rest

uint32_t flash_used = calculate_flash_usage();

"ephemeral-storage": {
    "capacity": "2Gi",
    "allocatable": "1900Mi",
    "used": "<calculated>Mi"
}
```

**CPU Utilization (Faked, but plausible):**
```c
// We can track actual CPU busy time with timers
uint32_t cpu_busy_ms = 0;
uint32_t cpu_total_ms = uptime_ms();
uint32_t cpu_percent = (cpu_busy_ms * 100) / cpu_total_ms;

"cpu": {
    "capacity": "2",           // 2 cores
    "allocatable": "1800m",    // 1.8 cores (90%)
    "used": "<calculated>m"    // Millicores
}
```

## ECI Registry & Container Runtime Simulation

### The Illusion K8s Needs to See

When K8s schedules a pod, the node must fake:

1. **ECI Pull:** "Downloading embedded container image from registry"
2. **ECI Verification:** "Checking signature"
3. **Container Creation:** "Creating container from ECI"
4. **Container Start:** "Starting container processes"
5. **Container Runtime:** "Container is running"

### How We Fake It

#### ECI "Registry"

Store embedded container images (ECIs) in flash as firmware variants:

```
Flash Layout:
[0x00000000 - 0x00400000]  Bootloader/Current firmware (4MB)
[0x00400000 - 0x00800000]  "ECI registry" (4MB)
    ├── gpio-controller:v1  (embedded container image)
    ├── temp-sensor:v1      (embedded container image)
    ├── led-display:v1      (embedded container image)
    └── custom-app:v1       (embedded container image)
```

#### "ECI Pull" Simulation

```c
// When K8s schedules pod with ECI "pico/gpio:v1"
int fake_eci_pull(const char *eci_name) {
    printf("Pulling ECI: %s\n", eci_name);
    sleep_ms(1000);  // Fake network delay

    // Look up ECI in flash registry
    firmware_t *fw = flash_lookup_eci(eci_name);
    if (!fw) {
        return -1;  // ImagePullBackOff
    }

    printf("ECI pulled: %s (size: %d bytes)\n",
           eci_name, fw->size);
    return 0;
}
```

#### "Container" Creation

```c
// A "container" is just a function pointer + state
typedef struct {
    const char *name;
    const char *eci;            // ECI name
    int (*main_func)(void);     // The actual code
    void *state;                // Container-specific state
    bool running;
} container_t;

int create_container(const char *eci_name) {
    firmware_t *fw = flash_lookup_eci(eci_name);

    container_t *c = malloc(sizeof(container_t));
    c->name = extract_name_from_eci(eci_name);
    c->eci = eci_name;
    c->main_func = (int (*)(void))fw->entry_point;
    c->state = malloc(fw->state_size);
    c->running = false;

    return 0;
}

int start_container(container_t *c) {
    c->running = true;

    // Create a "thread" (really just a state machine)
    // Or use the second core for actual parallel execution!
    multicore_launch_core1(c->main_func);

    return 0;
}
```

#### Resource Accounting & Enforcement

**Note:** We don't need to fake "cgroups" - that's a Linux implementation detail. The K8s control plane doesn't interact with cgroups. We just need resource accounting and limit enforcement.

**What the control plane needs:**
- Pod resource requests/limits (from pod spec)
- Container status (Running/Failed/OOMKilled)
- Node capacity/allocatable (for scheduling)

**What we implement:**

```c
// Per-container resource tracking
typedef struct {
    char *container_name;

    // Memory accounting
    uint32_t memory_allocated;      // Current usage
    uint32_t memory_limit;          // From pod spec limits
    uint32_t memory_peak;           // Peak usage

    // CPU accounting
    uint32_t cpu_time_ms;           // Total CPU time
    uint32_t cpu_limit_millicores;  // From pod spec limits

    // Storage accounting
    uint32_t storage_used;          // Flash usage
    uint32_t storage_limit;         // From pod spec limits
} container_resources_t;

// Enforce limits during allocation
void* container_malloc(container_t *c, size_t size) {
    if (c->resources->memory_allocated + size > c->resources->memory_limit) {
        // OOM - kill container, report to K8s
        pod_set_status(c->pod, POD_STATUS_FAILED, "OOMKilled");
        return NULL;
    }

    void *ptr = malloc(size);
    if (ptr) {
        c->resources->memory_allocated += size;
    }
    return ptr;
}

// Report to K8s (only status, not live metrics)
"status": {
    "phase": "Running",  // or "Failed"
    "containerStatuses": [{
        "name": "gpio",
        "state": {"running": {"startedAt": "2026-01-20T22:00:00Z"}},
        "lastState": {},  // Previous state if restarted
        "ready": true,
        "restartCount": 0
    }]
}
```

**Key Insights:**
- ✅ **Track usage** - Know how much each container uses
- ✅ **Enforce limits** - Kill containers that exceed limits
- ✅ **Report failures** - Tell K8s why pod failed ("OOMKilled")
- ❌ **Don't need isolation** - No PID/mount/network namespaces needed
- ❌ **Don't need metrics API** - Optional, can add later for `kubectl top`

See `RESOURCE_ACCOUNTING.md` for complete implementation details.

## Revised Three-Layer Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  Layer 3: Kubernetes Resource Abstraction                       │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Resource Manager (NEW - Central Orchestrator)           │  │
│  │  - Discovers what resources firmware implements          │  │
│  │  - Routes ConfigMap/Secret updates to resources          │  │
│  │  - Aggregates status from all resources                  │  │
│  │  - Reports to K8s API server                             │  │
│  └──────────────────────────────────────────────────────────┘  │
│                           ↓                                      │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Resource Implementations (Pluggable)                    │  │
│  │  ┌────────────────┐  ┌────────────────┐  ┌────────────┐│  │
│  │  │ Pod Manager    │  │Service Manager │  │  Ingress   ││  │
│  │  │ - Pod lifecycle│  │- Endpoints     │  │  - Routes  ││  │
│  │  │ - Container    │  │- Load balancer │  │  - Rules   ││  │
│  │  │   simulation   │  │  simulation    │  │            ││  │
│  │  └────────────────┘  └────────────────┘  └────────────┘│  │
│  │  ┌────────────────┐  ┌────────────────┐  ┌────────────┐│  │
│  │  │  Deployment    │  │  ConfigMap     │  │   Custom   ││  │
│  │  │  - Replicas    │  │  - Key/Value   │  │  Resource  ││  │
│  │  │  - Updates     │  │  - Watch       │  │  - User    ││  │
│  │  │                │  │                │  │   defined  ││  │
│  │  └────────────────┘  └────────────────┘  └────────────┘│  │
│  └──────────────────────────────────────────────────────────┘  │
│                           ↓                                      │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Node Manager (Already Exists)                           │  │
│  │  - Node registration                                     │  │
│  │  - Node status (capacity, allocatable, conditions)       │  │
│  │  - Resource utilization tracking                         │  │
│  │  - Health monitoring                                     │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────────────┐
│  Layer 2: K3s API Client (Generic Communication)                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  HTTP Functions (Issue #1 Implementation)               │  │
│  │  - k3s_request(method, path, body, response)            │  │
│  │  - k3s_get(path, response)                              │  │
│  │  - k3s_post(path, body)                                 │  │
│  │  - k3s_patch(path, body)                                │  │
│  │  - k3s_delete(path)                                     │  │
│  │  - k3s_watch(path, callback)  // Future: SSE/websocket │  │
│  │                                                          │  │
│  │  NOTE: All requests go via HTTP to nginx TLS proxy      │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────────────┐
│  Layer 1: HTTP Transport (HTTP-only via TLS Proxy)              │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  TCP Connection (lwIP)                                   │  │
│  │  - Connection management                                 │  │
│  │  - Buffer management                                     │  │
│  └──────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  HTTP Client (Plain HTTP)                                │  │
│  │  - HTTP request formatting                               │  │
│  │  - HTTP response parsing                                 │  │
│  │  - Connects to nginx proxy on k3s server                 │  │
│  │  - Proxy terminates TLS before reaching Pico             │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────────────┐
│  Layer 0: Hardware Abstraction (Already Exists)                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Flash Manager (NEW - Critical for 2GB storage)         │  │
│  │  - Flash partitions                                      │  │
│  │  - Image registry                                        │  │
│  │  - Data persistence                                      │  │
│  │  - Wear leveling                                         │  │
│  └──────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Memory Manager (Already Exists)                        │  │
│  │  - SRAM allocation tracking                              │  │
│  │  - Per-container memory accounting                       │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

## Flexible Resource Interface

### Common Interface for All K8s Resources

```c
// All K8s resource types implement this interface
typedef struct {
    // Metadata
    const char *kind;           // "Pod", "Service", "Deployment", etc.
    const char *api_version;    // "v1", "apps/v1", etc.
    const char *name;
    const char *namespace;

    // Lifecycle
    int (*init)(void);                          // Initialize resource
    int (*apply)(const char *manifest_json);    // Create/update from K8s
    int (*delete)(void);                        // Clean up resource

    // Status reporting
    int (*get_status)(char *status_json, size_t size);

    // Watch/update
    int (*on_update)(const char *manifest_json);  // ConfigMap change, etc.

    // Resource-specific
    void *private_data;         // Resource-specific state
} k8s_resource_t;
```

### Example: Pod Implementation

```c
typedef struct {
    char *pod_name;
    container_t *containers[4];  // Max 4 containers per pod
    int container_count;
    pod_phase_t phase;           // Pending, Running, Succeeded, Failed
    uint32_t memory_used;
    uint32_t cpu_used;
} pod_private_t;

k8s_resource_t gpio_pod_resource = {
    .kind = "Pod",
    .api_version = "v1",
    .name = "gpio-controller",
    .namespace = "default",
    .init = gpio_pod_init,
    .apply = gpio_pod_apply,
    .delete = gpio_pod_delete,
    .get_status = gpio_pod_get_status,
    .on_update = gpio_pod_on_update,
    .private_data = &gpio_pod_private
};

// Register with resource manager
resource_manager_register(&gpio_pod_resource);
```

### Example: Service Implementation

```c
typedef struct {
    char *service_name;
    uint16_t port;
    char *target_selector;   // Which pod(s) this service routes to
    endpoint_t endpoints[8]; // IP:Port pairs
} service_private_t;

k8s_resource_t gpio_service_resource = {
    .kind = "Service",
    .api_version = "v1",
    .name = "gpio-service",
    .namespace = "default",
    .init = gpio_service_init,
    .apply = gpio_service_apply,
    .delete = gpio_service_delete,
    .get_status = gpio_service_get_status,
    .on_update = gpio_service_on_update,
    .private_data = &gpio_service_private
};
```

### Example: Custom Resource

```c
// User-defined: Represents a physical LED matrix
typedef struct {
    uint8_t brightness;
    char message[256];
    uint32_t scroll_speed_ms;
} ledmatrix_private_t;

k8s_resource_t ledmatrix_resource = {
    .kind = "LEDMatrix",
    .api_version = "pico.io/v1",
    .name = "main-display",
    .namespace = "default",
    .init = ledmatrix_init,
    .apply = ledmatrix_apply,
    .delete = ledmatrix_delete,
    .get_status = ledmatrix_get_status,
    .on_update = ledmatrix_on_update,
    .private_data = &ledmatrix_private
};
```

## Use Case Examples

### Use Case 1: GPIO Controller Node

**Purpose:** Control external devices via GPIO pins

**Resources Implemented:**
- 1 Node (the Pico itself)
- 1 Pod ("gpio-controller")
- 1 Service ("gpio-service" - exposes HTTP API on kubelet port)

**ConfigMap Control:**
```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: gpio-config
  namespace: default
data:
  pin_0: "HIGH"
  pin_1: "LOW"
  pin_2: "PWM:128"  # PWM duty cycle
```

**Flash Usage:**
- Current ECI: 2MB
- Logs: 10MB (rolling)
- GPIO state history: 100MB (for debugging)

### Use Case 2: Multi-Sensor Node

**Purpose:** Read multiple sensors, report metrics

**Resources Implemented:**
- 1 Node
- 3 Pods:
  - "temp-sensor" (reads temperature)
  - "humidity-sensor" (reads humidity)
  - "pressure-sensor" (reads barometric pressure)
- 3 Services (one per sensor, exposes metrics)

**Flash Usage:**
- ECIs (3 total): 6MB
- Time-series data: 1GB (days/weeks of samples)

**K8s Integration:**
- Prometheus scrapes metrics from service endpoints
- Grafana displays sensor dashboards
- AlertManager triggers on thresholds

### Use Case 3: Physical Ingress Controller

**Purpose:** Route physical signals based on digital rules

**Resources Implemented:**
- 1 Node
- 1 Pod ("ingress-controller")
- 1 Ingress resource (routing rules)

**Ingress Manifest:**
```yaml
apiVersion: networking.k8s.io/v1
kind: Ingress
metadata:
  name: gpio-ingress
spec:
  rules:
  - host: "button-a.local"
    http:
      paths:
      - path: /
        backend:
          service:
            name: led-matrix
            port: 80
```

**Behavior:**
- Physical button press (GPIO input) → triggers HTTP request to led-matrix service
- Ingress rules stored in flash
- Routes updated via ConfigMap

### Use Case 4: Deployment with Replica Management

**Purpose:** Show how ECI updates work

**Resources Implemented:**
- 1 Node
- 1 Deployment ("app-deployment")
- 3 Pods (replicas - simulated, but useful for demonstration)

**Deployment Manifest:**
```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: app-deployment
spec:
  replicas: 3
  template:
    spec:
      containers:
      - name: app
        image: pico/app:v2
```

**Flash Usage:**
- Current ECI (v1): 2MB
- New ECI (v2): 2MB (staged)
- Rollback ECI: 2MB (previous version)

**Behavior:**
- K8s updates deployment to v2
- Pico stages new ECI in flash
- Reboots to new ECI
- Reports new version to K8s
- Old ECI kept for rollback

## Flash Memory Management

### Partition Layout (2GB Total)

```
Address Range          Size    Purpose
─────────────────────────────────────────────────────────
0x00000000-0x00400000   4MB    Active firmware
0x00400000-0x00800000   4MB    Backup firmware (rollback)
0x00800000-0x01000000   8MB    ECI registry (embedded container images)
0x01000000-0x02000000  16MB    Manifests & configs
0x02000000-0x10000000 224MB    Application data
0x10000000-0x80000000   1.7GB  User data / logs
```

### Flash API

```c
// Flash management
int flash_init_partitions(void);
int flash_write_eci(const char *name, const uint8_t *data, size_t size);
firmware_t* flash_lookup_eci(const char *name);
int flash_erase_partition(flash_partition_t partition);

// Persistence
int flash_save_manifest(const char *name, const char *json);
char* flash_load_manifest(const char *name);

// Data storage (for pods)
int flash_write_data(const char *key, const uint8_t *data, size_t size);
int flash_read_data(const char *key, uint8_t *buffer, size_t size);
```

## Implementation Phases (Revised)

### Phase 1: HTTP Communication via TLS Proxy (Issue #1)
**Goal:** Get basic node operations working

**Deliverables:**
- Working HTTP client layer (plain HTTP to nginx proxy)
- Node registration
- Node status with proper capacity/allocatable reporting
- ConfigMap watching
- nginx TLS proxy setup on k3s server

**Files:**
- `src/k3s_client.c` - HTTP client implementation
- `src/node_status.c` - Enhanced status reporting
- `docs/NGINX-PROXY-SETUP.md` - Proxy configuration guide

**Architecture Decision:**
Pico uses HTTP-only communication to an nginx proxy on the k3s server. The proxy terminates TLS and forwards to the k3s API. This design choice is documented in `docs/TLS-PROXY-RATIONALE.md` and is critical to keeping the Pico implementation simple and memory-efficient.

### Phase 2: Flash Storage & ECI Registry (New Issue #2)
**Goal:** Enable multi-firmware support

**Deliverables:**
- Flash partition manager
- ECI registry (store embedded container images)
- Manifest persistence
- Data storage API

**Files:**
- `src/flash_manager.c` (NEW)
- `include/flash_manager.h` (NEW)

### Phase 3: Resource Manager Framework (New Issue #3)
**Goal:** Pluggable resource architecture

**Deliverables:**
- Resource interface definition
- Resource registration/discovery
- Resource status aggregation
- ConfigMap routing to resources

**Files:**
- `src/resource_manager.c` (NEW)
- `include/k8s_resource.h` (NEW)

### Phase 4: Pod Implementation (Issue #4)
**Goal:** First real resource type

**Deliverables:**
- Pod lifecycle management
- Container simulation
- Resource limits enforcement
- Pod status reporting to K8s

**Files:**
- `src/pod_manager.c` (NEW)
- Examples: `src/pods/gpio_pod.c`, `src/pods/sensor_pod.c`

### Phase 5: Additional Resource Types (Issue #5+)
**Goal:** Demonstrate flexibility

**Deliverables:**
- Service implementation
- Deployment implementation
- Ingress implementation
- Custom resource example

## Answers to Your Questions

### Q1: Do node status updates include resource utilization?

**Yes!** Node status must report:
```yaml
status:
  capacity:        # What the hardware can provide
    cpu: "2"
    memory: "264Ki"
    pods: "10"
    ephemeral-storage: "2Gi"  # The 2GB flash!

  allocatable:     # What K8s can schedule
    cpu: "1800m"
    memory: "200Ki"
    pods: "10"
    ephemeral-storage: "1900Mi"

  # Real-time utilization (updated every 10s)
  conditions:
    - type: MemoryPressure
      status: "True/False"  # Based on actual SRAM usage
    - type: DiskPressure
      status: "True/False"  # Based on flash usage
```

We track and report REAL values for SRAM and flash, fake plausible CPU values.

### Q2: How do we fake registry/container management?

**Answer:**
- **ECI Registry:** Flash storage at 0x00800000 contains embedded container images (ECIs)
- **ECI Pull:** Copy from flash to staging area, simulate delay
- **Container Creation:** Function pointer + state structure
- **Container Runtime:** Use second core for actual parallel execution!
- **Resource Accounting:** Track per-container SRAM usage, enforce limits

### Q3: How to use the 2GB flash?

**Answer:**
- Report as "ephemeral-storage" to K8s (accurate!)
- Store embedded container images / ECIs (enable multi-pod scenarios)
- Store manifests/configs persistently
- Store application data (sensor logs, state history, etc.)
- Lazy load into SRAM as needed

### Q4: Beyond pods - other resource types?

**Yes! The architecture supports:**
- Pods (most common)
- Services (network abstractions)
- Deployments (managed replicas)
- Ingress (routing rules)
- StatefulSets (if we add persistence)
- Custom Resources (user-defined)

Each firmware build chooses which resources to implement.

## Decision for Issue #1

**For HTTP communication implementation (Issue #1), we do NOT need:**
- Resource manager framework (comes in Phase 3)
- Pod abstractions (comes in Phase 4)
- Flash partitioning (comes in Phase 2)
- TLS implementation on the Pico (using HTTP-only via proxy)

**For Issue #1, we DO need:**
- Generic `k3s_request()` function for plain HTTP (already designed this way ✅)
- Enhanced node status reporting (add capacity/allocatable/conditions)
- nginx TLS proxy setup on k3s server

**Key Design Decision:**
Keep Layer 2 (k3s_client.c) completely resource-agnostic. It doesn't know about pods vs services vs ingress. It just sends HTTP requests to the nginx proxy.

The Pico uses **HTTP-only communication**. TLS termination happens at the nginx proxy on the k3s server. This architectural decision is critical for:
1. Memory efficiency (saves ~40KB RAM by eliminating mbedtls on Pico)
2. Simplicity (plain HTTP debugging)
3. Compatibility (avoids mbedtls/Go TLS issues)
4. Performance (no TLS handshake overhead)

See `docs/TLS-PROXY-RATIONALE.md` for the complete rationale.

This design is ALREADY correct in the current code! We just need to:
1. Implement the HTTP transport (connecting to nginx proxy)
2. Enhance `src/node_status.c` to report capacity/allocatable/conditions properly
3. Set up nginx TLS proxy on k3s server

## Next Steps

1. **Approve this architecture** - Does this align with your vision?
2. **Focus on Issue #1** - Implement TLS, keep it generic
3. **Plan Issue #2** - Flash storage management
4. **Plan Issue #3** - Resource manager framework
5. **Iterate from there**

## Questions for You

1. Does this flexibility match your vision?
2. Should we support multiple resources in one firmware, or one resource per firmware build?
3. Priority order: Pods → Services → Deployments → Ingress?
4. Should we create a "firmware builder" tool that compiles different resource types?

Ready to proceed with TLS implementation once you approve the architecture!
