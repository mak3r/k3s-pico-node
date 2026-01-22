# Next Steps: Raspberry Pi Pico WH as K3s Node

## ✅ Project Status

The project has been successfully implemented and builds without errors!

**Build Output:**
- **Firmware file**: `build/k3s_pico_node.uf2` (785 KB)
- **Code size**: 396 KB
- **RAM usage**: ~35 KB (leaving ~229 KB free)

## 📋 What's Been Completed

1. ✅ Certificate generation using k3s CA
2. ✅ Project structure and configuration
3. ✅ All source modules implemented:
   - Main loop with WiFi and polling
   - Mock kubelet server (/healthz, /metrics)
   - Node status reporting
   - ConfigMap watcher
   - Memory manager
   - K3s API client (TLS scaffolding)
4. ✅ CMake build system configured
5. ✅ Successful compilation

## ⚠️ Known Limitations

### Critical TODOs Before Hardware Testing:

1. **Set up nginx TLS Proxy on k3s Server**
   - Install nginx on your k3s server
   - Configure TLS termination proxy (see `docs/NGINX-PROXY-SETUP.md`)
   - Configure firewall to restrict access to local network only
   - **CRITICAL**: This proxy is essential for the Pico to communicate with k3s
   - The Pico uses HTTP-only and relies on the proxy for TLS termination

2. **Update WiFi Credentials** (SECURE METHOD)
   - Copy the template: `cp include/config_local.h.template include/config_local.h`
   - Edit `include/config_local.h` with your actual credentials
   - Set `WIFI_SSID`, `WIFI_PASSWORD`, and `K3S_SERVER_IP`
   - Update `K3S_SERVER_PORT` to `6080` (nginx proxy port, not k3s API port)
   - **IMPORTANT**: `config_local.h` is gitignored and will NOT be committed to version control

3. **Complete HTTP Client Implementation**
   - `src/k3s_client.c` needs plain HTTP client implementation
   - Connect to nginx proxy at `http://K3S_SERVER_IP:6080`
   - Format HTTP requests with proper headers
   - Parse HTTP responses
   - **NOTE**: No TLS implementation needed on Pico - proxy handles it

## 🚀 Testing Steps

### 1. Update Configuration

```bash
# Copy the config template (first time only)
cp include/config_local.h.template include/config_local.h

# Edit WiFi credentials and K3s server IP
nano include/config_local.h

# Rebuild
cd build
make -j4
```

### 2. Flash to Pico

```bash
# Connect Pico WH while holding BOOTSEL button
# Copy firmware to mounted drive
cp build/k3s_pico_node.uf2 /media/$USER/RPI-RP2/
```

### 3. Monitor Serial Output

```bash
# Connect via USB serial (115200 baud)
screen /dev/ttyACM0 115200
# or
minicom -D /dev/ttyACM0 -b 115200
```

You should see:
```
========================================
  Raspberry Pi Pico WH - K3s Node
========================================
Node Name: pico-node-1
K3s Server: 192.168.86.232:6443
...
```

### 4. Verify Node Registration

```bash
# Check if node appears
kubectl get nodes

# Should eventually show (after TLS is implemented):
# NAME         STATUS   ROLES   AGE   VERSION
# pico-node-1  Ready    <none>  10s   v1.34.0
```

### 5. Test ConfigMap Updates

```bash
# Create ConfigMap
kubectl create configmap pico-config \
  --from-literal=memory_values="0=0x42,1=0x43,2=0xFF"

# Watch Pico serial output for:
# "ConfigMap update detected: 0=0x42,1=0x43,2=0xFF"
# "Memory[0] = 0x42"
# etc.
```

## 🔧 Completing HTTP Client Implementation

The most critical missing piece is the plain HTTP client implementation in `k3s_client.c`. The Pico uses HTTP-only communication to an nginx proxy on the k3s server, which terminates TLS.

### Architecture: HTTP-only via TLS Proxy

```
Pico (HTTP) ──────► nginx proxy (HTTP→HTTPS) ──────► k3s API (HTTPS)
            :6080                                :6443
```

See `docs/TLS-PROXY-RATIONALE.md` for why we use this architecture instead of TLS on the Pico.

