#include "tcp_connection.h"
#include "config.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include <string.h>

// Helper: Check if ring buffer is empty
static inline bool ring_is_empty(tcp_connection_t *conn) {
    return conn->recv_head == conn->recv_tail;
}

// Helper: Get number of bytes available in ring buffer
static inline uint16_t ring_available(tcp_connection_t *conn) {
    return (conn->recv_head - conn->recv_tail) & (TCP_RECV_RING_SIZE - 1);
}

// Helper: Get free space in ring buffer
static inline uint16_t ring_free_space(tcp_connection_t *conn) {
    return (TCP_RECV_RING_SIZE - 1) - ring_available(conn);
}

// lwIP TCP receive callback
static err_t tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    tcp_connection_t *conn = (tcp_connection_t *)arg;

    if (err != ERR_OK || p == NULL) {
        // Connection closed or error
        if (p) {
            pbuf_free(p);
        }
        return ERR_OK;
    }

    // Copy data from pbuf chain to ring buffer
    struct pbuf *q = p;
    uint16_t copied = 0;

    for (; q != NULL; q = q->next) {
        uint8_t *data = (uint8_t *)q->payload;
        uint16_t len = q->len;

        for (uint16_t i = 0; i < len && ring_free_space(conn) > 0; i++) {
            conn->recv_ring[conn->recv_head] = data[i];
            conn->recv_head = (conn->recv_head + 1) & (TCP_RECV_RING_SIZE - 1);
            copied++;
        }

        if (ring_free_space(conn) == 0) {
            DEBUG_PRINT("Ring buffer full, dropping remaining data");
            break;
        }
    }

    // Tell TCP stack we processed all the data
    tcp_recved(tpcb, p->tot_len);

    pbuf_free(p);
    return ERR_OK;
}

// lwIP TCP error callback
static void tcp_err_callback(void *arg, err_t err) {
    tcp_connection_t *conn = (tcp_connection_t *)arg;

    DEBUG_PRINT("TCP error callback: %d", err);
    conn->state = TCP_STATE_ERROR;
    conn->error_code = TCP_ERR_CONNECT;
    conn->pcb = NULL;  // lwIP already freed the PCB
}

// lwIP TCP connected callback
static err_t tcp_connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err) {
    tcp_connection_t *conn = (tcp_connection_t *)arg;

    if (err != ERR_OK) {
        DEBUG_PRINT("Connection failed: %d", err);
        conn->state = TCP_STATE_ERROR;
        conn->error_code = TCP_ERR_CONNECT;
        return err;
    }

    DEBUG_PRINT("TCP connection established");
    conn->state = TCP_STATE_CONNECTED;

    return ERR_OK;
}

// DNS resolution callback
static void tcp_dns_found_callback(const char *name, const ip_addr_t *ipaddr, void *arg) {
    tcp_connection_t *conn = (tcp_connection_t *)arg;

    if (ipaddr == NULL) {
        DEBUG_PRINT("DNS lookup failed for %s", name);
        conn->state = TCP_STATE_ERROR;
        conn->error_code = TCP_ERR_DNS;
        return;
    }

    DEBUG_PRINT("DNS resolved: %s", ip4addr_ntoa(ipaddr));
    conn->resolved_ip = *ipaddr;
    conn->state = TCP_STATE_DNS_RESOLVED;
}

int tcp_connection_init(tcp_connection_t *conn) {
    if (!conn) {
        return TCP_ERR_INVALID_PARAM;
    }

    memset(conn, 0, sizeof(tcp_connection_t));
    conn->state = TCP_STATE_IDLE;
    conn->pcb = NULL;
    conn->recv_head = 0;
    conn->recv_tail = 0;

    return TCP_OK;
}

int tcp_connection_connect(tcp_connection_t *conn, const char *hostname, uint16_t port, uint32_t timeout_ms) {
    if (!conn || !hostname) {
        return TCP_ERR_INVALID_PARAM;
    }

    err_t err;

    // Create new TCP PCB
    conn->pcb = tcp_new();
    if (!conn->pcb) {
        DEBUG_PRINT("Failed to allocate TCP PCB");
        return TCP_ERR_MEMORY;
    }

    // Set callbacks
    tcp_arg(conn->pcb, conn);
    tcp_recv(conn->pcb, tcp_recv_callback);
    tcp_err(conn->pcb, tcp_err_callback);

    // Try to parse as IP address first
    ip_addr_t server_ip;
    if (ipaddr_aton(hostname, &server_ip)) {
        // Direct IP address
        conn->resolved_ip = server_ip;
        conn->state = TCP_STATE_DNS_RESOLVED;
    } else {
        // Need DNS resolution
        conn->state = TCP_STATE_DNS_RESOLVING;
        conn->timeout = make_timeout_time_ms(timeout_ms);

        DEBUG_PRINT("Resolving DNS for %s...", hostname);
        err = dns_gethostbyname(hostname, &conn->resolved_ip, tcp_dns_found_callback, conn);

        if (err == ERR_OK) {
            // Cached, already resolved
            conn->state = TCP_STATE_DNS_RESOLVED;
        } else if (err == ERR_INPROGRESS) {
            // Wait for DNS resolution
            while (conn->state == TCP_STATE_DNS_RESOLVING) {
                cyw43_arch_poll();
                sleep_ms(10);

                if (absolute_time_diff_us(get_absolute_time(), conn->timeout) < 0) {
                    DEBUG_PRINT("DNS timeout");
                    tcp_close(conn->pcb);
                    conn->pcb = NULL;
                    conn->state = TCP_STATE_ERROR;
                    return TCP_ERR_TIMEOUT;
                }
            }

            if (conn->state == TCP_STATE_ERROR) {
                tcp_close(conn->pcb);
                conn->pcb = NULL;
                return TCP_ERR_DNS;
            }
        } else {
            DEBUG_PRINT("DNS error: %d", err);
            tcp_close(conn->pcb);
            conn->pcb = NULL;
            return TCP_ERR_DNS;
        }
    }

    // Connect to server
    DEBUG_PRINT("Connecting to %s:%d...", ip4addr_ntoa(&conn->resolved_ip), port);
    conn->state = TCP_STATE_CONNECTING;
    conn->timeout = make_timeout_time_ms(timeout_ms);

    err = tcp_connect(conn->pcb, &conn->resolved_ip, port, tcp_connected_callback);
    if (err != ERR_OK) {
        DEBUG_PRINT("tcp_connect failed: %d", err);
        tcp_close(conn->pcb);
        conn->pcb = NULL;
        return TCP_ERR_CONNECT;
    }

    // Wait for connection
    while (conn->state == TCP_STATE_CONNECTING) {
        cyw43_arch_poll();
        sleep_ms(10);

        if (absolute_time_diff_us(get_absolute_time(), conn->timeout) < 0) {
            DEBUG_PRINT("Connection timeout");
            tcp_close(conn->pcb);
            conn->pcb = NULL;
            conn->state = TCP_STATE_ERROR;
            return TCP_ERR_TIMEOUT;
        }
    }

    if (conn->state != TCP_STATE_CONNECTED) {
        DEBUG_PRINT("Connection failed");
        if (conn->pcb) {
            tcp_close(conn->pcb);
            conn->pcb = NULL;
        }
        return TCP_ERR_CONNECT;
    }

    DEBUG_PRINT("Connection successful");
    return TCP_OK;
}

