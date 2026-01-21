# Porting Guide: K3s Node to New Hardware

## Overview

This guide shows how to port the K3s Pico Node to a new hardware platform. The architecture is designed for portability - most of the "OS" code (K8s integration, networking, TLS, pod management) is board-agnostic. Only the Board Support Package (BSP) needs to be written for new hardware.

## What's Portable vs. What's Not

### ✅ Fully Portable (No Changes Needed)

These components work on any ARM Cortex-M board with sufficient RAM:

```
src/
├── k3s_client.c          # TLS communication with K8s API
├── resource_manager.c    # K8s resource orchestration
├── pod_manager.c         # Pod lifecycle management
├── node_status.c         # Node status reporting
├── configmap_watcher.c   # ConfigMap polling
├── container.c           # Container abstraction
└── kubelet_server.c      # HTTP server

include/
├── hardware_manager.h    # Hardware abstraction API
└── (other headers)
```

**Requirements:**
- ARM Cortex-M0+ or better
- 200KB+ RAM (more is better)
- WiFi or Ethernet
- Flash storage (512KB+ for firmware, more for variants)

### ⚠️ Board-Specific (You Write This)

```
bsp/YOUR-BOARD/
├── hardware_manager_YOUR_BOARD.c  # Hardware discovery
├── bsp_YOUR_BOARD.c              # Board initialization
├── devices/                      # Device drivers
│   ├── led.c
│   ├── gpio.c
│   ├── sensors.c
│   └── ...
└── CMakeLists.txt               # Build config
```

### 🔄 May Need Tweaking

- **Network stack integration** - If not using lwIP
- **TLS library integration** - If not using mbedtls
- **Flash driver** - If flash API differs significantly
- **Build system** - CMake files may need adjustment

## Prerequisites

Before starting:

- [ ] Familiarize yourself with the target hardware
- [ ] Understand the hardware's SDK/HAL
- [ ] Know what peripherals are available (LEDs, GPIO, sensors)
- [ ] Have development toolchain set up
- [ ] Have a working "blink" example for the board

## Step-by-Step Porting Process

### Step 1: Create BSP Directory Structure

```bash
cd k3s-pico-node
mkdir -p bsp/YOUR-BOARD/devices
cd bsp/YOUR-BOARD
```

Create the basic structure:

```
bsp/YOUR-BOARD/
├── hardware_manager_YOUR_BOARD.c
├── bsp_YOUR_BOARD.c
├── devices/
└── CMakeLists.txt
```

### Step 2: Implement Board Initialization

**File: `bsp/YOUR-BOARD/bsp_YOUR_BOARD.c`**

This handles early hardware initialization.

```c
#include "pico/stdlib.h"  // Or your board's equivalent
#include "hardware_manager.h"

// Board-specific initialization
int bsp_init(void) {
    // 1. Initialize system clocks
    // Example for RP2040:
    // set_sys_clock_khz(133000, true);

    // 2. Initialize stdio (USB, UART, etc.)
    stdio_init_all();

    // 3. Initialize any board-specific peripherals
    // - External oscillator
    // - Power management
    // - etc.

    // 4. Initialize network interface
    // For Pico W: cyw43_arch_init()
    // For ESP32: esp_netif_init()
    // For your board: ???

    return 0;
}
```

### Step 3: Implement Hardware Discovery

**File: `bsp/YOUR-BOARD/hardware_manager_YOUR_BOARD.c`**

This tells the system what hardware exists.

```c
#include "hardware_manager.h"

// Forward declarations for your devices
extern hardware_device_t onboard_led;
extern hardware_device_t status_led;
extern hardware_device_t temp_sensor;
extern hardware_device_t gpio_pins[NUM_GPIO];
// ... add more as needed

int hardware_manager_init(void) {
    // Board-specific hardware initialization
    return bsp_init();
}

int hardware_manager_discover(void) {
    printf("Discovering hardware for YOUR-BOARD...\n");

    // Register each device you've implemented
    hardware_manager_register_device(&onboard_led);
    hardware_manager_register_device(&temp_sensor);

    // Register all GPIO pins
    for (int i = 0; i < NUM_GPIO; i++) {
        hardware_manager_register_device(&gpio_pins[i]);
    }

    // Print summary
    printf("Hardware discovery complete:\n");
    printf("  - %d LEDs\n", led_count);
    printf("  - %d sensors\n", sensor_count);
    printf("  - %d GPIO pins\n", NUM_GPIO);

    return 0;
}
```

