#ifndef NODE_STATUS_H
#define NODE_STATUS_H

/**
 * Node Registration and Status Reporting
 *
 * Handles registering the Pico as a Kubernetes node
 * and periodically reporting node status
 */

/**
 * Register the node with k3s cluster
 * Creates a Node object in the API server
 * Returns 0 on success, -1 on error
 */
int node_status_register(void);

/**
 * Report node status to k3s API server
 * Updates node conditions, capacity, and addresses
 * Should be called every NODE_STATUS_INTERVAL_MS
 * Returns 0 on success, -1 on error
 */
int node_status_report(void);

/**
 * Get the current node's IP address
 * @param ip_buffer Buffer to store IP address string
 * @param buffer_size Size of buffer
 */
void node_status_get_ip(char *ip_buffer, int buffer_size);

#endif // NODE_STATUS_H
