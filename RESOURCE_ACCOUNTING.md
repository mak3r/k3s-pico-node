# Resource Accounting for K8s Pico Node

## What We Need (Not cgroups!)

### 1. Track Resource Usage Per Container

```c
typedef struct {
    char *container_name;

    // Memory tracking
    uint32_t memory_allocated;      // Bytes currently allocated
    uint32_t memory_limit;          // Max allowed (from pod spec)
    uint32_t memory_peak;           // Peak usage

    // CPU tracking (simplified)
    uint32_t cpu_time_ms;           // Total CPU time used
    uint32_t cpu_limit_millicores;  // Max allowed

    // Flash/storage tracking
    uint32_t storage_used;          // Flash space used
    uint32_t storage_limit;         // Max allowed
} container_resources_t;
```

### 2. Enforce Resource Limits

```c
// When container tries to allocate memory
void* container_malloc(container_t *c, size_t size) {
    container_resources_t *res = c->resources;

    // Check if allocation would exceed limit
    if (res->memory_allocated + size > res->memory_limit) {
        // Log OOM event
        printf("Container %s: Memory limit exceeded\n", c->name);

        // Report to K8s: Pod Failed (OOMKilled)
        pod_set_status(c->pod, POD_STATUS_FAILED, "OOMKilled");

        return NULL;
    }

    // Allocate from global heap
    void *ptr = malloc(size);
    if (ptr) {
        res->memory_allocated += size;
        if (res->memory_allocated > res->memory_peak) {
            res->memory_peak = res->memory_allocated;
        }
    }

    return ptr;
}

void container_free(container_t *c, void *ptr, size_t size) {
    free(ptr);
    c->resources->memory_allocated -= size;
}
```

### 3. Report Usage to K8s (Optional - Metrics API)

K8s can query metrics from `/stats/summary` endpoint:

```c
// Kubelet stats endpoint
int kubelet_get_stats_summary(char *response, size_t size) {
    // Aggregate all container stats
    snprintf(response, size,
        "{"
        "  \"node\": {"
        "    \"memory\": {\"usageBytes\": %u},"
        "    \"cpu\": {\"usageNanoCores\": %u}"
        "  },"
        "  \"pods\": ["
        "    {"
        "      \"podRef\": {\"name\": \"gpio-controller\"},"
        "      \"containers\": ["
        "        {"
        "          \"name\": \"gpio\","
        "          \"memory\": {\"usageBytes\": %u},"
        "          \"cpu\": {\"usageNanoCores\": %u}"
        "        }"
        "      ]"
        "    }"
        "  ]"
        "}",
        get_total_memory_usage(),
        get_total_cpu_usage(),
        container->resources->memory_allocated,
        container->resources->cpu_time_ms * 1000000  // Convert to nanocores
    );

    return 0;
}
```

**Note:** Metrics API is optional! Many simple Kubernetes setups work fine without it. The control plane only requires pod status (Running/Failed), not live metrics.

## What We DON'T Need

### ❌ Isolation Mechanisms

**Real cgroups provide:**
- PID namespaces (process isolation)
- Mount namespaces (filesystem isolation)
- Network namespaces (network isolation)
- User namespaces (user isolation)

**Why we don't need this:**
- Single firmware image - no untrusted code
- No multi-tenancy concerns
- Pico can't run arbitrary workloads
- Everything is cooperative, not adversarial

### ❌ Complex CPU Scheduling

**Real cgroups provide:**
- CPU shares (relative priority)
- CPU quotas (hard limits)
- CPU bandwidth control

**What we do instead:**
- Simple round-robin or priority scheduling
- Or use core1 for one container, core0 for another
- Track total CPU time for metrics reporting

### ❌ I/O Throttling

**Real cgroups provide:**
- Block I/O limits
- Network bandwidth limits

**What we do instead:**
- Flash I/O is fast enough, no throttling needed
- Network is WiFi - bandwidth is external constraint
- Can track bytes sent/received for metrics

## Simplified Implementation

### Memory Accounting (Most Important)

```c
// Global heap allocator wrapper
#define HEAP_SIZE (200 * 1024)  // 200KB usable
static uint8_t heap[HEAP_SIZE];
static uint32_t heap_used = 0;

typedef struct {
    container_t *owner;
    size_t size;
} alloc_header_t;

void* tracked_malloc(container_t *container, size_t size) {
    // Check global heap space
    if (heap_used + size + sizeof(alloc_header_t) > HEAP_SIZE) {
        return NULL;  // Out of memory globally
    }

    // Check container limit
    if (container->resources->memory_allocated + size >
        container->resources->memory_limit) {
        // Container exceeded its limit
        pod_set_status(container->pod, POD_STATUS_FAILED, "OOMKilled");
        return NULL;
    }

    // Allocate
    void *ptr = /* allocate from heap */;

    // Track allocation
    alloc_header_t *header = (alloc_header_t *)ptr;
    header->owner = container;
    header->size = size;

    container->resources->memory_allocated += size;
    heap_used += size + sizeof(alloc_header_t);

    return (void *)((uint8_t *)ptr + sizeof(alloc_header_t));
}
```

### CPU Accounting (Nice to Have)

