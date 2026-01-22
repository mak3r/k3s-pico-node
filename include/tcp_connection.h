#ifndef TCP_CONNECTION_H
#define TCP_CONNECTION_H

#include "lwip/tcp.h"
#include "pico/stdlib.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * TCP Connection Layer (HTTP Only - No TLS)
 *
 * Simple TCP connection for HTTP communication with nginx proxy.
 * The proxy handles TLS termination to the k3s API server.
 */

// Connection states
typedef enum {
    TCP_STATE_IDLE = 0,
    TCP_STATE_DNS_RESOLVING,
    TCP_STATE_DNS_RESOLVED,
    TCP_STATE_CONNECTING,
    TCP_STATE_CONNECTED,
    TCP_STATE_ERROR,
    TCP_STATE_CLOSED
} tcp_conn_state_t;

// Error codes
typedef enum {
    TCP_OK = 0,
    TCP_ERR_INVALID_PARAM = -1,
    TCP_ERR_DNS = -2,
    TCP_ERR_CONNECT = -3,
    TCP_ERR_SEND = -5,
    TCP_ERR_RECV = -6,
    TCP_ERR_TIMEOUT = -7,
    TCP_ERR_MEMORY = -8,
    TCP_ERR_CLOSED = -9
} tcp_error_t;

// Ring buffer size for incoming data (must be power of 2)
#define TCP_RECV_RING_SIZE 2048

// Connection context structure
typedef struct {
    // lwIP TCP control block
    struct tcp_pcb *pcb;

    // Ring buffer for incoming data
    uint8_t recv_ring[TCP_RECV_RING_SIZE];
    uint16_t recv_head;  // Write position
    uint16_t recv_tail;  // Read position

    // Connection state
    tcp_conn_state_t state;
    int error_code;

    // DNS resolution
    ip_addr_t resolved_ip;

    // Timeouts
    absolute_time_t timeout;

} tcp_connection_t;

/**
 * Initialize TCP connection context
 */
int tcp_connection_init(tcp_connection_t *conn);

/**
 * Connect to server (DNS + TCP)
 * Returns TCP_OK on success, error code on failure
 */
int tcp_connection_connect(tcp_connection_t *conn, const char *hostname, uint16_t port, uint32_t timeout_ms);

/**
 * Send data over connection
 * Returns number of bytes sent, or negative error code
 */
int tcp_connection_send(tcp_connection_t *conn, const uint8_t *data, size_t len, uint32_t timeout_ms);

/**
 * Receive data from connection
 * Returns number of bytes received, 0 for connection closed, or negative error code
 */
int tcp_connection_recv(tcp_connection_t *conn, uint8_t *buffer, size_t buffer_size, uint32_t timeout_ms);

/**
 * Close connection and cleanup resources
 */
void tcp_connection_close(tcp_connection_t *conn);

/**
 * Convert error code to human-readable string
 */
const char *tcp_error_to_string(tcp_error_t error);

#endif // TCP_CONNECTION_H
