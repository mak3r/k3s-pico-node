#ifndef KUBELET_SERVER_H
#define KUBELET_SERVER_H

/**
 * Mock Kubelet HTTP Server
 *
 * Implements minimal kubelet endpoints required for k3s:
 * - GET /healthz - Health check endpoint
 * - GET /metrics - Prometheus metrics endpoint (minimal)
 *
 * Runs on port 10250 (KUBELET_PORT)
 */

/**
 * Initialize the kubelet HTTP server
 * Sets up TCP listener on KUBELET_PORT
 * Returns 0 on success, -1 on error
 */
int kubelet_server_init(void);

/**
 * Poll for incoming kubelet requests
 * Must be called regularly from main loop
 * Processes any pending HTTP requests non-blocking
 */
void kubelet_server_poll(void);

/**
 * Shutdown the kubelet server
 * Closes all connections and frees resources
 */
void kubelet_server_shutdown(void);

#endif // KUBELET_SERVER_H
