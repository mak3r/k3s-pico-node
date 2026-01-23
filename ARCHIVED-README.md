# ⚠️ ARCHIVED - Moved to k3s-micro Platform

This repository has been integrated into the **k3s-micro embedded kubelet platform**:

**New Repository**: https://github.com/mak3r/k3s-micro

## What Changed?

The RP2040 Pico implementation is now the **reference implementation** in a multi-board platform at:

📂 **`implementations/rp2040-pico-c/`**

## What is k3s-micro?

A unified platform for running Kubernetes worker nodes on multiple embedded device families:

| Board | Architecture | Language | Status |
|-------|-------------|----------|--------|
| **Raspberry Pi Pico W** | ARM Cortex-M0+ | C | ✅ Reference Implementation |
| ESP32 | Xtensa LX6 | C | 🔜 Planned |
| MicroPython | Various | Python | 🔜 Planned |
| Arduino | Various | C++ | 🔜 Planned |

### Platform Features

- **Multi-Board Support**: Extensible architecture for diverse microcontrollers
- **Provisioning Infrastructure**: K8s-native device flashing and configuration
- **ECI Library**: Embedded container images for supported architectures
- **Specifications**: Kubelet protocol, porting guides, API contracts

## Migration Guide

### For Pico W Development

Your workflow remains largely the same, just with new paths:

**Old**:
```bash
cd k3s-pico-node
mkdir build && cd build
cmake ..
make -j4
```

**New**:
```bash
cd k3s-micro/implementations/rp2040-pico-c
mkdir build && cd build
cmake ..
make -j4
```

### Documentation Locations

| Old Location | New Location |
|-------------|--------------|
| `README.md` | `implementations/rp2040-pico-c/README.md` |
| `docs/ARCHITECTURE.md` | `implementations/rp2040-pico-c/docs/ARCHITECTURE.md` |
| `docs/TLS-PROXY-RATIONALE.md` | `implementations/rp2040-pico-c/docs/TLS-PROXY-RATIONALE.md` |
| `plans/PORTING_GUIDE.md` | `docs/adding-bsp/PORTING_GUIDE.md` *(platform-level)* |
| `docs/KUBELET_REQUIREMENTS.md` | `docs/kubelet-requirements/REQUIREMENTS.md` *(platform-level)* |

### Platform Documentation

New documentation in k3s-micro:

- **Platform Architecture**: `docs/architecture/OVERVIEW.md`
- **Provisioning Workflow**: `docs/architecture/PROVISIONING.md`
- **Adding New Boards**: `docs/adding-bsp/PORTING_GUIDE.md`
- **Contributing**: `docs/CONTRIBUTING.md`
- **Specifications**: `specs/` (kubelet protocol, ECI format, etc.)

## Why the Change?

### Vision Expansion

What started as a Pico-specific project has evolved into a **platform architecture** that can support:
- Multiple board families (ESP32, Arduino, STM32, etc.)
- Multiple languages (C, C++, MicroPython, Rust)
- Shared infrastructure (proxy, operators, ECI library)
- Common specifications (kubelet protocol, provisioning API)

### Benefits

1. **Reusable Infrastructure**: nginx proxy, provisioning workflows work for all boards
2. **Shared Knowledge**: Common specs and docs help all implementations
3. **ECI Library**: Workloads can target multiple architectures
4. **GitOps Provisioning**: Cluster-managed device setup
5. **Community Growth**: Platform approach attracts more contributors

## Repository History

This repository's complete commit history (11 commits) has been preserved in k3s-micro as:

```
implementations/rp2040-pico-c/
```

**Last standalone commit**: `ffcf93f` - Add private key files to gitignore
**Archived**: 2026-01-23
**Integrated into**: k3s-micro v1.0.0

## Getting Started with k3s-micro

### 1. Clone the Platform

```bash
git clone https://github.com/mak3r/k3s-micro.git
cd k3s-micro
```

### 2. Set Up Environment

```bash
./setup-env.sh
# This generates CLAUDE.md with your environment config
```

### 3. Build Pico Firmware

```bash
cd implementations/rp2040-pico-c
cp include/config_local.h.template include/config_local.h
nano include/config_local.h  # Edit WiFi credentials, k8s server IP

mkdir build && cd build
cmake ..
make -j4

# Flash to Pico
cp k3s_pico_node.uf2 /run/media/$USER/RPI-RP2/
```

### 4. Deploy Infrastructure

```bash
cd ../../infrastructure
kubectl apply -f manifests/
```

### 5. Verify

```bash
kubectl get nodes
kubectl describe node pico-node-1
```

## Issues and Support

**For Pico-specific issues**:
- Open issue at: https://github.com/mak3r/k3s-micro/issues
- Label with: `bsp:rp2040-pico-c`

**For platform questions**:
- Discussions: https://github.com/mak3r/k3s-micro/discussions

## Contributing

We welcome contributions!

- **Improve Pico BSP**: https://github.com/mak3r/k3s-micro/tree/main/implementations/rp2040-pico-c
- **Add New Board**: See `docs/adding-bsp/PORTING_GUIDE.md`
- **Create ECIs**: Add workloads to `eci-library/`
- **Improve Docs**: All documentation improvements welcome

See: https://github.com/mak3r/k3s-micro/blob/main/docs/CONTRIBUTING.md

## Links

- **k3s-micro Platform**: https://github.com/mak3r/k3s-micro
- **Pico Implementation**: https://github.com/mak3r/k3s-micro/tree/main/implementations/rp2040-pico-c
- **Platform Docs**: https://github.com/mak3r/k3s-micro/tree/main/docs
- **Porting Guide**: https://github.com/mak3r/k3s-micro/blob/main/docs/adding-bsp/PORTING_GUIDE.md

---

Thank you for your interest in this project! The evolution to k3s-micro enables us to bring Kubernetes to embedded devices everywhere. 🚀

**Questions?** Open an issue at https://github.com/mak3r/k3s-micro/issues