### Step 4: Implement Device Drivers

For each hardware device (LED, sensor, GPIO), create a driver.

#### Example: LED Driver

**File: `bsp/YOUR-BOARD/devices/led.c`**

```c
#include "hardware_manager.h"
#include "YOUR_BOARD_gpio.h"  // Your board's GPIO header

#define LED_PIN 25  // Change to your board's LED pin

// LED capabilities (metadata)
typedef struct {
    bool supports_pwm;
    uint8_t max_brightness;
} led_capabilities_t;

static led_capabilities_t led_caps = {
    .supports_pwm = false,  // true if your LED supports PWM
    .max_brightness = 1     // 1 for on/off, 255 for PWM
};

// LED state
typedef struct {
    bool is_on;
    uint32_t blink_count;
} led_state_t;

static led_state_t led_state = {0};

// Initialize LED hardware
static int led_init(hardware_device_t *self) {
    // Initialize GPIO pin for LED
    // YOUR_BOARD specific code here
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);  // Off

    DEBUG_PRINT("LED initialized on pin %d", LED_PIN);
    return 0;
}

// Allocate LED to a container
static int led_allocate(hardware_device_t *self, const char *container_id) {
    if (self->allocated) {
        printf("LED already allocated to: %s\n",
               self->allocated_to_container);
        return -1;
    }

    self->allocated = true;
    self->allocated_to_container = strdup(container_id);
    self->state = HW_STATE_ALLOCATED;

    DEBUG_PRINT("LED allocated to container: %s", container_id);
    return 0;
}

// Release LED
static int led_release(hardware_device_t *self) {
    if (!self->allocated) {
        return -1;
    }

    // Turn off LED
    gpio_put(LED_PIN, 0);
    led_state.is_on = false;

    // Clear allocation
    free(self->allocated_to_container);
    self->allocated_to_container = NULL;
    self->allocated = false;
    self->state = HW_STATE_AVAILABLE;

    DEBUG_PRINT("LED released");
    return 0;
}

// Get LED status
static int led_get_status(hardware_device_t *self, char *json, size_t size) {
    snprintf(json, size,
        "{"
        "\"name\":\"%s\","
        "\"state\":\"%s\","
        "\"allocated\":%s,"
        "\"isOn\":%s"
        "}",
        self->name,
        self->state == HW_STATE_AVAILABLE ? "available" : "allocated",
        self->allocated ? "true" : "false",
        led_state.is_on ? "true" : "false");

    return 0;
}

// Public device descriptor
hardware_device_t onboard_led = {
    .name = "onboard-led",
    .resource_name = "YOUR-BOARD.io/onboard-led",  // YOUR-BOARD specific
    .type = HW_TYPE_LED,
    .state = HW_STATE_AVAILABLE,
    .allocated = false,
    .capabilities = &led_caps,
    .private_data = &led_state,
    .init = led_init,
    .allocate = led_allocate,
    .release = led_release,
    .get_status = led_get_status
};

// LED control API (for containers)
int led_set(bool on) {
    if (!onboard_led.allocated) {
        printf("ERROR: LED not allocated to this container\n");
        return -1;
    }

    gpio_put(LED_PIN, on);
    led_state.is_on = on;
    if (on) led_state.blink_count++;

    return 0;
}
```

#### Example: GPIO Pins

**File: `bsp/YOUR-BOARD/devices/gpio_pins.c`**

