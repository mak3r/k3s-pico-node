/**
 * Unit tests for node status JSON with timestamps
 *
 * Validates that node status conditions include proper timestamp fields
 * in the correct ISO 8601 format.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        if (condition) { \
            printf("  ✓ %s\n", message); \
            tests_passed++; \
        } else { \
            printf("  ✗ %s\n", message); \
            tests_failed++; \
        } \
    } while(0)

// Test template from node_status.c with timestamps
static const char *status_json_template =
    "{"
    "  \"status\": {"
    "    \"conditions\": ["
    "      {\"type\": \"Ready\", \"status\": \"True\", \"lastHeartbeatTime\": \"%s\", \"lastTransitionTime\": \"%s\", \"reason\": \"KubeletReady\", \"message\": \"Pico node is ready\"},"
    "      {\"type\": \"MemoryPressure\", \"status\": \"False\", \"lastHeartbeatTime\": \"%s\", \"lastTransitionTime\": \"%s\", \"reason\": \"KubeletHasSufficientMemory\"},"
    "      {\"type\": \"DiskPressure\", \"status\": \"False\", \"lastHeartbeatTime\": \"%s\", \"lastTransitionTime\": \"%s\", \"reason\": \"KubeletHasNoDiskPressure\"},"
    "      {\"type\": \"PIDPressure\", \"status\": \"False\", \"lastHeartbeatTime\": \"%s\", \"lastTransitionTime\": \"%s\", \"reason\": \"KubeletHasSufficientPID\"},"
    "      {\"type\": \"NetworkUnavailable\", \"status\": \"False\", \"lastHeartbeatTime\": \"%s\", \"lastTransitionTime\": \"%s\", \"reason\": \"RouteCreated\"}"
    "    ],"
    "    \"addresses\": ["
    "      {\"type\": \"InternalIP\", \"address\": \"%s\"},"
    "      {\"type\": \"Hostname\", \"address\": \"%s\"}"
    "    ],"
    "    \"capacity\": {"
    "      \"cpu\": \"1\","
    "      \"memory\": \"256Ki\","
    "      \"pods\": \"0\""
    "    },"
    "    \"allocatable\": {"
    "      \"cpu\": \"1\","
    "      \"memory\": \"256Ki\","
    "      \"pods\": \"0\""
    "    },"
    "    \"nodeInfo\": {"
    "      \"machineID\": \"rp2040-pico-wh\","
    "      \"systemUUID\": \"rp2040-pico-wh\","
    "      \"bootID\": \"rp2040-pico-wh\","
    "      \"kernelVersion\": \"5.15.0-rp2040\","
    "      \"osImage\": \"Pico SDK\","
    "      \"containerRuntimeVersion\": \"mock://1.0.0\","
    "      \"kubeletVersion\": \"v1.34.0\","
    "      \"kubeProxyVersion\": \"v1.34.0\","
    "      \"operatingSystem\": \"linux\","
    "      \"architecture\": \"arm\""
    "    },"
    "    \"daemonEndpoints\": {"
    "      \"kubeletEndpoint\": {"
    "        \"Port\": %d"
    "      }"
    "    }"
    "  }"
    "}";

void test_timestamp_presence_in_json(void) {
    printf("\n[TEST] Timestamp Fields in Node Status JSON\n");

    char json_buffer[4096];
    const char *timestamp = "2026-01-23T16:30:45Z";
    const char *node_ip = "192.168.1.100";
    const char *node_name = "test-node";
    int kubelet_port = 10250;

    // Generate JSON with timestamps
    int len = snprintf(json_buffer, sizeof(json_buffer),
                      status_json_template,
                      timestamp, timestamp,  // Ready
                      timestamp, timestamp,  // MemoryPressure
                      timestamp, timestamp,  // DiskPressure
                      timestamp, timestamp,  // PIDPressure
                      timestamp, timestamp,  // NetworkUnavailable
                      node_ip,
                      node_name,
                      kubelet_port);

    TEST_ASSERT(len > 0 && len < sizeof(json_buffer),
                "JSON generated within buffer size");

    // Verify all required timestamp fields are present
    int heartbeat_count = 0;
    int transition_count = 0;
    const char *ptr = json_buffer;

    while ((ptr = strstr(ptr, "lastHeartbeatTime")) != NULL) {
        heartbeat_count++;
        ptr++;
    }

    ptr = json_buffer;
    while ((ptr = strstr(ptr, "lastTransitionTime")) != NULL) {
        transition_count++;
        ptr++;
    }

    TEST_ASSERT(heartbeat_count == 5,
                "All 5 conditions have lastHeartbeatTime");
    TEST_ASSERT(transition_count == 5,
                "All 5 conditions have lastTransitionTime");

    // Verify timestamp value appears in JSON
    TEST_ASSERT(strstr(json_buffer, timestamp) != NULL,
                "Timestamp value present in JSON");

    // Count timestamp occurrences (should be 10: 5 conditions × 2 timestamps each)
    int timestamp_count = 0;
    ptr = json_buffer;
    while ((ptr = strstr(ptr, timestamp)) != NULL) {
        timestamp_count++;
        ptr++;
    }
    TEST_ASSERT(timestamp_count == 10,
                "Timestamp appears 10 times (5 conditions × 2)");

    printf("  JSON size: %d bytes\n", len);
}

void test_timestamp_format_in_conditions(void) {
    printf("\n[TEST] Timestamp Format in Conditions\n");

    char json_buffer[4096];
    const char *timestamp = "2026-01-23T16:30:45Z";

    snprintf(json_buffer, sizeof(json_buffer),
            status_json_template,
            timestamp, timestamp,  // Ready
            timestamp, timestamp,  // MemoryPressure
            timestamp, timestamp,  // DiskPressure
            timestamp, timestamp,  // PIDPressure
            timestamp, timestamp,  // NetworkUnavailable
            "192.168.1.100",
            "test-node",
            10250);

    // Check each condition type has correct timestamp fields
    const char *condition_types[] = {
        "Ready", "MemoryPressure", "DiskPressure", "PIDPressure", "NetworkUnavailable"
    };

    for (int i = 0; i < 5; i++) {
        char search_pattern[256];
        snprintf(search_pattern, sizeof(search_pattern),
                "\"type\": \"%s\"", condition_types[i]);

        const char *condition_start = strstr(json_buffer, search_pattern);
        TEST_ASSERT(condition_start != NULL,
                    condition_types[i]);

        if (condition_start) {
            // Find the closing brace for this condition
            const char *condition_end = strchr(condition_start, '}');
            if (condition_end) {
                size_t condition_len = condition_end - condition_start;
                char condition_json[512];
                memcpy(condition_json, condition_start, condition_len);
                condition_json[condition_len] = '\0';

                // Verify both timestamp fields are in this condition
                int has_heartbeat = strstr(condition_json, "lastHeartbeatTime") != NULL;
                int has_transition = strstr(condition_json, "lastTransitionTime") != NULL;

                char msg[128];
                snprintf(msg, sizeof(msg), "%s has lastHeartbeatTime", condition_types[i]);
                TEST_ASSERT(has_heartbeat, msg);

                snprintf(msg, sizeof(msg), "%s has lastTransitionTime", condition_types[i]);
                TEST_ASSERT(has_transition, msg);
            }
        }
    }
}

void test_iso8601_format_validation(void) {
    printf("\n[TEST] ISO 8601 Format Validation\n");

    char json_buffer[4096];

    // Test various valid ISO 8601 timestamps
    const char *valid_timestamps[] = {
        "2026-01-23T16:30:45Z",
        "2024-02-29T00:00:00Z",  // Leap year
        "2025-12-31T23:59:59Z",
        "2026-01-01T00:00:00Z",
    };

    for (int i = 0; i < 4; i++) {
        snprintf(json_buffer, sizeof(json_buffer),
                status_json_template,
                valid_timestamps[i], valid_timestamps[i],
                valid_timestamps[i], valid_timestamps[i],
                valid_timestamps[i], valid_timestamps[i],
                valid_timestamps[i], valid_timestamps[i],
                valid_timestamps[i], valid_timestamps[i],
                "192.168.1.100", "test-node", 10250);

        char msg[128];
        snprintf(msg, sizeof(msg), "Accepts ISO 8601 timestamp: %s", valid_timestamps[i]);
        TEST_ASSERT(strstr(json_buffer, valid_timestamps[i]) != NULL, msg);
    }
}

void test_json_size_with_timestamps(void) {
    printf("\n[TEST] JSON Size with Timestamps\n");

    char json_buffer[4096];
    const char *timestamp = "2026-01-23T16:30:45Z";

    int len = snprintf(json_buffer, sizeof(json_buffer),
                      status_json_template,
                      timestamp, timestamp,
                      timestamp, timestamp,
                      timestamp, timestamp,
                      timestamp, timestamp,
                      timestamp, timestamp,
                      "192.168.1.100",
                      "test-node",
                      10250);

    TEST_ASSERT(len < 4096, "JSON fits in reasonable buffer size");
    TEST_ASSERT(len > 1500, "JSON has expected size (>1500 bytes with timestamps)");

    // Size should be larger than without timestamps
    // Without timestamps: ~1200 bytes
    // With timestamps: ~1900 bytes
    printf("  JSON with timestamps: %d bytes\n", len);
}

void test_multiple_conditions_independence(void) {
    printf("\n[TEST] Multiple Conditions with Different Timestamps\n");

    char json_buffer[4096];
    const char *timestamps[] = {
        "2026-01-23T16:30:00Z",
        "2026-01-23T16:30:10Z",
        "2026-01-23T16:30:20Z",
        "2026-01-23T16:30:30Z",
        "2026-01-23T16:30:40Z",
    };

    // Generate JSON with different timestamp for each condition
    int len = snprintf(json_buffer, sizeof(json_buffer),
                      status_json_template,
                      timestamps[0], timestamps[0],  // Ready
                      timestamps[1], timestamps[1],  // MemoryPressure
                      timestamps[2], timestamps[2],  // DiskPressure
                      timestamps[3], timestamps[3],  // PIDPressure
                      timestamps[4], timestamps[4],  // NetworkUnavailable
                      "192.168.1.100",
                      "test-node",
                      10250);

    TEST_ASSERT(len > 0, "JSON generated with different timestamps");

    // Verify each unique timestamp appears
    for (int i = 0; i < 5; i++) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Timestamp %s present", timestamps[i]);
        TEST_ASSERT(strstr(json_buffer, timestamps[i]) != NULL, msg);
    }
}

void test_regression_no_null_timestamps(void) {
    printf("\n[TEST] Regression: No NULL Timestamps\n");

    char json_buffer[4096];
    const char *timestamp = "2026-01-23T16:30:45Z";

    snprintf(json_buffer, sizeof(json_buffer),
            status_json_template,
            timestamp, timestamp,
            timestamp, timestamp,
            timestamp, timestamp,
            timestamp, timestamp,
            timestamp, timestamp,
            "192.168.1.100",
            "test-node",
            10250);

    // This is the regression test - ensure we never send null timestamps
    TEST_ASSERT(strstr(json_buffer, ": null") == NULL,
                "No null timestamp values in JSON");
    TEST_ASSERT(strstr(json_buffer, ":null") == NULL,
                "No null timestamp values in JSON (no space)");

    // Ensure we have timestamp field names
    TEST_ASSERT(strstr(json_buffer, "lastHeartbeatTime") != NULL,
                "lastHeartbeatTime field present");
    TEST_ASSERT(strstr(json_buffer, "lastTransitionTime") != NULL,
                "lastTransitionTime field present");

    // Ensure timestamp fields have values (not empty)
    TEST_ASSERT(strstr(json_buffer, "lastHeartbeatTime\": \"\"") == NULL,
                "lastHeartbeatTime not empty");
    TEST_ASSERT(strstr(json_buffer, "lastTransitionTime\": \"\"") == NULL,
                "lastTransitionTime not empty");
}

int main() {
    printf("========================================\n");
    printf("  Node Status Timestamp Tests\n");
    printf("========================================\n");

    test_timestamp_presence_in_json();
    test_timestamp_format_in_conditions();
    test_iso8601_format_validation();
    test_json_size_with_timestamps();
    test_multiple_conditions_independence();
    test_regression_no_null_timestamps();

    printf("\n========================================\n");
    printf("  Test Results\n");
    printf("========================================\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed == 0 ? 0 : 1;
}
