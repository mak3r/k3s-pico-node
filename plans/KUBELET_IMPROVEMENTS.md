# Kubelet Implementation Improvements

Based on Kubernetes expectations and best practices, this document outlines recommended improvements to the Pico kubelet implementation.

## Current Status

### ✅ What Works
- `/healthz` endpoint (returns "ok")
- `/metrics` endpoint (returns empty but valid Prometheus format)
- Node registration via Kubernetes API
- Periodic status updates (heartbeat)
- Correct node conditions reporting
- Proper capacity/allocatable resources

### ⚠️ What's Missing
- `/pods` endpoint (returns 404 instead of empty list)
- `/spec` endpoint (machine specification)
- Real Prometheus metrics
- Kubelet authentication
- TLS on kubelet port

## Priority 1: Critical Improvements

### 1. Implement `/pods` Endpoint

**Why**: The Kubernetes node lifecycle controller may query this endpoint. Returning 404 could cause warnings.

**What to return**:
```json
{
  "kind": "PodList",
  "apiVersion": "v1",
  "metadata": {},
  "items": []
}
```

**Implementation** (in `src/kubelet_server.c`):
```c
static const char *pods_response_header =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 74\r\n"
    "Connection: close\r\n"
    "\r\n";

static const char *pods_response_body =
    "{\"kind\":\"PodList\",\"apiVersion\":\"v1\",\"metadata\":{},\"items\":[]}";

// In kubelet_recv():
else if (strstr(conn->recv_buffer, "GET /pods") != NULL) {
    DEBUG_PRINT("Kubelet: GET /pods");
    // Send header
    tcp_write(pcb, pods_response_header, strlen(pods_response_header), TCP_WRITE_FLAG_COPY);
    // Send body
    tcp_write(pcb, pods_response_body, strlen(pods_response_body), TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
}
```

**Memory impact**: ~150 bytes static data

**Benefit**: Eliminates 404 errors in k8s logs, improves compatibility

---

### 2. Add Real Prometheus Metrics

**Why**: Monitoring systems expect meaningful data from `/metrics`

**What to include**:
- Free memory (from heap)
- Uptime
- WiFi signal strength
- Request counts
- Temperature (RP2040 internal sensor)

**Implementation**:
```c
// Generate metrics dynamically
int generate_metrics(char *buffer, size_t size) {
    // Get free heap
    extern char __StackLimit, __bss_end__;
    uint32_t free_heap = &__StackLimit  - &__bss_end__;

    // Get uptime
    uint32_t uptime_ms = to_ms_since_boot(get_absolute_time());

    return snprintf(buffer, size,
        "# HELP node_memory_free_bytes Free heap memory\n"
        "# TYPE node_memory_free_bytes gauge\n"
        "node_memory_free_bytes %u\n"
        "\n"
        "# HELP node_uptime_seconds Time since boot\n"
        "# TYPE node_uptime_seconds counter\n"
        "node_uptime_seconds %.2f\n"
        "\n"
        "# HELP kubelet_http_requests_total Total HTTP requests\n"
        "# TYPE kubelet_http_requests_total counter\n"
        "kubelet_http_requests_total{endpoint=\"healthz\"} %u\n"
        "kubelet_http_requests_total{endpoint=\"metrics\"} %u\n"
        "kubelet_http_requests_total{endpoint=\"pods\"} %u\n",
        free_heap,
        uptime_ms / 1000.0,
        healthz_request_count,
        metrics_request_count,
        pods_request_count
    );
}
```

**Memory impact**: ~500 bytes buffer + counters

**Benefit**: Real monitoring, early detection of issues

---

### 3. Implement `/spec` Endpoint

**Why**: Helps Kubernetes understand hardware capabilities

**What to return**:
```json
{
  "num_cores": 1,
  "cpu_frequency_khz": 133000,
  "memory_capacity": 270336,
  "machine_id": "rp2040-pico-wh",
  "system_uuid": "rp2040-pico-wh",
  "boot_id": "rp2040-pico-wh"
}
```