int tcp_connection_send(tcp_connection_t *conn, const uint8_t *data, size_t len, uint32_t timeout_ms) {
    if (!conn || !data || conn->state != TCP_STATE_CONNECTED) {
        return TCP_ERR_INVALID_PARAM;
    }

    if (!conn->pcb) {
        return TCP_ERR_CLOSED;
    }

    err_t err;
    size_t sent = 0;
    conn->timeout = make_timeout_time_ms(timeout_ms);

    while (sent < len) {
        // Check available send buffer space
        uint16_t available = tcp_sndbuf(conn->pcb);
        if (available == 0) {
            // Wait for send buffer space
            cyw43_arch_poll();
            sleep_ms(10);

            if (absolute_time_diff_us(get_absolute_time(), conn->timeout) < 0) {
                DEBUG_PRINT("Send timeout");
                return TCP_ERR_TIMEOUT;
            }
            continue;
        }

        // Send as much as possible
        uint16_t to_send = (len - sent) < available ? (len - sent) : available;
        err = tcp_write(conn->pcb, data + sent, to_send, TCP_WRITE_FLAG_COPY);

        if (err == ERR_OK) {
            sent += to_send;
        } else if (err == ERR_MEM) {
            // Out of memory, wait and retry
            cyw43_arch_poll();
            sleep_ms(10);

            if (absolute_time_diff_us(get_absolute_time(), conn->timeout) < 0) {
                DEBUG_PRINT("Send timeout");
                return TCP_ERR_TIMEOUT;
            }
        } else {
            DEBUG_PRINT("tcp_write error: %d", err);
            return TCP_ERR_SEND;
        }
    }

    // Flush output
    tcp_output(conn->pcb);

    return sent;
}

int tcp_connection_recv(tcp_connection_t *conn, uint8_t *buffer, size_t buffer_size, uint32_t timeout_ms) {
    if (!conn || !buffer || buffer_size == 0 || conn->state != TCP_STATE_CONNECTED) {
        return TCP_ERR_INVALID_PARAM;
    }

    if (!conn->pcb) {
        return TCP_ERR_CLOSED;
    }

    conn->timeout = make_timeout_time_ms(timeout_ms);
    size_t received = 0;

    // Wait for data or timeout
    while (ring_is_empty(conn)) {
        cyw43_arch_poll();
        sleep_ms(10);

        // Check if connection closed
        if (conn->state != TCP_STATE_CONNECTED) {
            return 0;  // Connection closed
        }

        if (absolute_time_diff_us(get_absolute_time(), conn->timeout) < 0) {
            // Timeout - return what we have
            return received;
        }
    }

    // Read from ring buffer
    while (received < buffer_size && !ring_is_empty(conn)) {
        buffer[received++] = conn->recv_ring[conn->recv_tail];
        conn->recv_tail = (conn->recv_tail + 1) & (TCP_RECV_RING_SIZE - 1);
    }

    return received;
}

void tcp_connection_close(tcp_connection_t *conn) {
    if (!conn) {
        return;
    }

    if (conn->pcb) {
        tcp_arg(conn->pcb, NULL);
        tcp_recv(conn->pcb, NULL);
        tcp_err(conn->pcb, NULL);
        tcp_close(conn->pcb);
        conn->pcb = NULL;
    }

    conn->state = TCP_STATE_CLOSED;
    DEBUG_PRINT("TCP connection closed");
}

const char *tcp_error_to_string(tcp_error_t error) {
    switch (error) {
        case TCP_OK: return "OK";
        case TCP_ERR_INVALID_PARAM: return "Invalid parameter";
        case TCP_ERR_DNS: return "DNS resolution failed";
        case TCP_ERR_CONNECT: return "Connection failed";
        case TCP_ERR_SEND: return "Send failed";
        case TCP_ERR_RECV: return "Receive failed";
        case TCP_ERR_TIMEOUT: return "Timeout";
        case TCP_ERR_MEMORY: return "Out of memory";
        case TCP_ERR_CLOSED: return "Connection closed";
        default: return "Unknown error";
    }
}
