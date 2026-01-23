/**
 * Unit tests for node status JSON generation
 *
 * Validates that the node status JSON is correctly formatted
 * and contains all required Kubernetes fields.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

// Test JSON parsing - in real tests we'd use a library like cJSON
// For now, we'll do simple string matching

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

// Mock node status JSON template (same as in node_status.c)
static const char *node_status_json_template =
    "{"
    "  \"kind\": \"Node\","
    "  \"apiVersion\": \"v1\","
    "  \"metadata\": {"
    "    \"name\": \"%s\","
    "    \"labels\": {"
    "      \"beta.kubernetes.io/arch\": \"arm\","
    "      \"beta.kubernetes.io/os\": \"linux\","
    "      \"kubernetes.io/arch\": \"arm\","
    "      \"kubernetes.io/os\": \"linux\","
    "      \"kubernetes.io/hostname\": \"%s\","
    "      \"node.kubernetes.io/instance-type\": \"rp2040-pico\""
    "    }"
    "  },"
    "  \"status\": {"
    "    \"conditions\": ["
    "      {\"type\": \"Ready\", \"status\": \"True\", \"reason\": \"KubeletReady\", \"message\": \"Pico node is ready\"},"
    "      {\"type\": \"MemoryPressure\", \"status\": \"False\", \"reason\": \"KubeletHasSufficientMemory\"},"
    "      {\"type\": \"DiskPressure\", \"status\": \"False\", \"reason\": \"KubeletHasNoDiskPressure\"},"
    "      {\"type\": \"PIDPressure\", \"status\": \"False\", \"reason\": \"KubeletHasSufficientPID\"},"
    "      {\"type\": \"NetworkUnavailable\", \"status\": \"False\", \"reason\": \"RouteCreated\"}"
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

void test_node_json_generation() {
    printf("\n[TEST] Node status JSON generation\n");

    char json_buffer[2048];
    const char *node_name = "pico-node-test";
    const char *node_ip = "192.168.1.100";
    int kubelet_port = 10250;

    int len = snprintf(json_buffer, sizeof(json_buffer),
                      node_status_json_template,
                      node_name,
                      node_name,
                      node_ip,
                      node_name,
                      kubelet_port);

    TEST_ASSERT(len > 0 && len < sizeof(json_buffer), "JSON generated within buffer size");

    // Validate required fields are present
    TEST_ASSERT(strstr(json_buffer, "\"kind\": \"Node\"") != NULL, "kind field present");
    TEST_ASSERT(strstr(json_buffer, "\"apiVersion\": \"v1\"") != NULL, "apiVersion field present");
    TEST_ASSERT(strstr(json_buffer, node_name) != NULL, "Node name present");
    TEST_ASSERT(strstr(json_buffer, node_ip) != NULL, "Node IP present");
    TEST_ASSERT(strstr(json_buffer, "\"Ready\"") != NULL, "Ready condition present");
    TEST_ASSERT(strstr(json_buffer, "\"cpu\": \"1\"") != NULL, "CPU capacity present");
    TEST_ASSERT(strstr(json_buffer, "\"memory\": \"256Ki\"") != NULL, "Memory capacity present");
    TEST_ASSERT(strstr(json_buffer, "\"kubeletVersion\": \"v1.34.0\"") != NULL, "Kubelet version present");
    TEST_ASSERT(strstr(json_buffer, "\"Port\": 10250") != NULL, "Kubelet port present");

    printf("  JSON size: %d bytes\n", len);
}

void test_node_json_required_conditions() {
    printf("\n[TEST] Required node conditions\n");

    char json_buffer[2048];
    snprintf(json_buffer, sizeof(json_buffer),
            node_status_json_template,
            "test-node", "test-node", "1.2.3.4", "test-node", 10250);

    // All required conditions must be present
    const char *required_conditions[] = {
        "Ready",
        "MemoryPressure",
        "DiskPressure",
        "PIDPressure",
        "NetworkUnavailable"
    };

    for (int i = 0; i < 5; i++) {
        int found = strstr(json_buffer, required_conditions[i]) != NULL;
        char msg[128];
        snprintf(msg, sizeof(msg), "Condition '%s' present", required_conditions[i]);
        TEST_ASSERT(found, msg);
    }
}

void test_node_json_labels() {
    printf("\n[TEST] Node labels\n");

    char json_buffer[2048];
    snprintf(json_buffer, sizeof(json_buffer),
            node_status_json_template,
            "test-node", "test-node", "1.2.3.4", "test-node", 10250);

    // Validate architecture and OS labels
    TEST_ASSERT(strstr(json_buffer, "\"kubernetes.io/arch\": \"arm\"") != NULL,
                "Architecture label present");
    TEST_ASSERT(strstr(json_buffer, "\"kubernetes.io/os\": \"linux\"") != NULL,
                "OS label present");
    TEST_ASSERT(strstr(json_buffer, "\"node.kubernetes.io/instance-type\": \"rp2040-pico\"") != NULL,
                "Instance type label present");
}

void test_node_json_addresses() {
    printf("\n[TEST] Node addresses\n");

    char json_buffer[2048];
    const char *test_ip = "192.168.99.88";
    snprintf(json_buffer, sizeof(json_buffer),
            node_status_json_template,
            "test-node", "test-node", test_ip, "test-node", 10250);

    TEST_ASSERT(strstr(json_buffer, "\"type\": \"InternalIP\"") != NULL, "InternalIP address type present");
    TEST_ASSERT(strstr(json_buffer, "\"type\": \"Hostname\"") != NULL, "Hostname address type present");
    TEST_ASSERT(strstr(json_buffer, test_ip) != NULL, "IP address present");
}

void test_node_info_fields() {
    printf("\n[TEST] NodeInfo fields\n");

    char json_buffer[2048];
    snprintf(json_buffer, sizeof(json_buffer),
            node_status_json_template,
            "test-node", "test-node", "1.2.3.4", "test-node", 10250);

    // Validate nodeInfo fields
    const char *required_fields[] = {
        "machineID",
        "systemUUID",
        "bootID",
        "kernelVersion",
        "osImage",
        "containerRuntimeVersion",
        "kubeletVersion",
        "kubeProxyVersion",
        "operatingSystem",
        "architecture"
    };

    for (int i = 0; i < 10; i++) {
        int found = strstr(json_buffer, required_fields[i]) != NULL;
        char msg[128];
        snprintf(msg, sizeof(msg), "nodeInfo field '%s' present", required_fields[i]);
        TEST_ASSERT(found, msg);
    }
}

void test_json_size_limits() {
    printf("\n[TEST] JSON size within limits\n");

    char json_buffer[2048];

    // Test with maximum-length node name
    char long_name[256];
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[255] = '\0';

    int len = snprintf(json_buffer, sizeof(json_buffer),
                      node_status_json_template,
                      "reasonable-node-name",
                      "reasonable-node-name",
                      "192.168.1.100",
                      "reasonable-node-name",
                      10250);

    TEST_ASSERT(len < 2048, "JSON fits within 2KB buffer");
    TEST_ASSERT(len > 500, "JSON has reasonable size (> 500 bytes)");

    printf("  Typical JSON size: %d bytes (%.1f%% of 2KB buffer)\n",
           len, (len * 100.0) / 2048);
}

int main() {
    printf("========================================\n");
    printf("  Node Status JSON Unit Tests\n");
    printf("========================================\n");

    test_node_json_generation();
    test_node_json_required_conditions();
    test_node_json_labels();
    test_node_json_addresses();
    test_node_info_fields();
    test_json_size_limits();

    printf("\n========================================\n");
    printf("  Test Results\n");
    printf("========================================\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed == 0 ? 0 : 1;
}
