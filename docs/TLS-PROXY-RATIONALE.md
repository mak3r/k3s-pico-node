# TLS Proxy Architecture - Security Rationale

## Decision: Use HTTP Proxy Instead of Client-Side TLS

This document explains why we bypassed TLS on the Pico microcontroller and use a server-side TLS termination proxy instead.

## Why TLS Doesn't Provide Real Security Here

### 1. Physical Access to Embedded Devices

**Problem**: Microcontrollers are physically accessible devices.

- Anyone with physical access can dump the Pico's 2MB flash memory
- The entire filesystem, including certificates and private keys, is stored in plaintext on flash
- Tools like `picotool` can extract firmware in minutes
- The RP2040 has no secure enclave, no encrypted storage, no hardware root of trust

**Reality**: Physical security is the only real security boundary. If an attacker has physical access to the Pico, they can extract:
- Client certificate
- Client private key
- Server CA certificate
- WiFi credentials
- K3s server IP address
- All application code

TLS protects data in transit, but when the keys protecting that data are stored in cleartext on easily-dumpable flash, the protection is illusory.

### 2. Trust Bootstrapping Problem

**Problem**: TLS mutual authentication requires both parties to trust each other, but that trust must be established *before* the first connection.

**How we "solved" this**:
- Generated client certificate on development machine
- Embedded client cert and private key directly in firmware source code (`certs.h`)
- Downloaded server CA certificate from k3s server
- Embedded server CA certificate in firmware source code
- Compiled and flashed to Pico

**Security implications**:
- The certificates are in the **git repository** (or should be gitignored, but still in the build artifacts)
- Anyone with access to the repository or firmware binary has the credentials
- The client certificate is **shared across all Pico devices** running this firmware
- There's no per-device identity or certificate provisioning system
- Revoking a compromised certificate requires reflashing **all devices**