```c
// Simple time tracking per container
void container_tick(container_t *c) {
    // Called from timer interrupt or main loop
    if (c->running) {
        c->resources->cpu_time_ms++;
    }
}

// Or more sophisticated: track actual core usage
uint32_t get_container_cpu_percent(container_t *c) {
    uint32_t total_time_ms = get_uptime_ms();
    if (total_time_ms == 0) return 0;

    return (c->resources->cpu_time_ms * 100) / total_time_ms;
}
```

### Flash/Storage Accounting (If Needed)

```c
// Track flash usage per container
int container_write_flash(container_t *c, const char *key,
                          const void *data, size_t size) {
    // Check container's storage limit
    if (c->resources->storage_used + size > c->resources->storage_limit) {
        printf("Container %s: Storage limit exceeded\n", c->name);
        return -1;
    }

    // Write to flash
    int ret = flash_write_data(key, data, size);
    if (ret == 0) {
        c->resources->storage_used += size;
    }

    return ret;
}
```

## What K8s Scheduler Needs to Know

When scheduler decides where to place a pod:

```yaml
# Pod requests resources
spec:
  containers:
  - name: gpio
    resources:
      requests:
        memory: "64Ki"
        cpu: "500m"
      limits:
        memory: "128Ki"
        cpu: "1000m"
```

**Scheduler logic:**
1. Looks at node's `allocatable` resources
2. Checks if node has enough FREE resources
3. If yes, schedules pod to node
4. Kubelet receives pod assignment
5. Kubelet tries to start pod

**Our kubelet must:**
1. Receive pod assignment (watch K8s API)
2. Check if resources are available
3. If not enough memory: Set pod status to `Failed` with reason "Insufficient memory"
4. If enough memory: Start container, track usage
5. If container exceeds limits later: Kill it, set status to `Failed` with reason "OOMKilled"

## Reporting to K8s

### Pod Status (Required)

```json
{
  "status": {
    "phase": "Running",  // or "Failed"
    "conditions": [
      {
        "type": "Ready",
        "status": "True"
      }
    ],
    "containerStatuses": [
      {
        "name": "gpio",
        "state": {
          "running": {
            "startedAt": "2026-01-20T22:00:00Z"
          }
        },
        "ready": true,
        "restartCount": 0
      }
    ]
  }
}
```

### Resource Usage (Optional - Metrics API)

Only needed if you want:
- `kubectl top nodes`
- `kubectl top pods`
- Prometheus metrics scraping
- HPA (Horizontal Pod Autoscaler) - probably not relevant

Can be added in a future phase!

## Implementation Priority for Issue #1

**For TLS implementation, we need:**

1. ✅ **Node registration** - Report capacity/allocatable
2. ✅ **Node status updates** - Report conditions (Ready, MemoryPressure, etc.)
3. ❌ **Container resource tracking** - Not yet, comes in Phase 4 (Pod implementation)
4. ❌ **Metrics API** - Optional, can add later

**Enhanced node status (for Issue #1):**

```c
// In node_status.c
int node_status_report(void) {
    // Get real values
    uint32_t total_sram = 264 * 1024;
    uint32_t used_sram = get_heap_used();
    uint32_t free_sram = total_sram - used_sram;

    bool memory_pressure = (used_sram * 100 / total_sram) > 85;

    // Build status JSON
    snprintf(json_buffer, sizeof(json_buffer),
        "{"
        "  \"status\": {"
        "    \"capacity\": {"
        "      \"cpu\": \"2\","
        "      \"memory\": \"264Ki\","
        "      \"pods\": \"10\","
        "      \"ephemeral-storage\": \"2Gi\""
        "    },"
        "    \"allocatable\": {"
        "      \"cpu\": \"1800m\","
        "      \"memory\": \"200Ki\","
        "      \"pods\": \"10\","
        "      \"ephemeral-storage\": \"1900Mi\""
        "    },"
        "    \"conditions\": ["
        "      {\"type\": \"Ready\", \"status\": \"True\"},"
        "      {\"type\": \"MemoryPressure\", \"status\": \"%s\"},"
        "      {\"type\": \"DiskPressure\", \"status\": \"False\"}"
        "    ]"
        "  }"
        "}",
        memory_pressure ? "True" : "False"
    );

    // Send to K8s API
    return k3s_patch("/api/v1/nodes/" K3S_NODE_NAME "/status", json_buffer);
}
```

## Summary

### What We Concluded:

✅ **Don't need to fake cgroups** - They're a Linux implementation detail

✅ **Do need resource accounting** - Track memory/CPU/storage per container

✅ **Do need limit enforcement** - Fail pods that exceed limits

❌ **Don't need isolation** - No security concerns with known firmware

❌ **Don't need complex scheduling** - Simple round-robin is fine

### For Issue #1 (TLS Implementation):

**Only need to add:**
- Enhanced node status reporting (capacity, allocatable, conditions)
- Real memory pressure calculation

**Don't need yet:**
- Container resource tracking (Phase 4)
- Metrics API (Phase 5+)

### Future Issues Will Add:

- **Issue #4 (Pod Implementation):** Container resource tracking
- **Issue #6 (Metrics API):** Optional metrics endpoint for `kubectl top`

The architecture document is correct - we just don't need to call it "cgroups". It's simply "resource accounting and enforcement."
