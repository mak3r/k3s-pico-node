# Raspberry Pi Pico WH as K3s Node

This project enables a Raspberry Pi Pico WH microcontroller to appear as a node in a k3s Kubernetes cluster. The Pico cannot run actual containers, but it can register as a node, report status, and receive configuration updates via ConfigMaps.

## Features

- **Node Registration**: Registers with k3s cluster and appears in `kubectl get nodes`
- **Status Reporting**: Reports node status every 10 seconds
- **Mock Kubelet**: Implements minimal kubelet endpoints (`/healthz`, `/metrics`)
- **ConfigMap Integration**: Polls ConfigMaps and updates designated memory regions
- **Memory-Optimized**: Runs within RP2040's 264KB RAM constraint

## Architecture

This project uses a **TLS proxy architecture** where the Pico sends HTTP requests to an nginx proxy on the k3s server, which terminates TLS and forwards to the k3s API. This approach bypasses TLS on the resource-constrained Pico while maintaining secure communication with the k3s API.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for detailed architecture and [docs/TLS-PROXY-RATIONALE.md](docs/TLS-PROXY-RATIONALE.md) for security rationale.

```
┌─────────────────┐         ┌──────────────────────────────────┐
│                 │  HTTP   │  k3s Server                      │
│  Pico W         ├────────►│  ┌─────────────────┐             │
│  (HTTP Client)  │ :6080   │  │ nginx proxy     │  HTTPS      │
│                 │         │  │ (TLS term)      ├────────────► │
│                 │         │  └─────────────────┘   :6443     │
│                 │         │           │                       │
│  ConfigMap      │         │           v                       │
│  Watcher        │◄────────┤  ┌─────────────────┐             │
│                 │  Poll   │  │ k3s API server  │             │
│  Mock Kubelet   │         │  │ localhost:6443  │             │
│    :10250       │         │  └─────────────────┘             │
│                 │         │                                  │
│  Memory Region  │         │                                  │
│    (1KB SRAM)   │         │                                  │
└─────────────────┘         └──────────────────────────────────┘
```

## Prerequisites

- **Hardware**: Raspberry Pi Pico WH (with WiFi)
- **K3s Cluster**: Running k3s cluster with API access
- **nginx**: Installed on k3s server for TLS proxy
- **Toolchain**: ARM cross-compiler, CMake, pico-sdk
- **Network**: Pico and k3s server on same network (WPA2/WPA3 protected)

## Hardware Testing

Before building the full firmware, it's recommended to verify your Pico WH hardware is working correctly using the test firmware in the `test-blink/` subdirectory.

See [test-blink/README.md](test-blink/README.md) for instructions on:
- Building the minimal LED blink test
- Verifying USB serial communication
- Confirming WiFi chip initialization
- Troubleshooting hardware issues

The test firmware takes only a few minutes to build and flash, and can save time by identifying hardware problems before attempting the full K3s node firmware.

## nginx TLS Proxy Setup

The Pico sends HTTP requests to an nginx proxy on your k3s server, which terminates TLS and forwards to the k3s API. This must be configured before the Pico can communicate with k3s.

### Install nginx

```bash
# On k3s server
sudo apt update
sudo apt install -y nginx
```

### Configure the Proxy

Create the proxy configuration:

```bash
sudo tee /etc/nginx/sites-available/k3s-proxy > /dev/null <<'EOF'
server {
    listen 6080;
    server_name _;

    # Only allow local network access
    allow 192.168.0.0/16;
    allow 10.0.0.0/8;
    deny all;

    location / {
        proxy_pass https://127.0.0.1:6443;
        proxy_ssl_certificate /var/lib/rancher/k3s/server/tls/client-admin.crt;
        proxy_ssl_certificate_key /var/lib/rancher/k3s/server/tls/client-admin.key;
        proxy_ssl_trusted_certificate /var/lib/rancher/k3s/server/tls/server-ca.crt;
        proxy_ssl_verify on;

        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;

        # Kubernetes needs these
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";

        # No buffering for streaming
        proxy_buffering off;
    }
}
EOF

# Enable the site
sudo ln -s /etc/nginx/sites-available/k3s-proxy /etc/nginx/sites-enabled/
sudo nginx -t  # Test configuration
sudo systemctl restart nginx
```

### Configure Firewall

**Important**: Only allow local network access to port 6080.

```bash
# For firewalld (openSUSE, RHEL, Fedora)
sudo firewall-cmd --zone=internal --add-port=6080/tcp --permanent
sudo firewall-cmd --reload

# For ufw (Ubuntu, Debian)
sudo ufw allow from 192.168.0.0/16 to any port 6080 proto tcp
sudo ufw reload
```

### Test the Proxy

```bash
# From k3s server (should work)
curl -v http://localhost:6080/version

# From Pico network (should work if firewall configured correctly)
curl -v http://192.168.x.x:6080/version
```

You should see k3s version information in JSON format.

## Pico Configuration

Before building, create your local configuration file with WiFi credentials:

```bash
# Copy the template
cp include/config_local.h.template include/config_local.h

# Edit with your actual credentials
nano include/config_local.h
```

Update the following values in `include/config_local.h`:

