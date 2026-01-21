#include "configmap_watcher.h"
#include "k3s_client.h"
#include "memory_manager.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

// Simple JSON value extractor (minimal parser)
// Finds "key": "value" patterns in JSON
static const char *find_json_string_value(const char *json, const char *key) {
    static char value_buffer[512];
    char search_pattern[128];

    // Build search pattern: "key":"
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":\"", key);

    const char *start = strstr(json, search_pattern);
    if (start == NULL) {
        // Try without space: "key": "
        snprintf(search_pattern, sizeof(search_pattern), "\"%s\": \"", key);
        start = strstr(json, search_pattern);
        if (start == NULL) {
            return NULL;
        }
    }

    // Move past the pattern to the value
    start = strchr(start + strlen(search_pattern) - 1, '"');
    if (start == NULL) {
        return NULL;
    }
    start++; // Skip opening quote

    // Find closing quote
    const char *end = strchr(start, '"');
    if (end == NULL) {
        return NULL;
    }

    // Copy value to buffer
    int len = end - start;
    if (len >= sizeof(value_buffer)) {
        len = sizeof(value_buffer) - 1;
    }

    strncpy(value_buffer, start, len);
    value_buffer[len] = '\0';

    return value_buffer;
}

int configmap_watcher_init(void) {
    DEBUG_PRINT("ConfigMap watcher initialized");
    DEBUG_PRINT("  Watching: %s/%s", CONFIGMAP_NAMESPACE, CONFIGMAP_NAME);
    return 0;
}

int configmap_watcher_poll(void) {
    char url[256];
    char response[JSON_PARSE_BUFFER_SIZE];

    DEBUG_PRINT("Polling ConfigMap %s/%s", CONFIGMAP_NAMESPACE, CONFIGMAP_NAME);

    // Build URL: /api/v1/namespaces/{namespace}/configmaps/{name}
    snprintf(url, sizeof(url),
            "/api/v1/namespaces/%s/configmaps/%s",
            CONFIGMAP_NAMESPACE, CONFIGMAP_NAME);

    // Fetch ConfigMap from API server
    int result = k3s_client_get(url, response, sizeof(response));

    if (result != 0) {
        // ConfigMap might not exist yet, or network error
        DEBUG_PRINT("Failed to fetch ConfigMap (may not exist yet)");
        return -1;
    }

    DEBUG_PRINT("ConfigMap fetched, parsing...");

    // Parse JSON to extract data.memory_values
    // The response looks like:
    // {
    //   "kind": "ConfigMap",
    //   "metadata": {...},
    //   "data": {
    //     "memory_values": "0=0x42,1=0x43,..."
    //   }
    // }

    // Simple approach: find "memory_values": "..."
    const char *memory_values = find_json_string_value(response, "memory_values");

    if (memory_values != NULL && strlen(memory_values) > 0) {
        printf("ConfigMap update detected: %s\n", memory_values);
        memory_manager_update_from_string(memory_values);
        return 0;
    } else {
        DEBUG_PRINT("No memory_values field found in ConfigMap");
        return -1;
    }
}

int configmap_watcher_check_now(void) {
    return configmap_watcher_poll();
}
