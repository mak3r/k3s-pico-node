# k3s-pico-node Architecture

## Overview

This project implements a Raspberry Pi Pico W as a Kubernetes node in a k3s cluster. The Pico cannot run containers, but it registers as a node, reports status, and receives configuration updates via ConfigMaps.

## System Architecture

### With TLS Proxy (Current Implementation)

```
┌─────────────────┐         ┌──────────────────────────────────┐
│                 │  HTTP   │  k3s Server (192.168.x.x)        │
│  Pico W         ├────────►│  ┌─────────────────┐             │
│  (HTTP Client)  │ :6080   │  │ nginx proxy     │  HTTPS      │
│                 │         │  │ (TLS term)      ├────────────► │
│                 │         │  └─────────────────┘   :6443     │
│                 │         │           │                       │
│  - WiFi         │         │           v                       │
│  - lwIP         │         │  ┌─────────────────┐             │
│  - HTTP Client  │         │  │ k3s API server  │             │
│  - Mock Kubelet │         │  │ localhost:6443  │             │
│    :10250       │         │  └─────────────────┘             │
└─────────────────┘         └──────────────────────────────────┘
      |                                       |
      |                                       |
      v                                       v
┌─────────────────┐                 ┌──────────────────┐
│ 1KB SRAM Region │                 │ k3s etcd         │
│ (ConfigMap Data)│                 │ (cluster state)  │
└─────────────────┘                 └──────────────────┘
```

### Component Breakdown

#### Pico W (Client Side)

**Hardware:**
- RP2040 microcontroller (133MHz ARM Cortex-M0+, 264KB SRAM, 2MB Flash)
- CYW43 WiFi chip (2.4GHz 802.11n)
- No hardware crypto acceleration
- No secure enclave or encrypted storage

**Software Stack:**
1. **WiFi Layer**: CYW43 driver from pico-sdk
2. **Network Stack**: lwIP (NO_SYS mode, polling-only)
3. **HTTP Client**: Custom implementation for K8s API requests
4. **Mock Kubelet**: HTTP server on port 10250 (`/healthz`, `/metrics`)
5. **K3s Client**: Node registration, status reporting, ConfigMap polling
6. **Memory Manager**: 1KB SRAM region controlled by ConfigMaps

**No TLS on Client**: See [TLS-PROXY-RATIONALE.md](TLS-PROXY-RATIONALE.md) for why we bypass client-side TLS.

#### k3s Server (Server Side)

**nginx TLS Proxy:**
- Listens on HTTP port 6080 (local network only)
- Terminates TLS with proper client certificate
- Forwards authenticated requests to k3s API at `https://localhost:6443`
- Handles WebSocket upgrades for Watch API (future)

**k3s API Server:**
- Standard k3s installation
- Listens on `https://localhost:6443`
- Receives authenticated requests from nginx proxy
- Stores node state in etcd

## Communication Flow

### Node Registration (Startup)

```
Pico                nginx              k3s API
 │                    │                  │
 │   POST /api/v1/nodes (JSON)           │
 ├───────────────────►│                  │
 │                    │  + Client cert   │
 │                    │  + TLS handshake │
 │                    ├─────────────────►│
 │                    │                  │
 │                    │◄─────────────────┤
 │                    │  201 Created     │
 │◄───────────────────┤                  │
 │   201 Created      │                  │
 │                    │                  │
```

### Status Reporting (Every 10s)

```
Pico                nginx              k3s API
 │                    │                  │
 │   PATCH /api/v1/nodes/{name}/status   │
 ├───────────────────►│                  │
 │   (status JSON)    │  + TLS           │
 │                    ├─────────────────►│
 │                    │◄─────────────────┤
 │◄───────────────────┤  200 OK          │
 │                    │                  │
```

### ConfigMap Polling (Every 30s)

```
Pico                nginx              k3s API
 │                    │                  │
 │   GET /api/v1/namespaces/default/configmaps │
 ├───────────────────►│                  │
 │                    │  + TLS           │
 │                    ├─────────────────►│
 │                    │◄─────────────────┤
 │◄───────────────────┤  200 OK + JSON   │
 │                    │                  │
 │  Parse "memory_values" field          │
 │  Update SRAM[0..1023] accordingly     │
 │                    │                  │
```

