#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

#include "config.h"
#include "kubelet_server.h"
#include "k3s_client.h"
#include "node_status.h"
#include "configmap_watcher.h"
#include "memory_manager.h"
#include "time_sync.h"

// Timing tracking
static absolute_time_t last_status_report;
static absolute_time_t last_configmap_poll;
static absolute_time_t last_health_check;

// System state
static bool system_initialized = false;
static bool node_registered = false;

void print_banner(void) {
    printf("\n");
    printf("========================================\n");
    printf("  Raspberry Pi Pico WH - K3s Node\n");
    printf("========================================\n");
    printf("Node Name: %s\n", K3S_NODE_NAME);
    printf("K3s Server: %s:%d\n", K3S_SERVER_IP, K3S_SERVER_PORT);
    printf("Kubelet Port: %d\n", KUBELET_PORT);
    printf("========================================\n");
    printf("\n");
}

int init_wifi(void) {
    printf("Initializing WiFi...\n");

    if (cyw43_arch_init()) {
        printf("ERROR: Failed to initialize WiFi chip\n");
        return -1;
    }

    cyw43_arch_enable_sta_mode();
    printf("WiFi chip initialized, connecting to: %s\n", WIFI_SSID);

    // Try different authentication modes
    printf("Attempting WiFi connection...\n");
    printf("SSID: %s\n", WIFI_SSID);
    printf("Password length: %d characters\n", strlen(WIFI_PASSWORD));

    // Try WPA2 Mixed Mode (most compatible)
    printf("Trying CYW43_AUTH_WPA2_MIXED_PSK...\n");
    int result = cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID,
        WIFI_PASSWORD,
        CYW43_AUTH_WPA2_MIXED_PSK,
        30000);

    if (result != 0) {
        printf("ERROR: Failed to connect to WiFi (error %d)\n", result);
        printf("Error -7 = PICO_ERROR_BADAUTH (bad credentials or wrong security type)\n");
        printf("Please verify:\n");
        printf("  1. WiFi SSID is correct\n");
        printf("  2. WiFi password is correct\n");
        printf("  3. WiFi is 2.4GHz (5GHz not supported)\n");
        printf("  4. WiFi uses WPA2 security (not WPA3)\n");
        return -1;
    }

    // Get and display IP address
    char ip_buffer[16];
    node_status_get_ip(ip_buffer, sizeof(ip_buffer));
    printf("WiFi connected! IP address: %s\n", ip_buffer);

    // Give DHCP extra time to fully complete and get gateway info
    printf("Waiting for DHCP to complete...\n");
    sleep_ms(2000);
    cyw43_arch_poll();  // Poll once more
    printf("DHCP wait complete\n");

    // WORKAROUND: Manually set gateway if DHCP didn't set it correctly
    // This is a known issue with some pico-sdk/cyw43 versions
    struct netif *netif = netif_default;
    if (netif != NULL) {
        printf("Current gateway: %s\n", ipaddr_ntoa(&netif->gw));

        // Check if gateway is invalid (all zeros or netmask value)
        if (ip4_addr_isany_val(*netif_ip4_gw(netif)) ||
            netif->gw.addr == netif->netmask.addr) {

            printf("Gateway not set correctly by DHCP, setting manually...\n");

            // Manually set gateway to .1 of our subnet (standard convention)
            ip4_addr_t gw;
            IP4_ADDR(&gw, 192, 168, 86, 1);
            netif_set_gw(netif, &gw);

            printf("Gateway manually set to: %s\n", ipaddr_ntoa(&netif->gw));
        }
    }

    return 0;
}

int init_subsystems(void) {
    printf("\nInitializing subsystems...\n");

    // Initialize memory manager
    printf("  [1/6] Memory manager...\n");
    memory_manager_init();

    // Initialize time synchronization
    printf("  [2/6] Time sync...\n");
    time_sync_init();

    // Initialize k3s client
    printf("  [3/6] K3s API client...\n");
    if (k3s_client_init() != 0) {
        printf("ERROR: Failed to initialize k3s client\n");
        return -1;
    }

    // Initialize kubelet server
    printf("  [4/6] Kubelet server...\n");
    if (kubelet_server_init() != 0) {
        printf("ERROR: Failed to initialize kubelet server\n");
        return -1;
    }

    // Initialize ConfigMap watcher
    printf("  [5/6] ConfigMap watcher...\n");
    if (configmap_watcher_init() != 0) {
        printf("ERROR: Failed to initialize ConfigMap watcher\n");
        return -1;
    }

    // Register node with k3s cluster
    printf("  [6/6] Registering node with k3s...\n");
    if (node_status_register() != 0) {
        printf("WARNING: Node registration failed, will retry in status reports\n");
        node_registered = false;
    } else {
        node_registered = true;
    }

    printf("Subsystems initialized!\n\n");
    return 0;
}

void perform_health_check(void) {
    // Simple health check - verify WiFi is still connected
    uint32_t status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);

    if (status != CYW43_LINK_UP) {
        printf("WARNING: WiFi link down (status: %u)\n", status);
        // Could attempt reconnection here
    }

    DEBUG_PRINT("Health check: OK (link status: %u)", status);
}

int main() {
    // Initialize USB serial for debug output
    stdio_init_all();

    // Give USB time to enumerate
    sleep_ms(2000);

    print_banner();

    // Initialize WiFi
    if (init_wifi() != 0) {
        printf("FATAL: WiFi initialization failed\n");
        return 1;
    }

    // Initialize all subsystems
    if (init_subsystems() != 0) {
        printf("FATAL: Subsystem initialization failed\n");
        cyw43_arch_deinit();
        return 1;
    }

    system_initialized = true;
    printf("System ready! Entering main loop...\n\n");

    // Initialize timing
    last_status_report = get_absolute_time();
    last_configmap_poll = get_absolute_time();
    last_health_check = get_absolute_time();

    // Main loop
    while (1) {
        // CRITICAL: Poll WiFi/lwIP stack
        // This MUST be called regularly for network operation
        cyw43_arch_poll();

        // Process kubelet server requests (non-blocking)
        kubelet_server_poll();

        // Get current time
        absolute_time_t now = get_absolute_time();

        // Periodic: Send node status reports
        if (absolute_time_diff_us(last_status_report, now) >
            (NODE_STATUS_INTERVAL_MS * 1000LL)) {

            DEBUG_PRINT("--- Status report interval ---");
            if (node_status_report() == 0) {
                node_registered = true;
            }
            last_status_report = now;
        }

        // Periodic: Poll ConfigMaps
        if (absolute_time_diff_us(last_configmap_poll, now) >
            (CONFIGMAP_POLL_INTERVAL_MS * 1000LL)) {

            DEBUG_PRINT("--- ConfigMap poll interval ---");
            configmap_watcher_poll();
            last_configmap_poll = now;
        }

        // Periodic: Health check
        if (absolute_time_diff_us(last_health_check, now) >
            (HEALTH_CHECK_INTERVAL_MS * 1000LL)) {

            perform_health_check();
            last_health_check = now;
        }

        // Small sleep to prevent busy-waiting and allow other processes
        sleep_ms(10);
    }

    // Cleanup (never reached in normal operation)
    kubelet_server_shutdown();
    k3s_client_shutdown();
    cyw43_arch_deinit();

    return 0;
}
