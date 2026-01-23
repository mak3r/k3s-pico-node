# Hardware Resource Management

## Overview

The K3s Pico Node manages physical hardware (sensors, actuators, GPIO, etc.) as Kubernetes-schedulable resources. This document describes how hardware resources are discovered, allocated, and reported to the Kubernetes control plane.

## Problem Statement

Kubernetes was designed to schedule workloads on nodes with CPU, memory, and storage. Modern workloads also need access to specialized hardware:
- **Cloud:** GPUs, TPUs, FPGAs
- **Edge/IoT:** Sensors, actuators, LEDs, GPIO pins, communication interfaces

The Pico needs to:
1. **Discover** what hardware exists (LED, temp sensor, GPIO pins)
2. **Track quantity** (1 LED, 29 GPIO pins, 2 temp sensors)
3. **Monitor availability** (in use vs. free)
4. **Allocate** hardware to containers
5. **Report** to Kubernetes for scheduling decisions

## Kubernetes Mechanisms

### Extended Resources (Quantifiable Hardware)

Declared in node status, visible to scheduler:

```yaml
status:
  capacity:
    cpu: "2"
    memory: "264Ki"
    pico.io/onboard-led: "1"      # Custom resource
    pico.io/gpio-pin: "29"        # 29 available
    pico.io/temp-sensor: "1"
  allocatable:
    cpu: "1800m"
    memory: "200Ki"
    pico.io/onboard-led: "1"
    pico.io/gpio-pin: "29"
    pico.io/temp-sensor: "1"
```

Pods request them in resource limits/requests:

```yaml
spec:
  containers:
  - name: blink
    resources:
      limits:
        pico.io/onboard-led: 1    # Request LED
```

**Scheduler logic:**
1. Checks node has capacity
2. Checks resource is available (not allocated)
3. Schedules pod if available
4. Kubelet allocates resource to container

### Node Labels (Static Capabilities)

For node selection (hardware exists or not):

```yaml
metadata:
  labels:
    hardware.pico.io/has-temp-sensor: "true"
    hardware.pico.io/has-led-matrix: "true"
    hardware.pico.io/board: "pico-w"
    hardware.pico.io/gpio-count: "29"
```

Pods use node selectors:

```yaml
spec:
  nodeSelector:
    hardware.pico.io/has-temp-sensor: "true"
```

### Pod Annotations (Operational Metadata)

For tracking and debugging:

```yaml
metadata:
  annotations:
    hardware.pico.io/allocated-led: "onboard-led-0"
    hardware.pico.io/allocated-gpio: "pins-0,1,2"
    hardware.pico.io/sensor-calibration: "2026-01-20"
```

## Architecture

### Hardware Manager (Layer 0)

New layer below the existing architecture, responsible for:

```
┌─────────────────────────────────────────────────────────────┐
│  Layer 3: K8s Resource Abstraction                          │
│  - Pod Manager (allocates hardware to containers)           │
│  - Node Manager (reports hardware to K8s)                   │
│  - Resource Manager (coordinates allocation)                │
└─────────────────────────────────────────────────────────────┘
                         ↓ Uses ↓
┌─────────────────────────────────────────────────────────────┐
│  Layer 0: Hardware Manager (NEW)                            │
│  ┌────────────────────────────────────────────────────────┐ │
│  │  Hardware Manager API                                  │ │
│  │  - Discovery (what hardware exists)                   │ │
│  │  - Allocation (assign to container)                   │ │
│  │  - Tracking (who's using what)                        │ │
│  │  - Reporting (capacity/allocatable)                   │ │
│  └────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────┐ │
│  │  Hardware Devices (Pluggable)                         │ │
│  │  - Onboard LED                                        │ │
│  │  - GPIO Pins (29 instances)                           │ │
│  │  - Temperature Sensor                                 │ │
│  │  - LED Matrix                                         │ │
│  │  - Custom devices                                     │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

## Hardware Manager API

### Core Types

```c
// include/hardware_manager.h

// Device types
typedef enum {
    HW_TYPE_LED,
    HW_TYPE_GPIO_PIN,
    HW_TYPE_TEMP_SENSOR,
    HW_TYPE_PRESSURE_SENSOR,
    HW_TYPE_ACCELEROMETER,
    HW_TYPE_LED_MATRIX,
    HW_TYPE_I2C_DEVICE,
    HW_TYPE_SPI_DEVICE,
    HW_TYPE_ADC_CHANNEL,
    HW_TYPE_PWM_CHANNEL,
    HW_TYPE_CUSTOM
} hardware_type_t;