**The bootstrapping catch-22**: To securely bootstrap trust, you need:
1. A secure channel (but that's what TLS is supposed to provide)
2. A secure provisioning process (but the Pico has no secure storage)
3. Per-device credentials (but we have no secure way to generate or store them)

### 3. Threat Model Mismatch

**What TLS protects against**:
- Eavesdropping on network traffic by passive attackers
- Man-in-the-middle attacks by network-level adversaries
- Server impersonation
- Client impersonation (with mutual TLS)

**Our actual threat model**:
- **Trusted local network**: Devices operate on a private WiFi network, likely home/lab environment
- **Experimental project**: This is research into K8s-for-microcontrollers, not a production system
- **Physical security**: The primary attack vector is physical access to devices, not network attacks
- **Single-cluster deployment**: Devices only connect to a specific known k3s cluster, not arbitrary clusters

**Mismatch**: TLS is designed for adversarial internet environments. We're in a controlled local network where the effort to compromise TLS (physical access to Pico) is **easier** than network-based attacks.

### 4. Implementation Reality

**Technical challenges we encountered**:
- mbedtls 3.6.2 (Pico SDK) has incompatibility with Go's TLS implementation (k3s)
- RSA-2048 signing takes 3.5 seconds on RP2040's 133MHz Cortex-M0+
- Even ECDSA signing adds latency and complexity
- TLS handshake consumes ~40KB of the 264KB available RAM
- Debugging TLS failures is extremely difficult on embedded systems

**The cost**: We spent significant time fighting TLS compatibility issues instead of working on the actual project goal (container orchestration for microcontrollers).

**The benefit**: None, because the certificates are in plaintext on flash anyway.

## The Server-Side Proxy Solution

### Architecture

```
┌─────────────────┐         ┌──────────────────────────────────┐
│                 │  HTTP   │  k3s Server                      │
│  Pico (HTTP)    ├────────►│  ┌─────────────────┐             │
│  192.168.x.x    │ :6080   │  │ nginx proxy     │  HTTPS      │
│                 │         │  │ (TLS term)      ├────────────► │
│                 │         │  └─────────────────┘   :6443     │
│                 │         │           │                       │
│                 │         │           v                       │
│                 │         │  ┌─────────────────┐             │
│                 │         │  │ k3s API server  │             │
│                 │         │  │ localhost:6443  │             │
│                 │         │  └─────────────────┘             │
└─────────────────┘         └──────────────────────────────────┘
```

### How It Works

1. **Pico**: Sends plain HTTP requests to `http://192.168.x.x:6080`
2. **nginx proxy**:
   - Receives HTTP on port 6080
   - Adds TLS with proper client certificate
   - Forwards to k3s API at `https://localhost:6443`
3. **k3s API**: Sees a properly authenticated TLS connection from localhost

### Why This Is Acceptable

**Network security**:
- Traffic between Pico and server is unencrypted, but on a private network
- If someone can sniff your local network, they likely have physical access anyway
- The k3s server itself is on the same LAN as the Picos

**Authentication**:
- The proxy uses the k3s admin certificate to authenticate
- Picos effectively share the admin credential (but they did anyway via embedded certs)
- Access control can be implemented at the application layer (K8s RBAC, node names, etc.)

**Simplicity**:
- Eliminates TLS complexity from resource-constrained Pico
- Makes debugging trivial (can use curl, browser, wireshark with plaintext)
- Reduces RAM usage by ~40KB
- Eliminates multi-second RSA signature delays
- Removes compatibility issues between mbedtls and Go TLS

**Development velocity**:
- Unblocks progress on the actual interesting problems
- Easy to iterate and debug
- Can add TLS back later if needed

## What Security Considerations Remain

Even without TLS on the client side, these remain important:

### 1. Network Boundary Protection
- Ensure the WiFi network is WPA2/WPA3 protected (not open)
- The k3s server should not expose port 6080 to the internet
- Use firewall rules to restrict port 6080 to local network only

### 2. K8s RBAC and Authorization
- Use Kubernetes Role-Based Access Control
- Give node clients minimal required permissions
- Implement proper namespace isolation
- Use NetworkPolicies to restrict pod-to-pod traffic

### 3. Physical Security
- This remains the primary security boundary
- Store devices in physically secure locations when possible
- Don't deploy to untrusted/adversarial environments

### 4. Credential Rotation
- Use distinct node names per device
- Implement a registration/deregistration flow
- Consider adding API keys or tokens at the application layer

## Alternative Approaches Considered

### Option 1: Fix mbedtls/Go TLS Incompatibility
**Status**: Attempted, unsuccessful after extensive debugging
**Blocker**: Fundamental incompatibility in Finished message MAC calculation
**Effort**: Would require upstream patches to mbedtls or Pico SDK

### Option 2: Switch to WolfSSL or BearSSL
**Status**: Not attempted
**Risk**: May have same compatibility issues, significant refactoring required
**Timeline**: Unknown, could be days/weeks

### Option 3: Use TLS 1.3
**Status**: Not supported by mbedtls in Pico SDK
**Blocker**: K3s supports TLS 1.3, but would need mbedtls 3.6+ with TLS 1.3 enabled

### Option 4: Pre-shared Key (PSK) Cipher Suites
**Status**: Not attempted
**Problem**: Still doesn't solve the key distribution/storage problem

## Conclusion

**TLS on the Pico provided security theater, not actual security.**

The combination of:
- No secure storage
- Shared credentials across devices
- Certificates in git repo / firmware binary
- Physical access to devices
- Trusted network environment

...means TLS was adding significant complexity for negligible security benefit.

**The proxy approach is honest about the security model**: This is a local, experimental, physically-secured deployment where network security is secondary to physical security.

## Future Work

If this project moves toward production deployment or adversarial environments:

1. **Hardware security modules**: Use microcontrollers with secure enclaves (e.g., ESP32-S3, STM32 with secure boot)
2. **Per-device provisioning**: Generate unique certificates per device during manufacturing/setup
3. **Secure boot chain**: Sign and verify firmware images
4. **Network isolation**: Deploy on isolated VLAN with strict firewall rules
5. **Re-evaluate TLS**: With proper hardware and provisioning, client-side TLS becomes worthwhile

For now, the proxy approach lets us focus on the interesting problem: making Kubernetes work with microcontrollers.

---

**Document Date**: 2026-01-21
**Decision Made By**: Project team after extensive TLS debugging
**Status**: Active
