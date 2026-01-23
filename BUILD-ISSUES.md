# Build Issues - 2026-01-21

## Current State

**Server (k3s-pico-server)**: ✅ **WORKING**
- nginx proxy running successfully
- Accessible at `http://192.168.86.232:6080`
- All endpoints tested and functional
- Ready for client connections

**Client (k3s-pico-node)**: ❌ **BUILD FAILING**
- CMake cannot find SDK headers (`pico/cyw43_arch.h`, `lwip/tcp.h`)
- Issue affects all source files
- test-blink project builds successfully with same SDK
- Problem is with main project CMakeLists.txt configuration

## Symptoms

```
fatal error: pico/cyw43_arch.h: No such file or directory
fatal error: lwip/tcp.h: No such file or directory
```

## What Works

- `test-blink/` builds successfully
- PICO_SDK_PATH is set correctly: `/home/projects/k3s-device/pico-sdk`
- SDK has all required libraries (cyw43-driver, lwIP, mbedtls)
- cmake detects the SDK and board type correctly

## What Doesn't Work

- Main k3s_pico_node project cannot find includes
- Affects: main.c, kubelet_server.c, k3s_client.c, tls_connection.c, node_status.c
- Same includes work fine in test-blink

## Root Cause (Suspected)

CMakeLists.txt include directory configuration is incorrect or incomplete.

Comparison with working test-blink:
- test-blink: Simple configuration, minimal libraries
- k3s_pico_node: Complex configuration, many source files

**Hypothesis**: The include paths aren't being propagated from pico_cyw43_arch_lwip_poll target.

## Attempted Fixes (All Failed)

1. ✗ Added `pico_lwip` library explicitly
2. ✗ Removed then re-added source files
3. ✗ Clean rebuild multiple times
4. ✗ Restored original CMakeLists.txt from git
5. ✗ Verified all source files exist

## Configuration

**Port Settings** (Correct for nginx proxy):
```c
// include/config.h
#define K3S_SERVER_PORT 6080  // nginx proxy port

// include/config_local.h
#define K3S_SERVER_IP "192.168.86.232"
#define WIFI_SSID "Socrates"
#define WIFI_PASSWORD "orange-sixpence-perhaps-heal"
```

## Next Steps to Fix

### Option 1: Debug CMake Configuration
1. Compare working test-blink CMakeLists.txt line-by-line
2. Add explicit include directories:
   ```cmake
   target_include_directories(k3s_pico_node PRIVATE
       ${PICO_SDK_PATH}/src/rp2_common/pico_cyw43_arch/include
       ${PICO_SDK_PATH}/lib/lwip/src/include
       ${PICO_SDK_PATH}/lib/cyw43-driver/src
   )
   ```
3. Check cmake verbose output: `make VERBOSE=1`

### Option 2: Simplify Project Structure
1. Start with minimal working test-blink
2. Add source files one at a time
3. Identify which file/configuration breaks the build

### Option 3: Fresh Start
1. Create new project directory
2. Copy only essential files
3. Use test-blink CMakeLists.txt as template
4. Gradually add complexity

## HTTP-Only Implementation (Separate Issue)

New files created for HTTP-only mode (TLS handled by nginx proxy):
- `src/tcp_connection.c` - Plain TCP connection (no TLS)
- `include/tcp_connection.h` - TCP connection header
- Updated `src/k3s_client.c` - Uses TCP instead of TLS

**Status**: Code written but not tested due to build issues.

**When build is fixed**:
1. Update CMakeLists.txt to use `tcp_connection.c` instead of `tls_connection.c`
2. Remove mbedtls dependencies (optional - can keep for compatibility)
3. Test HTTP connection to nginx proxy
4. Verify node registration works

## Test Plan (Once Build Fixed)

### Phase 1: Basic Connectivity
1. Flash working firmware
2. Connect to WiFi
3. Attempt connection to `192.168.86.232:6080`
4. Verify nginx proxy receives connection

### Phase 2: HTTP Communication
1. Send HTTP GET to `/version`
2. Verify nginx forwards to k3s API
3. Confirm response received by Pico

### Phase 3: Node Registration
1. POST to `/api/v1/nodes`
2. Check `kubectl get nodes`
3. Verify node appears as "pico-node-1"

## Workarounds (Temporary)

None available - build must succeed before any testing can occur.

## Environment

- OS: openSUSE Tumbleweed
- SDK: `/home/projects/k3s-device/pico-sdk`
- Board: Pico W (PICO_BOARD=pico_w)
- Toolchain: arm-none-eabi-gcc
- CMake: Working (test-blink builds)

## Related Files

- `CMakeLists.txt` - Main build configuration (NEEDS FIX)
- `test-blink/CMakeLists.txt` - Working reference
- `include/config.h` - Port 6080 configured ✓
- `include/config_local.h` - WiFi and server IP ✓

## Priority

**HIGH** - Blocks all Pico hardware testing

## Estimated Time to Fix

- Option 1 (Debug CMake): 1-2 hours
- Option 2 (Simplify): 2-3 hours
- Option 3 (Fresh start): 3-4 hours

---

**Last Updated**: 2026-01-21 21:40 EST
**Status**: BLOCKED - Build system issue
**Next Action**: Debug CMake include paths or start fresh with working test-blink template
