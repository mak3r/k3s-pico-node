# k3s-pico-node Documentation

This directory contains comprehensive documentation for the k3s-pico-node project.

## Quick Start

1. [Main README](../README.md) - Start here for build and flash instructions
2. [nginx Proxy Setup](NGINX-PROXY-SETUP.md) - Set up the TLS proxy (required before running Pico)
3. [Architecture](ARCHITECTURE.md) - Understand the system design

## Documentation Index

### Setup Guides

- **[nginx Proxy Setup](NGINX-PROXY-SETUP.md)** - Step-by-step guide to configure the TLS termination proxy on your k3s server. **Start here before flashing the Pico.**

### Architecture & Design

- **[Architecture](ARCHITECTURE.md)** - Complete system architecture including:
  - Component breakdown (Pico W, nginx proxy, k3s API)
  - Communication flows (node registration, status reporting, ConfigMap polling)
  - Data structures (Node objects, ConfigMap format)
  - Memory layout
  - Performance characteristics

- **[TLS Proxy Rationale](TLS-PROXY-RATIONALE.md)** - Comprehensive analysis of why we bypass client-side TLS:
  - Security reality of embedded devices (no secure storage, physical access)
  - Trust bootstrapping problems (certificates in firmware)
  - Threat model mismatch (trusted local network vs adversarial internet)
  - Implementation challenges (mbedtls/Go incompatibility, performance)
  - Server-side proxy as pragmatic solution
  - What security considerations remain important

### Configuration Files

- **[k3s-proxy.conf](k3s-proxy.conf)** - nginx configuration file ready to deploy

## Key Concepts

### TLS Proxy Architecture

This project uses **server-side TLS termination** instead of implementing TLS on the Pico:

```
Pico W (HTTP) → nginx proxy (TLS termination) → k3s API (HTTPS)
   :6080                                            :6443
```

**Why?**
- Pico lacks secure storage, hardware crypto, and sufficient RAM for TLS
- mbedtls (Pico SDK) has compatibility issues with Go TLS (k3s)
- Certificates embedded in firmware are easily extracted
- Server-side TLS maintains k8s ecosystem compatibility
- Simplifies debugging and reduces complexity

**See**: [TLS-PROXY-RATIONALE.md](TLS-PROXY-RATIONALE.md) for detailed security analysis.

### Security Model

**What We Protect:**
- Network boundary (firewall, WPA2/WPA3 WiFi)
- Server-side TLS (k3s API remains secure)
- Application layer (Kubernetes RBAC)

**What We Don't Protect:**
- Physical access to Pico (no secure boot, no encrypted storage)
- Network eavesdropping on local LAN (HTTP between Pico and proxy)
- Credential sharing (all Picos use same identity via nginx's cert)

**Acceptable For:**
- Lab/experimental deployments
- Trusted local networks
- Physically secured environments
- Projects prioritizing simplicity over defense-in-depth

**Not Acceptable For:**
- Production deployments in adversarial environments
- Untrusted networks
- High-security requirements
- Compliance-regulated environments (HIPAA, PCI-DSS, etc.)

### Component Roles

| Component | Role | Protocol | Port |
|-----------|------|----------|------|
| **Pico W** | Kubernetes node client | HTTP | Outbound to :6080 |
| **nginx proxy** | TLS termination | HTTP→HTTPS | Listen :6080, connect :6443 |
| **k3s API** | Kubernetes API server | HTTPS | Listen :6443 |
| **Mock Kubelet** | Health endpoint (on Pico) | HTTP | Listen :10250 |

## Implementation Status

### ✅ Completed

- WiFi connection and lwIP networking
- HTTP client implementation
- JSON parsing and generation
- Node registration and status reporting
- ConfigMap polling and memory updates
- Mock kubelet server (/healthz, /metrics)
- nginx proxy architecture and documentation

### 🚧 In Progress

- TLS proxy deployment and testing
- End-to-end integration testing
- 24-hour endurance testing

### 📋 Planned

- Watch API (replace polling)
- OTA firmware updates via k8s Jobs
- Real Prometheus metrics
- Per-device identity provisioning (for production)

## Development Workflow

1. **Set up nginx proxy** on k3s server ([NGINX-PROXY-SETUP.md](NGINX-PROXY-SETUP.md))
2. **Configure Pico** with WiFi and k3s server IP
3. **Build and flash** Pico firmware
4. **Monitor serial output** to verify connection
5. **Check k3s cluster**: `kubectl get nodes`

## Troubleshooting

See [NGINX-PROXY-SETUP.md - Troubleshooting](NGINX-PROXY-SETUP.md#troubleshooting) for common issues:
- Connection refused
- 502 Bad Gateway
- 403 Forbidden
- SSL certificate problems
- Firewall blocking

## References

### External Documentation

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
- [lwIP Documentation](https://www.nongnu.org/lwip/)
- [Kubernetes API Reference](https://kubernetes.io/docs/reference/kubernetes-api/)
- [k3s Architecture](https://docs.k3s.io/architecture)
- [nginx Proxy Module](http://nginx.org/en/docs/http/ngx_http_proxy_module.html)

### Hardware Documentation

- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- [Pico W Datasheet](https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf)
- [CYW43439 WiFi Chip](https://www.infineon.com/cms/en/product/wireless-connectivity/airoc-wi-fi-plus-bluetooth-combos/cyw43439/)

## Contributing

This is an experimental research project. Contributions welcome for:
- Bug fixes
- Performance improvements
- Documentation enhancements
- Testing on different network configurations
- Alternative security approaches

## License

This project is provided as-is for educational and research purposes.

---

**Document Status**: Active
**Last Updated**: 2026-01-21
**Version**: 1.0
