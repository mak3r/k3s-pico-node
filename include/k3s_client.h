#ifndef K3S_CLIENT_H
#define K3S_CLIENT_H

/**
 * K3s API Client
 *
 * Handles TLS connections to the k3s API server
 * Provides functions to interact with Kubernetes API
 */

/**
 * Initialize the k3s client
 * Loads certificates and sets up TLS configuration
 * Returns 0 on success, -1 on error
 */
int k3s_client_init(void);

/**
 * Send a GET request to k3s API server
 * @param path API path (e.g., "/api/v1/nodes")
 * @param response Buffer to store response
 * @param response_size Size of response buffer
 * @return 0 on success, -1 on error
 */
int k3s_client_get(const char *path, char *response, int response_size);

/**
 * Send a POST request to k3s API server
 * @param path API path
 * @param body JSON body to send
 * @return 0 on success, -1 on error
 */
int k3s_client_post(const char *path, const char *body);

/**
 * Send a PATCH request to k3s API server
 * @param path API path
 * @param body JSON body to send
 * @return 0 on success, -1 on error
 */
int k3s_client_patch(const char *path, const char *body);

/**
 * Cleanup k3s client resources
 */
void k3s_client_shutdown(void);

#endif // K3S_CLIENT_H