```c
#include "hardware_manager.h"
#include "YOUR_BOARD_gpio.h"

#define NUM_GPIO 29  // Change to your board's GPIO count

// GPIO capabilities per pin
typedef struct {
    uint8_t pin_number;
    bool supports_pwm;
    bool supports_i2c;
    bool supports_spi;
    bool supports_adc;
} gpio_capabilities_t;

// GPIO state per pin
typedef struct {
    uint8_t pin_number;
    int mode;  // INPUT, OUTPUT, etc.
    bool value;
} gpio_state_t;

// Device operations
static int gpio_init(hardware_device_t *self) {
    gpio_state_t *state = (gpio_state_t *)self->private_data;

    // Initialize GPIO pin (YOUR_BOARD specific)
    gpio_init(state->pin_number);

    return 0;
}

static int gpio_allocate(hardware_device_t *self, const char *container_id) {
    if (self->allocated) return -1;

    self->allocated = true;
    self->allocated_to_container = strdup(container_id);
    self->state = HW_STATE_ALLOCATED;

    DEBUG_PRINT("GPIO pin %d allocated",
                ((gpio_state_t *)self->private_data)->pin_number);

    return 0;
}

static int gpio_release(hardware_device_t *self) {
    gpio_state_t *state = (gpio_state_t *)self->private_data;

    // Reset pin to safe state (input)
    gpio_init(state->pin_number);
    gpio_set_dir(state->pin_number, GPIO_IN);

    free(self->allocated_to_container);
    self->allocated_to_container = NULL;
    self->allocated = false;
    self->state = HW_STATE_AVAILABLE;

    return 0;
}

// Create GPIO devices (one per pin)
hardware_device_t gpio_pins[NUM_GPIO];

void gpio_pins_register(void) {
    for (int i = 0; i < NUM_GPIO; i++) {
        // Allocate capabilities
        gpio_capabilities_t *caps = malloc(sizeof(gpio_capabilities_t));
        caps->pin_number = i;

        // Set capabilities based on your board's features
        // This is board-specific knowledge
        caps->supports_pwm = /* YOUR BOARD - which pins have PWM? */;
        caps->supports_i2c = /* YOUR BOARD - which pins are I2C? */;
        caps->supports_spi = /* YOUR BOARD - which pins are SPI? */;
        caps->supports_adc = /* YOUR BOARD - which pins are ADC? */;

        // Allocate state
        gpio_state_t *state = malloc(sizeof(gpio_state_t));
        state->pin_number = i;
        state->mode = GPIO_IN;
        state->value = false;

        // Initialize device descriptor
        gpio_pins[i].name = malloc(16);
        snprintf(gpio_pins[i].name, 16, "gpio-pin-%d", i);
        gpio_pins[i].resource_name = "YOUR-BOARD.io/gpio-pin";
        gpio_pins[i].type = HW_TYPE_GPIO_PIN;
        gpio_pins[i].state = HW_STATE_AVAILABLE;
        gpio_pins[i].capabilities = caps;
        gpio_pins[i].private_data = state;
        gpio_pins[i].init = gpio_init;
        gpio_pins[i].allocate = gpio_allocate;
        gpio_pins[i].release = gpio_release;
        gpio_pins[i].get_status = gpio_get_status;

        // Register with hardware manager
        hardware_manager_register_device(&gpio_pins[i]);
    }
}
```

#### Example: Temperature Sensor

**File: `bsp/YOUR-BOARD/devices/temp_sensor.c`**

