#ifndef CONFIG_H
#define CONFIG_H

// Include local configuration with WiFi credentials and K3s server IP
// This file is gitignored and contains sensitive credentials
// Copy config_local.h.template to config_local.h and edit with your values
#include "config_local.h"

// K3s Configuration
#define K3S_SERVER_PORT          6443
#define K3S_NODE_NAME            "pico-node-1"

// Node configuration
#define KUBELET_PORT             10250

// Timing configuration (in milliseconds)
#define NODE_STATUS_INTERVAL_MS  10000      // Report status every 10s
#define CONFIGMAP_POLL_INTERVAL_MS 30000    // Poll ConfigMaps every 30s
#define HEALTH_CHECK_INTERVAL_MS 5000       // Internal health check

// Memory regions for ConfigMap updates
// Using a safe region in SRAM - adjust as needed
#define MEMORY_REGION_START      0x20040000  // Start of configurable region
#define MEMORY_REGION_SIZE       1024        // 1KB configurable region

// ConfigMap to watch
#define CONFIGMAP_NAMESPACE      "default"
#define CONFIGMAP_NAME           "pico-config"

// Buffer sizes
#define HTTP_REQUEST_BUFFER_SIZE 2048
#define HTTP_RESPONSE_BUFFER_SIZE 4096
#define JSON_PARSE_BUFFER_SIZE   4096

// Debug configuration
#define DEBUG_ENABLE             1           // Enable debug output via USB serial

#if DEBUG_ENABLE
#define DEBUG_PRINT(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...)
#endif

#endif // CONFIG_H
