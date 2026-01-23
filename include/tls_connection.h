#ifndef TLS_CONNECTION_H
#define TLS_CONNECTION_H

#include "lwip/tcp.h"
#include "mbedtls/ssl.h"
#include "pico/stdlib.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * TLS Connection Layer
 *
 * Bridges lwIP's asynchronous raw API with mbedtls's synchronous API using
 * a quasi-blocking state machine approach. This provides simple linear control
 * flow while remaining compatible with NO_SYS polling architecture.
 */

// Connection states
typedef enum {
    TLS_STATE_IDLE = 0,
    TLS_STATE_DNS_RESOLVING,
    TLS_STATE_DNS_RESOLVED,
    TLS_STATE_CONNECTING,
    TLS_STATE_CONNECTED,
    TLS_STATE_HANDSHAKING,
    TLS_STATE_READY,
    TLS_STATE_ERROR,
    TLS_STATE_CLOSED
} tls_conn_state_t;

// Error codes
typedef enum {
    TLS_OK = 0,
    TLS_ERR_INVALID_PARAM = -1,
    TLS_ERR_DNS = -2,
    TLS_ERR_CONNECT = -3,
    TLS_ERR_HANDSHAKE = -4,
    TLS_ERR_SEND = -5,
    TLS_ERR_RECV = -6,
    TLS_ERR_TIMEOUT = -7,
    TLS_ERR_MEMORY = -8,
    TLS_ERR_CLOSED = -9,
    TLS_ERR_MBEDTLS = -10
} tls_error_t;

// Ring buffer size for incoming data (must be power of 2 for efficiency)
#define TLS_RECV_RING_SIZE 2048

// Connection context structure
typedef struct {
    // lwIP TCP connection
    struct tcp_pcb *pcb;

    // Receive ring buffer for bridging async TCP to sync mbedtls
    uint8_t recv_ring[TLS_RECV_RING_SIZE];
    volatile uint16_t recv_head;  // Write position (updated by TCP callback)
    volatile uint16_t recv_tail;  // Read position (updated by mbedtls)

    // Connection state
    tls_conn_state_t state;
    int last_error;

    // Timeouts
    absolute_time_t timeout;

    // DNS resolution
    ip_addr_t resolved_ip;

    // TLS context (provided externally)
    mbedtls_ssl_context *ssl;

    // Statistics
    uint32_t bytes_sent;
    uint32_t bytes_received;

    // Flags
    bool connection_closed;
    bool handshake_complete;
} tls_connection_t;

/**
 * Initialize a TLS connection context
 *
 * @param conn Connection context to initialize
 * @param ssl mbedtls SSL context (must be pre-configured)
 * @return TLS_OK on success, error code otherwise
 */
int tls_connection_init(tls_connection_t *conn, mbedtls_ssl_context *ssl);

/**
 * Connect to a remote host with TLS
 *
 * This function performs DNS resolution, TCP connection, and TLS handshake.
 * It uses a polling approach with bounded timeouts.
 *
 * @param conn Connection context
 * @param hostname Remote hostname (will be resolved via DNS)
 * @param port Remote port
 * @param timeout_ms Total timeout in milliseconds
 * @return TLS_OK on success, error code otherwise
 */
int tls_connection_connect(tls_connection_t *conn, const char *hostname,
                          uint16_t port, uint32_t timeout_ms);

/**
 * Send data over TLS connection
 *
 * This function wraps mbedtls_ssl_write() and handles retries.
 *
 * @param conn Connection context
 * @param data Data to send
 * @param len Length of data
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes sent on success, error code otherwise
 */
int tls_connection_send(tls_connection_t *conn, const uint8_t *data,
                       size_t len, uint32_t timeout_ms);

/**
 * Receive data from TLS connection
 *
 * This function wraps mbedtls_ssl_read() and handles retries.
 *
 * @param conn Connection context
 * @param buffer Buffer to store received data
 * @param buffer_size Size of buffer
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes received on success, error code otherwise
 */
int tls_connection_recv(tls_connection_t *conn, uint8_t *buffer,
                       size_t buffer_size, uint32_t timeout_ms);

/**
 * Close TLS connection and cleanup resources
 *
 * @param conn Connection context
 */
void tls_connection_close(tls_connection_t *conn);

/**
 * Check if data is available to read
 *
 * @param conn Connection context
 * @return Number of bytes available in ring buffer
 */
uint16_t tls_connection_available(tls_connection_t *conn);

/**
 * Get connection state
 *
 * @param conn Connection context
 * @return Current connection state
 */
tls_conn_state_t tls_connection_get_state(tls_connection_t *conn);

/**
 * Get last error code
 *
 * @param conn Connection context
 * @return Last error code
 */
int tls_connection_get_error(tls_connection_t *conn);

/**
 * Convert error code to string
 *
 * @param error Error code
 * @return Human-readable error string
 */
const char* tls_error_to_string(int error);

#endif // TLS_CONNECTION_H