```c
#include "hardware_manager.h"
#include "YOUR_BOARD_adc.h"  // Or I2C if external sensor

// Sensor capabilities
typedef struct {
    float min_temp_celsius;
    float max_temp_celsius;
    float accuracy_celsius;
    uint32_t sample_rate_hz;
} temp_sensor_capabilities_t;

static temp_sensor_capabilities_t temp_caps = {
    .min_temp_celsius = -40.0,
    .max_temp_celsius = 85.0,
    .accuracy_celsius = 2.0,
    .sample_rate_hz = 100
};

// Sensor state
typedef struct {
    float last_reading;
    absolute_time_t last_sample_time;
    uint32_t sample_count;
} temp_sensor_state_t;

static temp_sensor_state_t temp_state = {0};

// Device operations
static int temp_sensor_init(hardware_device_t *self) {
    // Initialize temperature sensor hardware
    // YOUR_BOARD specific:
    // - If internal ADC-based sensor: initialize ADC
    // - If external I2C sensor: initialize I2C, detect sensor

    // Example for RP2040 internal sensor:
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);

    return 0;
}

static int temp_sensor_allocate(hardware_device_t *self,
                                const char *container_id) {
    if (self->allocated) return -1;

    self->allocated = true;
    self->allocated_to_container = strdup(container_id);
    self->state = HW_STATE_ALLOCATED;

    return 0;
}

static int temp_sensor_release(hardware_device_t *self) {
    free(self->allocated_to_container);
    self->allocated_to_container = NULL;
    self->allocated = false;
    self->state = HW_STATE_AVAILABLE;

    return 0;
}

// Public device descriptor
hardware_device_t temp_sensor = {
    .name = "onboard-temp-sensor",
    .resource_name = "YOUR-BOARD.io/temp-sensor",
    .type = HW_TYPE_TEMP_SENSOR,
    .state = HW_STATE_AVAILABLE,
    .capabilities = &temp_caps,
    .private_data = &temp_state,
    .init = temp_sensor_init,
    .allocate = temp_sensor_allocate,
    .release = temp_sensor_release,
    .get_status = temp_sensor_get_status
};

// Temperature reading API
float temp_sensor_read(void) {
    if (!temp_sensor.allocated) {
        printf("ERROR: Temp sensor not allocated\n");
        return -273.15;  // Error indicator
    }

    // Read temperature (YOUR_BOARD specific)
    // Example for RP2040:
    uint16_t adc_value = adc_read();
    float voltage = adc_value * 3.3f / 4096.0f;
    float temp_celsius = 27.0f - (voltage - 0.706f) / 0.001721f;

    temp_state.last_reading = temp_celsius;
    temp_state.last_sample_time = get_absolute_time();
    temp_state.sample_count++;

    return temp_celsius;
}
```

### Step 5: Create Build Configuration

**File: `bsp/YOUR-BOARD/CMakeLists.txt`**

```cmake
# BSP library for YOUR-BOARD

add_library(bsp_YOUR-BOARD
    hardware_manager_YOUR_BOARD.c
    bsp_YOUR_BOARD.c
    devices/led.c
    devices/gpio_pins.c
    devices/temp_sensor.c
    # Add more device drivers as needed
)

# Include directories
target_include_directories(bsp_YOUR-BOARD PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Link with board SDK libraries
target_link_libraries(bsp_YOUR-BOARD
    # YOUR_BOARD specific libraries
    # Examples:
    # - Pico SDK: pico_stdlib, hardware_gpio, hardware_adc
    # - ESP32: idf::driver, idf::nvs_flash
    # - STM32: HAL_Driver, CMSIS
)

# Board-specific compile definitions
target_compile_definitions(bsp_YOUR-BOARD PUBLIC
    BOARD_NAME="YOUR-BOARD"
    NUM_GPIO=29  # Your board's GPIO count
    # Add more as needed
)
```

### Step 6: Update Top-Level Build System

**File: `CMakeLists.txt` (top level)**

Add your board to the build options:

```cmake
# Select board (default: pico-w)
set(BOARD "pico-w" CACHE STRING "Target board")

# Board options
set_property(CACHE BOARD PROPERTY STRINGS
    pico-w
    pico-2
    esp32-c3
    YOUR-BOARD  # Add your board
)

# Include board-specific build config
if(BOARD STREQUAL "pico-w")
    add_subdirectory(bsp/pico-w)
    set(PICO_BOARD pico_w)
    set(BSP_TARGET bsp_pico-w)

elseif(BOARD STREQUAL "YOUR-BOARD")
    add_subdirectory(bsp/YOUR-BOARD)
    # Set board-specific variables
    set(BSP_TARGET bsp_YOUR-BOARD)
    # Add any YOUR-BOARD specific toolchain setup

else()
    message(FATAL_ERROR "Unknown board: ${BOARD}")
endif()

# Main executable (portable code)
add_executable(k3s_node
    src/k3s_client.c
    src/pod_manager.c
    src/resource_manager.c
    src/node_status.c
    src/configmap_watcher.c
    src/kubelet_server.c
    src/container.c
    src/memory_manager.c
    src/hardware_manager.c  # Core hardware manager (portable)
    src/main.c
)

# Link with BSP
target_link_libraries(k3s_node
    ${BSP_TARGET}  # Your board's BSP library
    # Common libraries (lwIP, mbedtls, etc.)
)
```

