# Build System Changes Summary

This document summarizes all changes made to the K3s Pico Node build system and project structure.

## Main Firmware Changes

### 1. Security: WiFi Credentials Management

**Files Modified:**
- `include/config.h` - Now includes `config_local.h` instead of hardcoding credentials
- `.gitignore` - Added to exclude `config_local.h` from version control

**Files Created:**
- `include/config_local.h.template` - Template with placeholder credentials

**Rationale:** Prevents accidental commit of WiFi credentials to version control.

**Usage:**
```bash
cp include/config_local.h.template include/config_local.h
nano include/config_local.h  # Edit with actual credentials
```

### 2. Hardware Random Number Generator

**Files Modified:**
- `CMakeLists.txt` - Added `pico_rand` library
- `src/k3s_client.c` - Removed duplicate `mbedtls_hardware_poll()` implementation

**Rationale:** The Pico SDK provides hardware RNG support via `pico_mbedtls`. Using the built-in implementation instead of creating our own ensures proper entropy collection.

### 3. Elliptic Curve Cryptography Support

**Files Modified:**
- `mbedtls_config.h` - Enabled ECC/ECDSA/ECDH support

**Changes:**
```c
// Added:
#define MBEDTLS_ECP_C              // Elliptic curve point operations
#define MBEDTLS_ECDSA_C            // ECDSA signature verification
#define MBEDTLS_ECDH_C             // ECDH key exchange
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED  // P-256 curve
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_ENTROPY_HARDWARE_ALT    // Use custom mbedtls_hardware_poll()
```

**Rationale:** K3s server certificates use ECDSA (Elliptic Curve Digital Signature Algorithm), which requires ECC support in mbedtls.

### 4. WiFi Authentication Diagnostics

**Files Modified:**
- `src/main.c` - Added diagnostics for WiFi connection, changed from `CYW43_AUTH_WPA2_AES_PSK` to `CYW43_AUTH_WPA2_MIXED_PSK`

**Rationale:** WPA2 Mixed mode is more compatible with various router configurations (supports both TKIP and AES).

### 5. TinyUSB Initialization

**Dependencies:**
- Initialized `pico-sdk/lib/tinyusb` submodule

**Command:**
```bash
cd /path/to/pico-sdk
git submodule update --init lib/tinyusb
```

**Rationale:** USB serial support requires TinyUSB library for proper USB enumeration and CDC-ACM communication.

## Test Subproject

### Directory Structure

```
test-blink/
├── CMakeLists.txt           # Build configuration
├── LICENSE                  # Apache 2.0 license
├── README.md                # Test documentation
├── lwipopts.h              # lwIP configuration (copied from main)
├── pico_sdk_import.cmake   # SDK import script (copied from main)
├── src/
│   └── blink_test.c        # LED blink test firmware
└── build/                   # Build directory (gitignored)
```

### Purpose

Minimal hardware verification test that:
- Verifies USB serial communication works
- Tests WiFi chip initialization (required for LED control on Pico W/WH)
- Blinks the onboard LED every 500ms
- Outputs diagnostic messages to serial console

### Building

```bash
cd test-blink
mkdir -p build && cd build
cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..
make -j4
```

Output: `pico_blink_test.uf2` (~590KB)

### Benefits

1. **Quick Hardware Validation** - Takes minutes to build and flash
2. **Troubleshooting** - Identifies hardware issues before full firmware
3. **Separate License** - Can be distributed independently (Apache 2.0)
4. **Learning Tool** - Minimal example for Pico WH development

## Documentation Updates

### README.md

**Added Sections:**
- "Hardware Testing" - Links to test-blink subproject
- Updated "Configuration" - Documents secure credential management with `config_local.h`

### NEXT_STEPS.md

**Updated:**
- Section 1: Changed WiFi credential instructions to use `config_local.h`

## Build Verification

All changes have been tested and verified:

✅ Main firmware builds successfully
✅ Test firmware builds successfully
✅ WiFi connection works with WPA2 Mixed mode
✅ Hardware RNG provides proper entropy
✅ ECDSA certificates parse correctly
✅ USB serial communication works
✅ All subsystems initialize properly

## Dependencies

### Required Pico SDK Submodules

```bash
cd /path/to/pico-sdk

# TinyUSB (USB support)
git submodule update --init lib/tinyusb

# cyw43-driver (already initialized)
git submodule update --init lib/cyw43-driver

# mbedtls (already initialized)
git submodule update --init lib/mbedtls

# lwIP (already initialized)
git submodule update --init lib/lwip
```

## Build Artifacts

### Main Firmware
- Location: `build/k3s_pico_node.uf2`
- Size: ~814KB
- Memory: ~35KB RAM, ~396KB Flash

### Test Firmware
- Location: `test-blink/build/pico_blink_test.uf2`
- Size: ~590KB
- Memory: Minimal (just LED blink)

## Git Status

### Files to Commit
- `.gitignore`
- `include/config_local.h.template`
- `include/config.h` (modified)
- `CMakeLists.txt` (modified - added pico_rand)
- `mbedtls_config.h` (modified - enabled ECC)
- `src/main.c` (modified - WiFi diagnostics)
- `src/k3s_client.c` (modified - removed duplicate hardware poll)
- `README.md` (modified)
- `NEXT_STEPS.md` (modified)
- `BUILD_CHANGES.md` (this file)
- `test-blink/` (entire directory)

### Files to NOT Commit (gitignored)
- `include/config_local.h` - Contains actual WiFi credentials
- `build/` - Build artifacts
- `test-blink/build/` - Test build artifacts

## Summary

The build system is now:
1. **Secure** - Credentials not committed to version control
2. **Complete** - All dependencies properly configured
3. **Tested** - Hardware verification test available
4. **Documented** - README and test documentation updated
5. **Working** - Full firmware successfully runs on hardware

The project is ready for:
- Version control commit (excluding credentials)
- Distribution to other developers
- Next step: TLS implementation (as outlined in NEXT_STEPS.md)
