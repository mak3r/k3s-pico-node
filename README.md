# Raspberry Pi Pico WH as K3s Node

This project enables a Raspberry Pi Pico WH microcontroller to appear as a node in a k3s Kubernetes cluster. The Pico cannot run actual containers, but it can register as a node, report status, and receive configuration updates via ConfigMaps.

## Features

- **Node Registration**: Registers with k3s cluster and appears in `kubectl get nodes`
- **Status Reporting**: Reports node status every 10 seconds
- **Mock Kubelet**: Implements minimal kubelet endpoints (`/healthz`, `/metrics`)
- **ConfigMap Integration**: Polls ConfigMaps and updates designated memory regions
- **Memory-Optimized**: Runs within RP2040's 264KB RAM constraint

## Architecture

```
K3s Server                    Raspberry Pi Pico WH
┌──────────────┐              ┌────────────────────┐
│ API Server   │◄────TLS──────┤ K3s Client         │
│   :6443      │   Status     │ (node registration)│
│              │   Reports    │                    │
│ ConfigMaps   │◄────Poll─────┤ ConfigMap Watcher  │
│              │              │ (memory updates)   │
└──────────────┘              │                    │
                              │ Mock Kubelet       │
                              │   :10250 (HTTP)    │
                              │   /healthz         │
                              │                    │
                              │ Memory Region      │
                              │   (1KB SRAM)       │
                              └────────────────────┘
```

## Prerequisites

- **Hardware**: Raspberry Pi Pico WH (with WiFi)
- **K3s Cluster**: Running k3s cluster with API access
- **Toolchain**: ARM cross-compiler, CMake, pico-sdk
- **Network**: Pico and k3s server on same network

## Hardware Testing

Before building the full firmware, it's recommended to verify your Pico WH hardware is working correctly using the test firmware in the `test-blink/` subdirectory.

See [test-blink/README.md](test-blink/README.md) for instructions on:
- Building the minimal LED blink test
- Verifying USB serial communication
- Confirming WiFi chip initialization
- Troubleshooting hardware issues

The test firmware takes only a few minutes to build and flash, and can save time by identifying hardware problems before attempting the full K3s node firmware.

## Configuration

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

1. **No Container Runtime**: Cannot run actual containers
2. **HTTP Only (currently)**: Kubelet server doesn't use TLS yet
3. **No TLS Client (currently)**: k3s_client needs TLS implementation
4. **Polling Only**: Uses polling instead of Watch API for ConfigMaps
5. **Certificate Expiration**: Pre-generated certs expire in 365 days

## Next Steps

To make this production-ready:

1. **Implement TLS**: Add TLS to kubelet server and k3s client
2. **Implement Watch API**: Use k8s Watch instead of polling
3. **OTA Updates**: Enable firmware updates via k8s Jobs
4. **Certificate Renewal**: Implement CSR-based cert renewal
5. **Metrics**: Add real Prometheus metrics

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

This project is provided as-is for educational purposes.

## Credits

Built with:
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
- [lwIP](https://savannah.nongnu.org/projects/lwip/)
- [Mbed TLS](https://github.com/Mbed-TLS/mbedtls)
- [k3s](https://k3s.io/)