### Step 7: Build and Test

```bash
# Configure build
mkdir build && cd build
cmake -DBOARD=YOUR-BOARD ..

# Build
make -j4

# Output: k3s_node.uf2 (or .bin, .hex depending on board)
```

### Step 8: Flash and Verify

1. **Flash the firmware** to your board
2. **Connect serial console** (115200 baud)
3. **Verify output:**

```
========================================
  YOUR-BOARD - K3s Node
========================================
Node Name: YOUR-BOARD-node-1
K3s Server: 192.168.86.232:6443

Discovering hardware for YOUR-BOARD...
Hardware discovery complete:
  - 1 LEDs
  - 1 sensors
  - 29 GPIO pins

Connecting to WiFi...
WiFi connected! IP address: 192.168.86.249

Initializing subsystems...
  [1/5] Memory manager...
  [2/5] K3s API client...
  [3/5] Kubelet server...
  [4/5] ConfigMap watcher...
  [5/5] Registering node with k3s...

System ready! Entering main loop...
```

4. **Test hardware resources:**

```bash
# Check node shows up in K8s
kubectl get nodes

# Check extended resources
kubectl describe node YOUR-BOARD-node-1
# Should show:
#   Capacity:
#     YOUR-BOARD.io/onboard-led: 1
#     YOUR-BOARD.io/gpio-pin: 29
#     YOUR-BOARD.io/temp-sensor: 1
```

## Common Porting Issues

### Issue 1: Different GPIO API

**Problem:** Your board's GPIO functions are different

**Solution:** Create a thin wrapper layer

```c
// bsp/YOUR-BOARD/gpio_wrapper.c

// Pico SDK uses: gpio_put(pin, value)
// YOUR_BOARD uses: HAL_GPIO_WritePin(port, pin, value)

void gpio_put(uint8_t pin, bool value) {
    GPIO_TypeDef *port = get_port_from_pin(pin);
    uint16_t pin_mask = get_pin_mask(pin);
    HAL_GPIO_WritePin(port, pin_mask, value ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
```

### Issue 2: No ADC for Temperature

**Problem:** Board has no internal temperature sensor

**Solution:** Add external I2C/SPI sensor or skip temperature device

```c
// Option 1: External sensor (e.g., TMP102 over I2C)
int temp_sensor_init(hardware_device_t *self) {
    i2c_init(i2c0, 400000);
    // Configure TMP102
    uint8_t config[] = {0x01, 0x60, 0xA0};
    i2c_write_blocking(i2c0, TMP102_ADDR, config, 3, false);
    return 0;
}

// Option 2: Skip temperature sensor entirely
int hardware_manager_discover(void) {
    // Don't register temp_sensor
    // Only register LED and GPIO
}
```

### Issue 3: Different Network Stack

**Problem:** Board uses different WiFi/Ethernet stack

**Solution:** Implement network abstraction layer

```c
// bsp/YOUR-BOARD/network_YOUR_BOARD.c

// Pico uses: cyw43_arch_init()
// ESP32 uses: esp_wifi_init()
// YOUR_BOARD uses: ???

int network_init(void) {
    // YOUR_BOARD specific initialization
    // Return 0 on success, -1 on failure
}

int network_connect(const char *ssid, const char *password) {
    // YOUR_BOARD specific WiFi connection
}
```

### Issue 4: Limited RAM

**Problem:** Board has less than 200KB RAM

**Solution:** Reduce buffer sizes, disable features

```c
// bsp/YOUR-BOARD/config_YOUR_BOARD.h

// Reduce TLS buffers
#define MBEDTLS_SSL_MAX_CONTENT_LEN 4096  // Down from 8192

// Reduce pod limit
#define MAX_PODS 3  // Down from 10

// Disable features
#define ENABLE_METRICS_API 0  // Disable metrics
#define ENABLE_CONFIGMAP_WATCHER 0  // Disable ConfigMap
```