// Device state
typedef enum {
    HW_STATE_AVAILABLE,     // Free to allocate
    HW_STATE_ALLOCATED,     // In use
    HW_STATE_FAILED,        // Hardware failure
    HW_STATE_DISABLED       // Admin disabled
} hardware_state_t;

// Hardware device descriptor
typedef struct hardware_device {
    // Identity
    char *name;                     // "onboard-led", "gpio-pin-0"
    char *resource_name;            // "pico.io/onboard-led"
    hardware_type_t type;
    hardware_state_t state;

    // Allocation tracking
    bool allocated;
    char *allocated_to_container;   // Container ID
    char *allocated_to_pod;         // Pod name/namespace
    absolute_time_t allocated_at;   // When allocated

    // Capabilities (device-specific metadata)
    void *capabilities;

    // Operations (function pointers)
    int (*init)(struct hardware_device *self);
    int (*allocate)(struct hardware_device *self, const char *container_id);
    int (*release)(struct hardware_device *self);
    int (*get_status)(struct hardware_device *self, char *json, size_t size);

    // Private data (implementation-specific)
    void *private_data;
} hardware_device_t;
```

### Management Functions

```c
// Initialization
int hardware_manager_init(void);

// Discovery (called during boot)
int hardware_manager_discover(void);

// Registration (devices register themselves)
int hardware_manager_register_device(hardware_device_t *device);
int hardware_manager_unregister_device(const char *name);

// Lookup
hardware_device_t* hardware_manager_find(const char *resource_name);
hardware_device_t* hardware_manager_find_by_type(hardware_type_t type);
int hardware_manager_list(char *json, size_t size);

// Allocation
int hardware_manager_allocate(const char *resource_name,
                              const char *container_id,
                              const char *pod_name);
int hardware_manager_release(const char *resource_name,
                            const char *container_id);
int hardware_manager_release_all(const char *container_id);
bool hardware_manager_is_available(const char *resource_name);

// Reporting (for K8s node status)
int hardware_manager_get_capacity(char *json, size_t size);
int hardware_manager_get_allocatable(char *json, size_t size);
int hardware_manager_get_labels(char *json, size_t size);
int hardware_manager_get_usage(char *json, size_t size);
```

## Device Implementation

### Example: Onboard LED

```c
// bsp/pico-w/devices/onboard_led.c

#include "hardware_manager.h"
#include "pico/cyw43_arch.h"

// Capabilities
typedef struct {
    bool supports_pwm;
    uint8_t max_brightness;
    uint32_t max_frequency_hz;
} led_capabilities_t;

static led_capabilities_t led_caps = {
    .supports_pwm = false,
    .max_brightness = 1,  // On/off only
    .max_frequency_hz = 1000
};

// LED state
typedef struct {
    bool is_on;
    uint32_t blink_count;
} led_state_t;

static led_state_t led_state = {0};

// Initialize LED hardware
static int led_init(hardware_device_t *self) {
    // LED is controlled via CYW43 WiFi chip on Pico W
    // Assumes cyw43_arch_init() already called
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

    DEBUG_PRINT("LED initialized");
    return 0;
}

// Allocate LED to container
static int led_allocate(hardware_device_t *self, const char *container_id) {
    if (self->allocated) {
        printf("LED already allocated to: %s\n", self->allocated_to_container);
        return -1;
    }

    self->allocated = true;
    self->allocated_to_container = strdup(container_id);
    self->allocated_at = get_absolute_time();
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
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    led_state.is_on = false;

    // Clear allocation
    free(self->allocated_to_container);
    self->allocated_to_container = NULL;
    self->allocated_to_pod = NULL;
    self->allocated = false;
    self->state = HW_STATE_AVAILABLE;

    DEBUG_PRINT("LED released");
    return 0;
}

// Get LED status
static int led_get_status(hardware_device_t *self, char *json, size_t size) {
    snprintf(json, size,
        "{"
        "  \"name\": \"%s\","
        "  \"state\": \"%s\","
        "  \"allocated\": %s,"
        "  \"allocatedTo\": \"%s\","
        "  \"isOn\": %s,"
        "  \"blinkCount\": %u"
        "}",
        self->name,
        self->state == HW_STATE_AVAILABLE ? "available" : "allocated",
        self->allocated ? "true" : "false",
        self->allocated_to_container ? self->allocated_to_container : "",
        led_state.is_on ? "true" : "false",
        led_state.blink_count);

    return 0;
}