```c
#define WIFI_SSID         "your-wifi-ssid"
#define WIFI_PASSWORD     "your-wifi-password"
#define K3S_SERVER_IP     "192.168.1.100"  // Your K3s server IP
```

**Security Note**: `config_local.h` is gitignored and will not be committed to version control. Never commit credentials to the repository.

## Building

```bash
# Set up environment
export PICO_SDK_PATH=/home/projects/k3s-device/pico-sdk

# Create build directory
mkdir build
cd build

# Configure
cmake ..

# Build
make -j4
```

This produces `k3s_pico_node.uf2` file.

## Flashing

1. Connect Pico WH via USB while holding BOOTSEL button
2. Copy the UF2 file to the mounted drive:
   ```bash
   cp k3s_pico_node.uf2 /media/$USER/RPI-RP2/
   ```
3. Pico will reboot and start running

## Monitoring

Connect via USB serial (115200 baud) to see debug output:
```bash
screen /dev/ttyACM0 115200
# or
minicom -D /dev/ttyACM0 -b 115200
```

## Verifying Node Registration

On your k3s server:

```bash
# Check node appears
kubectl get nodes

# View node details
kubectl describe node pico-node-1

# Check node is Ready
kubectl get node pico-node-1 -o jsonpath='{.status.conditions[?(@.type=="Ready")].status}'
```

## Testing ConfigMap Updates

Create a ConfigMap to update Pico memory:

```bash
# Create ConfigMap with memory updates
kubectl create configmap pico-config \
  --from-literal=memory_values="0=0x42,1=0x43,2=0xFF"

# Update existing ConfigMap
kubectl patch configmap pico-config \
  --type merge \
  -p '{"data":{"memory_values":"0=0xAA,1=0xBB,10=0xCC"}}'
```

The Pico polls this ConfigMap every 30 seconds and updates its memory accordingly.

## Project Structure

```
k3s-pico-node/
├── CMakeLists.txt          # Build configuration
├── pico_sdk_import.cmake   # SDK import script
├── lwipopts.h              # lwIP memory configuration
├── mbedtls_config.h        # TLS configuration
├── certs/                  # Generated certificates
│   ├── generate-certs.sh
│   └── *.crt, *.key
├── include/                # Header files
│   ├── certs.h
│   ├── config.h
│   ├── kubelet_server.h
│   ├── k3s_client.h
│   ├── node_status.h
│   ├── configmap_watcher.h
│   └── memory_manager.h
└── src/                    # Source files
    ├── main.c
    ├── kubelet_server.c
    ├── k3s_client.c
    ├── node_status.c
    ├── configmap_watcher.c
    └── memory_manager.c
```

## Memory Budget

The RP2040 has 264KB SRAM. Estimated allocation:

- lwIP stack: ~40-60 KB
- mbedtls (TLS): ~30-50 KB
- CYW43 driver: ~10-15 KB
- Application code: ~20-30 KB
- Buffers: ~20-28 KB
- **Remaining: ~80-144 KB** for application data

## Known Limitations

1. **No Container Runtime**: Cannot run actual containers (by design)
2. **HTTP Client**: Pico uses HTTP to nginx proxy (server-side TLS only)
3. **Kubelet HTTP Only**: Mock kubelet server doesn't use TLS yet
4. **Polling Only**: Uses polling instead of Watch API for ConfigMaps
5. **Shared Identity**: All Picos use the same identity (nginx's admin cert)
6. **No Secure Storage**: WiFi credentials and firmware stored in plaintext on flash

See [docs/TLS-PROXY-RATIONALE.md](docs/TLS-PROXY-RATIONALE.md) for detailed security analysis.

## Next Steps

To make this production-ready:

1. **Implement Watch API**: Use k8s Watch instead of polling ConfigMaps
2. **OTA Updates**: Enable firmware updates via k8s Jobs
3. **Per-Device Identity**: Implement secure provisioning with unique certificates per device
4. **Hardware Security**: Migrate to microcontroller with secure enclave (e.g., ESP32-S3)
5. **Metrics**: Add real Prometheus metrics with more detailed telemetry
6. **Secure Boot**: Implement firmware signature verification

For experimental/lab deployments, the current architecture is acceptable. See [docs/TLS-PROXY-RATIONALE.md](docs/TLS-PROXY-RATIONALE.md) for detailed discussion of security trade-offs.

## Troubleshooting

### WiFi Connection Fails
- Check SSID and password in `config.h`
- Ensure WPA2-AES security mode
- Verify 2.4GHz WiFi network

### Node Registration Fails
- Check k3s API server is accessible from Pico
- Verify certificates were generated correctly
- Check `kubectl logs` for API server errors

### ConfigMap Not Updating
- Verify ConfigMap exists: `kubectl get configmap pico-config`
- Check ConfigMap has `data.memory_values` field
- Wait 30 seconds for poll interval

## License

This project is licensed under the Apache License 2.0. See the [LICENSE](LICENSE) file for details.

## Credits

Built with:
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
- [lwIP](https://savannah.nongnu.org/projects/lwip/)
- [Mbed TLS](https://github.com/Mbed-TLS/mbedtls)
- [k3s](https://k3s.io/)