## Data Structures

### Node Object (Sent to k3s)

```json
{
  "apiVersion": "v1",
  "kind": "Node",
  "metadata": {
    "name": "pico-node-1",
    "labels": {
      "beta.kubernetes.io/arch": "arm",
      "beta.kubernetes.io/os": "rtos",
      "node.kubernetes.io/instance-type": "rp2040"
    }
  },
  "spec": {
    "podCIDR": "10.42.0.0/24",
    "providerID": "pico://pico-node-1"
  },
  "status": {
    "capacity": {
      "memory": "264Ki",
      "pods": "0"
    },
    "allocatable": {
      "memory": "264Ki",
      "pods": "0"
    },
    "conditions": [
      {
        "type": "Ready",
        "status": "True",
        "reason": "KubeletReady",
        "message": "Pico node is ready"
      }
    ],
    "addresses": [
      {
        "type": "InternalIP",
        "address": "192.168.x.x"
      }
    ],
    "nodeInfo": {
      "machineID": "pico-mac-address",
      "systemUUID": "pico-uuid",
      "bootID": "boot-id",
      "kernelVersion": "lwIP-2.1.2",
      "osImage": "Pico-SDK-1.5.1",
      "containerRuntimeVersion": "none",
      "kubeletVersion": "v1.28.0",
      "kubeProxyVersion": "v1.28.0",
      "operatingSystem": "rtos",
      "architecture": "arm"
    }
  }
}
```

### ConfigMap Format (Memory Updates)

```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: pico-config
  namespace: default
data:
  memory_values: "0=0x42,1=0x43,10=0xFF,100=0xAA"
  # Format: offset=value,offset=value,...
  # offset: 0-1023 (SRAM byte offset)
  # value: 0x00-0xFF (byte value in hex)
```

## Memory Layout

### RP2040 SRAM (264KB Total)

```
0x20000000 ┌────────────────────────┐
           │ Stack (~16KB)          │
           ├────────────────────────┤
           │ lwIP buffers (~40KB)   │
           │ - PBUF_POOL            │
           │ - TCP/UDP PCBs         │
           ├────────────────────────┤
           │ CYW43 driver (~15KB)   │
           ├────────────────────────┤
           │ HTTP buffers (~20KB)   │
           │ - Request buffer       │
           │ - Response buffer      │
           ├────────────────────────┤
           │ Application data       │
           │ - Node status          │
           │ - ConfigMap cache      │
           ├────────────────────────┤
           │ ConfigMap Memory       │
           │ Region (1KB)           │  ← User-accessible via ConfigMaps
           ├────────────────────────┤
           │ Heap (~150KB)          │
           │                        │
0x20042000 └────────────────────────┘
```

## Security Model

**See [TLS-PROXY-RATIONALE.md](TLS-PROXY-RATIONALE.md) for comprehensive security analysis.**

### What We Protect

1. **Network Boundary**:
   - WPA2/WPA3 WiFi encryption
   - Firewall restricts nginx proxy to local network only
   - k3s API not exposed to internet

2. **Server-Side TLS**:
   - k3s API uses proper TLS with certificate validation
   - nginx proxy uses k3s admin client certificate

3. **Application Layer**:
   - Kubernetes RBAC controls node permissions
   - ConfigMaps provide authorized configuration updates
   - Node names provide device identification

### What We Don't Protect

1. **Physical Access**:
   - Pico flash can be dumped with picotool
   - No secure boot or encrypted storage
   - WiFi credentials in plaintext on flash

2. **Network Eavesdropping**:
   - Pico-to-proxy traffic is HTTP (unencrypted)
   - Acceptable on trusted local network
   - WiFi WPA2 provides some protection

