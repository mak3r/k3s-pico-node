#include "node_status.h"
#include "k3s_client.h"
#include "config.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include <string.h>

// Node status JSON template for creating/updating node
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

void node_status_get_ip(char *ip_buffer, int buffer_size) {
    if (ip_buffer == NULL || buffer_size < 16) {
        return;
    }

    // Get IP address from CYW43 WiFi
    uint32_t ip = cyw43_state.netif[0].ip_addr.addr;

    snprintf(ip_buffer, buffer_size, "%d.%d.%d.%d",
             ip & 0xFF,
             (ip >> 8) & 0xFF,
             (ip >> 16) & 0xFF,
             (ip >> 24) & 0xFF);
}

int node_status_register(void) {
    char json_buffer[2048];
    char node_ip[16];

    node_status_get_ip(node_ip, sizeof(node_ip));

    DEBUG_PRINT("Registering node %s with IP %s", K3S_NODE_NAME, node_ip);

    // Format node JSON
    int len = snprintf(json_buffer, sizeof(json_buffer),
                      node_status_json_template,
                      K3S_NODE_NAME,      // metadata.name
                      K3S_NODE_NAME,      // metadata.labels.kubernetes.io/hostname
                      node_ip,            // status.addresses[0].address
                      K3S_NODE_NAME,      // status.addresses[1].address
                      KUBELET_PORT);      // status.daemonEndpoints.kubeletEndpoint.Port

    if (len < 0 || len >= sizeof(json_buffer)) {
        printf("ERROR: Node JSON too large or formatting error\n");
        return -1;
    }

    DEBUG_PRINT("Node JSON size: %d bytes", len);

    // POST to /api/v1/nodes
    // Note: k3s may return 409 Conflict if node already exists
    // In that case, we should do a PATCH instead, but for simplicity
    // we'll just log it and continue (the node will update on next status report)
    int result = k3s_client_post("/api/v1/nodes", json_buffer);

    if (result == 0) {
        printf("Node registered successfully: %s\n", K3S_NODE_NAME);
    } else {
        printf("Node registration failed (may already exist), will try status update\n");
        // Try to update status instead
        return node_status_report();
    }

    return result;
}

int node_status_report(void) {
    char json_buffer[2048];
    char node_ip[16];
    char url[128];

    node_status_get_ip(node_ip, sizeof(node_ip));

    DEBUG_PRINT("Reporting node status for %s", K3S_NODE_NAME);

    // Format node status JSON
    int len = snprintf(json_buffer, sizeof(json_buffer),
                      node_status_json_template,
                      K3S_NODE_NAME,
                      K3S_NODE_NAME,
                      node_ip,
                      K3S_NODE_NAME,
                      KUBELET_PORT);

    if (len < 0 || len >= sizeof(json_buffer)) {
        printf("ERROR: Node status JSON too large or formatting error\n");
        return -1;
    }

    // PATCH to /api/v1/nodes/{name}/status
    snprintf(url, sizeof(url), "/api/v1/nodes/%s/status", K3S_NODE_NAME);

    int result = k3s_client_patch(url, json_buffer);

    if (result == 0) {
        DEBUG_PRINT("Node status reported successfully");
    } else {
        printf("ERROR: Node status report failed\n");
    }

    return result;
}