// Public LED device
hardware_device_t onboard_led = {
    .name = "onboard-led",
    .resource_name = "pico.io/onboard-led",
    .type = HW_TYPE_LED,
    .state = HW_STATE_AVAILABLE,
    .allocated = false,
    .capabilities = &led_caps,
    .init = led_init,
    .allocate = led_allocate,
    .release = led_release,
    .get_status = led_get_status,
    .private_data = &led_state
};

// LED control API (for containers to use)
int led_set(bool on) {
    if (!onboard_led.allocated) {
        printf("ERROR: LED not allocated to this container\n");
        return -1;
    }

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
    led_state.is_on = on;
    if (on) led_state.blink_count++;

    return 0;
}
```

### Example: GPIO Pins

```c
// bsp/pico-w/devices/gpio_pins.c

typedef struct {
    uint8_t pin_number;
    bool supports_pwm;
    bool supports_i2c;
    bool supports_spi;
} gpio_capabilities_t;

typedef struct {
    uint8_t pin_number;
    int mode;  // INPUT, OUTPUT, etc.
    bool value;
} gpio_state_t;

static int gpio_init(hardware_device_t *self) {
    gpio_state_t *state = (gpio_state_t *)self->private_data;
    gpio_init(state->pin_number);
    return 0;
}

static int gpio_allocate(hardware_device_t *self, const char *container_id) {
    if (self->allocated) return -1;

    self->allocated = true;
    self->allocated_to_container = strdup(container_id);
    self->state = HW_STATE_ALLOCATED;

    return 0;
}

static int gpio_release(hardware_device_t *self) {
    gpio_state_t *state = (gpio_state_t *)self->private_data;

    // Reset pin to safe state
    gpio_init(state->pin_number);
    gpio_set_dir(state->pin_number, GPIO_IN);

    free(self->allocated_to_container);
    self->allocated_to_container = NULL;
    self->allocated = false;
    self->state = HW_STATE_AVAILABLE;

    return 0;
}

// Create 29 GPIO devices (one per pin on Pico W)
hardware_device_t gpio_pins[29];

void gpio_pins_register(void) {
    for (int i = 0; i < 29; i++) {
        gpio_capabilities_t *caps = malloc(sizeof(gpio_capabilities_t));
        caps->pin_number = i;
        caps->supports_pwm = (i >= 0 && i <= 15);  // PWM channels
        caps->supports_i2c = (i == 4 || i == 5);   // I2C pins
        caps->supports_spi = (i == 16 || i == 17 || i == 18 || i == 19);

        gpio_state_t *state = malloc(sizeof(gpio_state_t));
        state->pin_number = i;
        state->mode = GPIO_IN;
        state->value = false;

        gpio_pins[i].name = malloc(16);
        snprintf(gpio_pins[i].name, 16, "gpio-pin-%d", i);
        gpio_pins[i].resource_name = "pico.io/gpio-pin";  // Same for all
        gpio_pins[i].type = HW_TYPE_GPIO_PIN;
        gpio_pins[i].state = HW_STATE_AVAILABLE;
        gpio_pins[i].capabilities = caps;
        gpio_pins[i].private_data = state;
        gpio_pins[i].init = gpio_init;
        gpio_pins[i].allocate = gpio_allocate;
        gpio_pins[i].release = gpio_release;
        gpio_pins[i].get_status = gpio_get_status;

        hardware_manager_register_device(&gpio_pins[i]);
    }
}
```

### Example: Temperature Sensor

```c
// bsp/pico-w/devices/temp_sensor.c

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

typedef struct {
    float last_reading;
    absolute_time_t last_sample_time;
    uint32_t sample_count;
} temp_sensor_state_t;

static temp_sensor_state_t temp_state = {0};

static int temp_sensor_init(hardware_device_t *self) {
    // Initialize ADC for temperature sensor
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);  // Temperature sensor channel

    return 0;
}