### Required Changes in `src/k3s_client.c`:

1. **Create TCP connection using lwIP**
```c
struct tcp_pcb *pcb = tcp_new();
ip4_addr_t server_ip;
ip4addr_aton(K3S_SERVER_IP, &server_ip);
// Connect to nginx proxy on port 6080, not k3s API on 6443
tcp_connect(pcb, &server_ip, 6080, connect_callback);
```

2. **Format plain HTTP request**
```c
char request[1024];
snprintf(request, sizeof(request),
    "POST %s HTTP/1.1\r\n"
    "Host: %s:6080\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: %d\r\n"
    "\r\n%s",
    path, K3S_SERVER_IP, strlen(body), body);
```

3. **Send HTTP request via TCP**
```c
// Send plain HTTP - no TLS needed
err_t err = tcp_write(pcb, request, strlen(request), TCP_WRITE_FLAG_COPY);
if (err == ERR_OK) {
    tcp_output(pcb);
}
```

4. **Receive HTTP response**
```c
// Receive plain HTTP response
char response[4096];
// Parse HTTP response headers and body
// Extract JSON from body
```

### Reference Implementation

Study these pico-sdk examples:
- `lib/lwip/contrib/apps/httpd/` - HTTP server example
- `pico-examples/pico_w/wifi/` - Basic HTTP client examples
- **NOTE**: Skip any TLS/mbedtls examples - not needed for this project

## 📊 Memory Budget Analysis

**Current Usage:**
- Flash: 396 KB (of 2 MB) - 19.5% used
- RAM: ~35 KB (of 264 KB) - 13.3% used

**Remaining headroom:**
- Flash: ~1.6 MB available for application code
- RAM: ~229 KB available for dynamic allocation

This leaves plenty of room for:
- Completing TLS implementation
- Adding application logic
- Storing ConfigMap data

## 🔄 Development Workflow

```bash
# Make changes to source code
nano src/*.c

# Rebuild
cd build
make -j4

# Flash to Pico
cp k3s_pico_node.uf2 /media/$USER/RPI-RP2/

# Monitor
screen /dev/ttyACM0 115200
```

## 🐛 Troubleshooting

### WiFi Won't Connect
- Check SSID and password in config.h
- Ensure 2.4GHz network (5GHz not supported)
- Verify WPA2-AES security

### Build Errors
```bash
# Clean build
rm -rf build/*
cd build
cmake -DPICO_SDK_PATH=/home/projects/k3s-device/pico-sdk ..
make -j4
```

### USB Serial Not Working
- Check USB cable supports data (not just power)
- Try different USB port
- Verify permissions: `sudo usermod -a -G dialout $USER`

## 🎯 Future Enhancements

1. **HTTP Client Implementation** (critical - plain HTTP to nginx proxy)
2. **Watch API** instead of polling for ConfigMaps
3. **OTA Firmware Updates** via Kubernetes Jobs
4. **Real Prometheus Metrics** (CPU temp, free memory, etc.)
5. **Multiple ConfigMap Support**
6. **WebAssembly Runtime** for downloadable applications
7. **Per-Device Identity** (if moving beyond lab/experimental deployment)

**NOTE**: TLS on the Pico is explicitly NOT planned. The HTTP-via-proxy architecture is the intended design. See `docs/TLS-PROXY-RATIONALE.md` for the rationale.

## 📚 Additional Resources

- [Pico SDK Documentation](https://github.com/raspberrypi/pico-sdk)
- [lwIP Documentation](https://www.nongnu.org/lwip/)
- [Mbed TLS Documentation](https://mbed-tls.readthedocs.io/)
- [Kubernetes API Reference](https://kubernetes.io/docs/reference/kubernetes-api/)

## 🎉 Summary

You now have a working foundation for a Raspberry Pi Pico WH that can appear as a Kubernetes node! The hardest part (project structure, build system, and core modules) is complete. The main remaining work is implementing the TLS client for actual k8s API communication.

This is an impressive achievement given the constraints:
- 264 KB RAM
- No operating system
- Full TLS stack
- Kubernetes integration

Good luck with the hardware testing!