### Issue 5: Different Toolchain

**Problem:** Board uses different compiler/linker

**Solution:** Update CMake toolchain file

```cmake
# bsp/YOUR-BOARD/toolchain.cmake

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

# YOUR_BOARD specific flags
set(CMAKE_C_FLAGS "-mcpu=cortex-m4 -mthumb ...")
```

## Testing Your Port

### Checklist

- [ ] Board initializes successfully
- [ ] Serial output appears
- [ ] WiFi/Ethernet connects
- [ ] Hardware discovery finds all devices
- [ ] Node registers with K8s
- [ ] Node status updates work
- [ ] Extended resources appear in `kubectl describe node`
- [ ] Test pod can allocate hardware (LED blink test)
- [ ] Hardware releases when pod stops
- [ ] System runs stably for 10+ minutes

### Test Pod: Blinker

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: blinker-test
spec:
  nodeSelector:
    kubernetes.io/hostname: YOUR-BOARD-node-1
  containers:
  - name: blink
    image: pico/blinker:v1
    resources:
      limits:
        YOUR-BOARD.io/onboard-led: 1
```

## Example Ports

### Pico W (Reference Implementation)

See `bsp/pico-w/` for the reference implementation.

**Hardware:**
- RP2040 chip (ARM Cortex-M0+, 133MHz)
- 264KB RAM
- 2MB Flash (can be up to 16MB)
- CYW43439 WiFi chip
- 1 LED (controlled via CYW43)
- 29 GPIO pins
- 1 internal temperature sensor

### Pico 2 (Extended)

See `bsp/pico-2/` for an extended implementation.

**Differences from Pico W:**
- RP2350 chip (ARM Cortex-M33, 150MHz)
- 520KB RAM (more memory!)
- Up to 4MB Flash
- 47 GPIO pins (more pins!)
- 2 temperature sensors
- PSRAM support (optional)

### ESP32-C3 (Different Architecture)

**Hardware:**
- RISC-V chip (not ARM!)
- 400KB RAM
- 4MB Flash
- Built-in WiFi
- 2 LEDs
- 22 GPIO pins
- Hall effect sensor

**Porting notes:**
- Different toolchain (ESP-IDF)
- Different network stack (esp_wifi)
- Different GPIO API
- Need to port lwIP integration

## Submitting Your Port

Once your port is working:

1. **Test thoroughly** - Run for 24+ hours
2. **Document hardware** - Update BSP README
3. **Create PR** - Submit to main repository
4. **Include examples** - Add test pod manifests

**PR Template:**

```markdown
# Add support for YOUR-BOARD

## Hardware
- Chip: YOUR_CHIP (ARM Cortex-Mx)
- RAM: XXX KB
- Flash: XXX MB
- Network: WiFi/Ethernet
- Peripherals: X LEDs, Y GPIO, Z sensors

## Testing
- [x] Board initializes
- [x] Network connects
- [x] Hardware discovery works
- [x] Node registers with K8s
- [x] Test pod runs successfully
- [x] Stable for 24+ hours

## Files Added
- bsp/YOUR-BOARD/
- Documentation
- Example pod manifests
```

## Resources

- **Hardware Manager API**: See `HARDWARE_RESOURCE_MANAGEMENT.md`
- **Architecture**: See `ARCHITECTURE_PLAN.md`
- **Reference BSP**: `bsp/pico-w/`
- **Example Device Drivers**: `bsp/pico-w/devices/`

## Getting Help

- **GitHub Issues**: https://github.com/mak3r/k3s-pico-node/issues
- **Discussion**: Tag with "porting" label
- **Community**: Share your progress!

## Summary

**Porting to a new board requires:**

1. ✅ Create BSP directory
2. ✅ Implement `hardware_manager_discover()`
3. ✅ Write device drivers (LED, GPIO, sensors)
4. ✅ Update build system
5. ✅ Test with K8s

**Effort estimate:**
- Simple board (similar to Pico): 1-2 days
- Different architecture: 1-2 weeks
- Custom peripherals: +1 day per device

**Most of the code (~80%) is reusable!** Only board-specific drivers need to be written.

Happy porting! 🚀