static int temp_sensor_allocate(hardware_device_t *self, const char *container_id) {
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

hardware_device_t temp_sensor = {
    .name = "onboard-temp-sensor",
    .resource_name = "pico.io/temp-sensor",
    .type = HW_TYPE_TEMP_SENSOR,
    .state = HW_STATE_AVAILABLE,
    .capabilities = &temp_caps,
    .private_data = &temp_state,
    .init = temp_sensor_init,
    .allocate = temp_sensor_allocate,
    .release = temp_sensor_release,
    .get_status = temp_sensor_get_status
};

// Temperature reading API (for containers)
float temp_sensor_read(void) {
    if (!temp_sensor.allocated) {
        printf("ERROR: Temp sensor not allocated\n");
        return -273.15;  // Absolute zero = error
    }

    // Read ADC
    uint16_t adc_value = adc_read();

    // Convert to temperature (Pico W formula)
    float voltage = adc_value * 3.3f / 4096.0f;
    float temp_celsius = 27.0f - (voltage - 0.706f) / 0.001721f;

    temp_state.last_reading = temp_celsius;
    temp_state.last_sample_time = get_absolute_time();
    temp_state.sample_count++;

    return temp_celsius;
}
```

## Integration with Pod Manager

### Container Resource Requests

When a pod spec requests hardware:

```yaml
spec:
  containers:
  - name: blink
    image: pico/blinker:v1  # ECI (embedded container image)
    resources:
      limits:
        memory: "64Ki"
        cpu: "500m"
        pico.io/onboard-led: 1        # Hardware resource
        pico.io/gpio-pin: 2           # Need 2 GPIO pins
```

### Pod Manager Allocates Hardware

```c
// src/pod_manager.c

int pod_start_container(pod_t *pod, container_t *container) {
    // Parse resource limits from container spec
    resource_limits_t *limits = parse_resource_limits(container->spec);

    // Allocate hardware resources
    for (int i = 0; i < limits->hw_resource_count; i++) {
        hw_resource_request_t *req = &limits->hw_resources[i];

        // Example: "pico.io/onboard-led": 1
        for (int j = 0; j < req->quantity; j++) {
            int ret = hardware_manager_allocate(
                req->resource_name,
                container->id,
                pod->name
            );

            if (ret != 0) {
                // Allocation failed - hardware not available
                pod_set_status(pod, POD_PHASE_FAILED, "InsufficientHardware");

                // Release any already-allocated hardware
                hardware_manager_release_all(container->id);

                return -1;
            }

            // Track allocation
            container->allocated_hardware[container->hw_count++] =
                strdup(req->resource_name);
        }
    }

    DEBUG_PRINT("Allocated %d hardware resources to container %s",
                container->hw_count, container->id);

    // Start container
    container->running = true;
    container->main_func(container->state);

    return 0;
}

int pod_stop_container(pod_t *pod, container_t *container) {
    // Stop container
    container->running = false;

    // Release all hardware
    int ret = hardware_manager_release_all(container->id);
    if (ret != 0) {
        printf("WARNING: Failed to release hardware for container %s\n",
               container->id);
    }

    // Free tracking
    for (int i = 0; i < container->hw_count; i++) {
        free(container->allocated_hardware[i]);
    }
    container->hw_count = 0;

    return 0;
}
```

## Integration with Node Manager

### Report Hardware as Extended Resources

```c
// src/node_status.c

int node_status_report(void) {
    char json[8192];
    int offset = 0;

    // Get hardware capacity
    char hw_capacity[2048];
    hardware_manager_get_capacity(hw_capacity, sizeof(hw_capacity));

    // Get hardware allocatable
    char hw_allocatable[2048];
    hardware_manager_get_allocatable(hw_allocatable, sizeof(hw_allocatable));

    // Build node status JSON
    offset += snprintf(json + offset, sizeof(json) - offset,
        "{"
        "  \"status\": {"
        "    \"capacity\": {"
        "      \"cpu\": \"2\","
        "      \"memory\": \"264Ki\","
        "      \"pods\": \"10\","
        "      \"ephemeral-storage\": \"2Gi\","
        "      %s"  // Hardware: "pico.io/onboard-led": "1", ...
        "    },"
        "    \"allocatable\": {"
        "      \"cpu\": \"1800m\","
        "      \"memory\": \"200Ki\","
        "      \"pods\": \"10\","
        "      \"ephemeral-storage\": \"1900Mi\","
        "      %s"  // Hardware
        "    },"
        "    \"conditions\": [...]"
        "  }"
        "}",
        hw_capacity,
        hw_allocatable);

    // Send to K8s API
    return k3s_patch("/api/v1/nodes/" K3S_NODE_NAME "/status", json);
}
```

### Hardware Capacity JSON Format

```c
int hardware_manager_get_capacity(char *json, size_t size) {
    int offset = 0;

    // Count resources by type
    typedef struct {
        char *resource_name;
        int count;
    } resource_count_t;

    resource_count_t counts[32] = {0};
    int unique_count = 0;

    // Iterate all devices
    for (int i = 0; i < device_count; i++) {
        hardware_device_t *dev = devices[i];

        // Find or create entry
        int idx = -1;
        for (int j = 0; j < unique_count; j++) {
            if (strcmp(counts[j].resource_name, dev->resource_name) == 0) {
                idx = j;
                break;
            }
        }

        if (idx == -1) {
            // New resource type
            counts[unique_count].resource_name = dev->resource_name;
            counts[unique_count].count = 1;
            unique_count++;
        } else {
            counts[idx].count++;
        }
    }

    // Build JSON
    for (int i = 0; i < unique_count; i++) {
        offset += snprintf(json + offset, size - offset,
            "\"%s\": \"%d\"",
            counts[i].resource_name,
            counts[i].count);

        if (i < unique_count - 1) {
            offset += snprintf(json + offset, size - offset, ",");
        }
    }

    return 0;
}
```

### Report Node Labels

```c
int hardware_manager_get_labels(char *json, size_t size) {
    int offset = 0;

    // Static labels based on hardware presence
    bool has_led = (hardware_manager_find("pico.io/onboard-led") != NULL);
    bool has_temp = (hardware_manager_find("pico.io/temp-sensor") != NULL);
    bool has_matrix = (hardware_manager_find("pico.io/led-matrix") != NULL);

    int gpio_count = 0;
    for (int i = 0; i < device_count; i++) {
        if (devices[i]->type == HW_TYPE_GPIO_PIN) {
            gpio_count++;
        }
    }

    offset += snprintf(json + offset, size - offset,
        "\"hardware.pico.io/has-led\": \"%s\","
        "\"hardware.pico.io/has-temp-sensor\": \"%s\","
        "\"hardware.pico.io/has-led-matrix\": \"%s\","
        "\"hardware.pico.io/gpio-count\": \"%d\","
        "\"hardware.pico.io/board\": \"pico-w\"",
        has_led ? "true" : "false",
        has_temp ? "true" : "false",
        has_matrix ? "true" : "false",
        gpio_count);

    return 0;
}
```

## Complete Flow Example

### Scenario: Blinker Pod

**1. Pod Manifest:**

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: blinker
  namespace: default
spec:
  nodeSelector:
    hardware.pico.io/has-led: "true"
  containers:
  - name: blink
    image: pico/blinker:v1  # ECI (embedded container image)
    resources:
      limits:
        memory: "64Ki"
        cpu: "500m"
        pico.io/onboard-led: 1
```

**2. Kubernetes Scheduler:**
- Checks node labels: `hardware.pico.io/has-led: "true"` ✅
- Checks capacity: `pico.io/onboard-led: 1` ✅
- Checks allocatable: `pico.io/onboard-led: 1` ✅ (not in use)
- Schedules pod to `pico-node-1`

**3. Pico Receives Pod Assignment:**

```c
// resource_manager_apply() receives manifest
resource_manager_apply(manifest_json);
    // Check hardware availability
    if (!hardware_manager_is_available("pico.io/onboard-led")) {
        pod_set_status(pod, POD_PHASE_PENDING, "InsufficientHardware");
        return -1;
    }

    // Create pod
    pod_t *pod = pod_manager_create(manifest);

    // Start container
    pod_start_container(pod, 0);
        // Allocate LED
        hardware_manager_allocate("pico.io/onboard-led",
                                 container->id,
                                 "default/blinker");

        // LED is now allocated ✅

        // Start container code
        container->main_func(container->state);
```

**4. Container Runs:**

```c
// Container firmware (blink_main)
int blink_main(void *state) {
    // LED was allocated by pod manager
    // Container can now use it

    led_set(true);
    sleep_ms(500);
    led_set(false);
    sleep_ms(500);

    return 0;  // Keep running
}
```

**5. Pod Stops:**

```c
pod_stop_container(pod, 0);
    // Release LED
    hardware_manager_release_all(container->id);
        // LED marked available again
```

**6. Node Status Updates:**

```c
node_status_report();
    // Reports:
    // "pico.io/onboard-led": "1" (capacity)
    // "pico.io/onboard-led": "1" (allocatable - now free)
```

## Implementation Files

### Core Implementation

```
include/
  hardware_manager.h          # Abstract API (portable)

src/
  hardware_manager.c          # Core logic (mostly portable)
                             # - Device registry
                             # - Allocation tracking
                             # - Reporting functions

bsp/
  pico-w/
    hardware_manager_pico.c   # Pico W discovery
    devices/
      onboard_led.c           # LED driver
      gpio_pins.c             # GPIO driver
      temp_sensor.c           # Temp sensor driver

  pico-2/
    hardware_manager_pico2.c  # Pico 2 discovery
    devices/
      # Similar but with more GPIO, PSRAM, etc.
```

### Integration Points

```
src/main.c:
  - hardware_manager_init()
  - hardware_manager_discover()

src/node_status.c:
  - hardware_manager_get_capacity()
  - hardware_manager_get_allocatable()
  - hardware_manager_get_labels()

src/pod_manager.c:
  - hardware_manager_allocate()
  - hardware_manager_release_all()
  - hardware_manager_is_available()

src/resource_manager.c:
  - hardware_manager_is_available() (pre-check)
```

## Testing Hardware Resources

### Manual Testing with kubectl

```bash
# View node with hardware resources
kubectl describe node pico-node-1

# Should show:
# Capacity:
#   cpu:                      2
#   memory:                   264Ki
#   pico.io/gpio-pin:        29
#   pico.io/onboard-led:     1
#   pico.io/temp-sensor:     1
#   pods:                     10
# Allocatable:
#   cpu:                      1800m
#   memory:                   200Ki
#   pico.io/gpio-pin:        29
#   pico.io/onboard-led:     1
#   pico.io/temp-sensor:     1
#   pods:                     10
```

### Deploy Test Pod

```bash
# Deploy blinker pod
kubectl apply -f - <<EOF
apiVersion: v1
kind: Pod
metadata:
  name: blinker
spec:
  nodeSelector:
    hardware.pico.io/has-led: "true"
  containers:
  - name: blink
    image: pico/blinker:v1  # ECI (embedded container image)
    resources:
      limits:
        pico.io/onboard-led: 1
EOF

# Check pod status
kubectl get pod blinker

# LED should be blinking!

# Verify hardware allocated
kubectl describe node pico-node-1
# Allocated resources:
#   pico.io/onboard-led: 1

# Delete pod
kubectl delete pod blinker

# LED stops, resource freed
kubectl describe node pico-node-1
# Allocated resources:
#   pico.io/onboard-led: 0
```

### Test Resource Exhaustion

```bash
# Deploy 2 blinker pods (only 1 LED available!)
kubectl apply -f blinker-pod.yaml
kubectl apply -f blinker-pod-2.yaml

# First pod: Running
# Second pod: Pending (InsufficientHardware)

kubectl get pods
# NAME        READY   STATUS    RESTARTS   AGE
# blinker     1/1     Running   0          10s
# blinker-2   0/1     Pending   0          5s

kubectl describe pod blinker-2
# Events:
#   Warning  FailedScheduling  insufficient pico.io/onboard-led
```

## Future Enhancements

### Dynamic Hardware Discovery

Detect hot-plugged devices (I2C/SPI):

```c
int hardware_manager_scan_i2c(void) {
    // Scan I2C bus for devices
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_device_present(addr)) {
            // Device found - identify and register
            identify_i2c_device(addr);
        }
    }
}
```

### Hardware Health Monitoring

Detect failed devices:

```c
void hardware_manager_health_check(void) {
    for (int i = 0; i < device_count; i++) {
        if (!device_health_check(devices[i])) {
            devices[i]->state = HW_STATE_FAILED;

            // Report to K8s (remove from allocatable)
            node_status_report();

            // Kill pods using failed hardware
            if (devices[i]->allocated) {
                pod_kill_by_container_id(
                    devices[i]->allocated_to_container,
                    "HardwareFailure"
                );
            }
        }
    }
}
```

### Shared Hardware

Allow multiple containers to share read-only hardware:

```c
// Temperature sensor can be shared (read-only)
temp_sensor.shareable = true;
temp_sensor.max_allocations = 10;

// GPIO output cannot be shared
gpio_pin_0.shareable = false;
gpio_pin_0.max_allocations = 1;
```

## Summary

The Hardware Manager provides:

✅ **Discovery** - Find available hardware at boot
✅ **Allocation** - Assign hardware to containers
✅ **Tracking** - Monitor who's using what
✅ **Reporting** - Tell Kubernetes scheduler what's available
✅ **Enforcement** - Prevent unauthorized access
✅ **Portability** - Abstract interface works across boards

This enables Kubernetes to schedule pods based on hardware requirements, just like CPU and memory!