**Implementation**:
```c
static const char *spec_json_template =
    "{"
    "\"num_cores\":1,"
    "\"cpu_frequency_khz\":133000,"
    "\"memory_capacity\":270336,"
    "\"machine_id\":\"rp2040-pico-wh\","
    "\"system_uuid\":\"rp2040-pico-wh\","
    "\"boot_id\":\"rp2040-pico-wh\""
    "}";

// Build and send response
```

**Memory impact**: ~200 bytes

**Benefit**: Better scheduler integration (even though we don't run pods)

---

## Priority 2: Important Improvements

### 4. Add Request Logging/Metrics

Track which endpoints are being called and how often:

```c
typedef struct {
    uint32_t healthz_count;
    uint32_t metrics_count;
    uint32_t pods_count;
    uint32_t unknown_count;
    uint32_t total_count;
} kubelet_stats_t;

static kubelet_stats_t stats = {0};
```

Include these in Prometheus metrics for observability.

---

### 5. Implement Readiness vs Liveness

Currently `/healthz` is the only health check. Consider:

- `/healthz` - Overall health (always returns ok if running)
- `/readyz` - Ready to serve (checks WiFi, k8s connectivity)

**Implementation**:
```c
// Check if node is truly ready
static bool is_node_ready(void) {
    // Check WiFi is connected
    if (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
        return false;
    }

    // Check can reach k8s API (maybe cache last successful heartbeat)
    if (time_since_last_heartbeat > 60000) {
        return false;
    }

    return true;
}

// In handler:
else if (strstr(conn->recv_buffer, "GET /readyz") != NULL) {
    if (is_node_ready()) {
        response = healthz_response;  // "ok"
    } else {
        response = not_ready_response;  // "not ready"
    }
}
```

---

### 6. Better Error Responses

Return proper HTTP error codes with JSON error messages:

```c
static const char *error_response_template =
    "HTTP/1.1 %d %s\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: %d\r\n"
    "Connection: close\r\n"
    "\r\n"
    "{\"error\":\"%s\",\"code\":%d}";
```

Examples:
- 400 Bad Request - Malformed request
- 404 Not Found - Unknown endpoint
- 500 Internal Server Error - Node issues
- 503 Service Unavailable - Not ready

---

## Priority 3: Production Readiness

### 7. Add Authentication

Real kubelets use client certificates. For Pico, we could:

**Option A: Token-based auth**
```c
// Simple bearer token check
bool check_auth(const char *request) {
    const char *auth_header = strstr(request, "Authorization: Bearer ");
    if (auth_header == NULL) return false;

    char token[128];
    sscanf(auth_header, "Authorization: Bearer %127s", token);

    return strcmp(token, KUBELET_TOKEN) == 0;
}
```

**Option B: No auth, rely on network security**
- Firewall rules restricting kubelet port to k8s nodes only
- This is acceptable for lab/experimental deployment

---

### 8. Add TLS Support (via nginx proxy)

The current architecture uses HTTP-only to nginx proxy. For production:

**Current (Development)**:
```
k8s API ──HTTP──► Pico:10250
```

**Production (via proxy)**:
```
k8s API ──HTTPS──► nginx:10250 ──HTTP──► Pico:10251 (internal)
```

Set up nginx to:
1. Listen on 10250 with TLS
2. Forward to Pico on internal port 10251
3. Add authentication headers

---

### 9. Connection Limits & Rate Limiting

Protect against resource exhaustion:

```c
#define MAX_CONCURRENT_CONNECTIONS 5
static int active_connections = 0;

static err_t kubelet_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    if (active_connections >= MAX_CONCURRENT_CONNECTIONS) {
        DEBUG_PRINT("Connection limit reached, rejecting");
        tcp_abort(newpcb);
        return ERR_ABRT;
    }

    active_connections++;
    // ... rest of accept logic
}
```

---

### 10. Request Timeout Handling

Close connections that don't send data:

```c
#define REQUEST_TIMEOUT_MS 5000

typedef struct {
    struct tcp_pcb *pcb;
    char recv_buffer[512];
    int recv_len;
    bool response_sent;
    absolute_time_t accept_time;  // NEW
} kubelet_conn_t;

// In polling function (call from main loop):
void kubelet_server_check_timeouts(void) {
    // Iterate connections, close any older than timeout
}
```

---

## Priority 4: Advanced Features

### 11. Custom Pico Endpoints

Add Pico-specific endpoints for management:

- **`GET /pico/status`** - Detailed Pico status
  ```json
  {
    "wifi_rssi": -45,
    "free_memory": 229376,
    "uptime_seconds": 12345,
    "temperature_celsius": 42.5,
    "last_k8s_update": "2025-01-23T10:30:00Z"
  }
  ```

- **`GET /pico/memory`** - Memory manager state
  ```json
  {
    "memory_cells": [
      {"address": 0, "value": 66},
      {"address": 1, "value": 67}
    ],
    "last_update": "2025-01-23T10:25:00Z"
  }
  ```

- **`POST /pico/command`** - Execute Pico commands
  ```json
  {
    "command": "blink_led",
    "params": {"times": 5, "delay_ms": 100}
  }
  ```

---

### 12. Structured Logging

Replace DEBUG_PRINT with structured logging:

```c
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level_t;

void log_request(log_level_t level, const char *endpoint,
                const char *client_ip, int response_code) {
    printf("[%s] endpoint=%s client=%s status=%d\n",
           log_level_string(level), endpoint, client_ip, response_code);
}
```

---

### 13. Health Check Details

Make `/healthz` return detailed status when queried with `?verbose=1`:

```
GET /healthz?verbose=1

Response:
{
  "status": "ok",
  "checks": {
    "wifi": "connected",
    "k8s_api": "reachable",
    "memory": "healthy",
    "uptime": 12345
  }
}
```

---

## Implementation Roadmap

### Phase 1: Essential (Week 1)
- [ ] Implement `/pods` endpoint returning empty list
- [ ] Add basic Prometheus metrics (memory, uptime)
- [ ] Implement `/spec` endpoint
- [ ] Add request counting

### Phase 2: Reliability (Week 2)
- [ ] Add `/readyz` endpoint
- [ ] Implement connection limits
- [ ] Add request timeout handling
- [ ] Better error responses with JSON

### Phase 3: Production (Week 3)
- [ ] Add authentication (token-based)
- [ ] Implement structured logging
- [ ] Add custom `/pico/*` endpoints
- [ ] Performance profiling and optimization

### Phase 4: Advanced (Future)
- [ ] TLS support via nginx proxy
- [ ] Rate limiting
- [ ] Detailed health checks
- [ ] Metrics aggregation and caching

---

## Testing Each Improvement

For each improvement, add corresponding tests:

1. **Unit tests**: Test response generation
2. **Integration tests**: Test against real k8s
3. **Load tests**: Test with multiple concurrent requests
4. **Failure tests**: Test error conditions

Example test for `/pods` endpoint:
```bash
# Should return 200 with empty pod list
response=$(curl -s http://pico-ip:10250/pods)
echo "$response" | jq -e '.kind == "PodList"'
echo "$response" | jq -e '.items | length == 0'
```

---

## Memory Budget

Estimated memory usage for improvements:

| Feature | Flash | RAM |
|---------|-------|-----|
| `/pods` endpoint | 200 B | 100 B |
| Prometheus metrics (dynamic) | 500 B | 1 KB |
| `/spec` endpoint | 200 B | 100 B |
| Request stats | 100 B | 64 B |
| Authentication | 1 KB | 256 B |
| Custom endpoints | 2 KB | 512 B |
| **Total** | **~4 KB** | **~2 KB** |

This is well within the Pico's capabilities (1.6 MB flash, 229 KB RAM available).

---

## Summary

The current kubelet implementation meets the **minimum requirements** for a valid Kubernetes node. The improvements above would:

✅ **Priority 1**: Eliminate error messages, improve monitoring
✅ **Priority 2**: Better observability and reliability
✅ **Priority 3**: Production-ready security and robustness
✅ **Priority 4**: Advanced management capabilities

Start with Priority 1 items for immediate benefit, then progress through the priorities based on your deployment needs.