3. **Credential Sharing**:
   - All Picos effectively use same identity (nginx's client cert)
   - Per-device identity requires secure provisioning

## nginx Proxy Configuration

**File**: `/etc/nginx/sites-available/k3s-proxy`

```nginx
server {
    listen 6080;
    server_name _;

    # Only allow local network access
    # Adjust subnet to match your network
    allow 192.168.0.0/16;
    allow 10.0.0.0/8;
    deny all;

    location / {
        # Forward to local k3s API with TLS
        proxy_pass https://127.0.0.1:6443;

        # Use k3s admin certificate for authentication
        proxy_ssl_certificate /var/lib/rancher/k3s/server/tls/client-admin.crt;
        proxy_ssl_certificate_key /var/lib/rancher/k3s/server/tls/client-admin.key;
        proxy_ssl_trusted_certificate /var/lib/rancher/k3s/server/tls/server-ca.crt;
        proxy_ssl_verify on;

        # Standard proxy headers
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;

        # Kubernetes needs HTTP/1.1 for WebSocket upgrades
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";

        # No buffering for streaming responses
        proxy_buffering off;
    }
}
```

**Firewall Configuration** (firewalld):

```bash
# Open port 6080 for local zone only
sudo firewall-cmd --zone=internal --add-port=6080/tcp --permanent
sudo firewall-cmd --reload
```

## Polling Intervals

| Operation | Interval | Reason |
|-----------|----------|--------|
| Status reporting | 10s | K8s expects kubelet heartbeats every 10s |
| ConfigMap polling | 30s | Balance between responsiveness and network load |
| Kubelet health check | On-demand | Responds to k8s health probe requests |

## Error Handling

### Network Failures

- **WiFi disconnect**: Automatic reconnect with backoff
- **DNS failure**: Retry with timeout
- **HTTP timeout**: 30s timeout, retry on next interval
- **HTTP 4xx/5xx**: Log error, retry on next interval

### Memory Constraints

- **Allocation failure**: Drop request, log error, continue
- **Buffer overflow**: Truncate response, log warning
- **Stack overflow**: Watchdog reset (last resort)

### k3s API Errors

- **401 Unauthorized**: Fatal, requires certificate fix
- **404 Not Found**: Node not registered, attempt registration
- **409 Conflict**: Node exists, proceed to status updates
- **503 Unavailable**: Retry with exponential backoff

## Performance Characteristics

### Typical Request Latency

- **DNS resolution**: 50-200ms
- **TCP connection**: 10-50ms
- **HTTP request/response**: 100-500ms
- **Total end-to-end**: 200-750ms

### Memory Usage

- **Baseline**: ~100KB (lwIP + CYW43 + stack)
- **Per-request peak**: +30KB (HTTP buffers)
- **Available headroom**: ~134KB

### Network Usage

- **Node registration**: ~2KB (once at startup)
- **Status update**: ~1.5KB every 10s → ~150KB/day
- **ConfigMap poll**: ~0.5KB every 30s → ~1.4MB/day
- **Total**: ~1.6MB/day

## Future Enhancements

### Short Term

1. **Watch API**: Replace ConfigMap polling with Watch for real-time updates
2. **OTA Updates**: Firmware updates via k8s Jobs
3. **Metrics**: Real Prometheus metrics endpoint

### Long Term

1. **Per-Device Identity**: Secure provisioning with unique certificates
2. **Hardware Security**: Move to MCU with secure enclave (e.g., ESP32-S3)
3. **Encrypted Storage**: Protect credentials with hardware crypto
4. **Secure Boot**: Verify firmware signatures at boot

## Testing

### Unit Tests (Planned)

- HTTP request/response parsing
- JSON serialization/deserialization
- Memory manager boundary conditions
- ConfigMap parsing edge cases

### Integration Tests

- Node registration succeeds
- Status reporting continuous operation
- ConfigMap updates apply correctly
- Error recovery (network down, server restart)

### System Tests

- 24-hour endurance test
- Memory leak detection (1000+ requests)
- Load testing with 1s intervals
- Multiple Pico nodes simultaneously

## References

- [TLS Proxy Security Rationale](TLS-PROXY-RATIONALE.md)
- [Kubernetes Node API](https://kubernetes.io/docs/reference/generated/kubernetes-api/v1.28/#node-v1-core)
- [Kubelet API](https://kubernetes.io/docs/reference/command-line-tools-reference/kubelet/)
- [lwIP Documentation](https://www.nongnu.org/lwip/)
- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
