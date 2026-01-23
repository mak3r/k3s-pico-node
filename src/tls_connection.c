#include "tls_connection.h"
#include "config.h"
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "lwip/err.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "mbedtls/error.h"
#include "mbedtls/x509.h"
#include <string.h>
#include <stdio.h>

// Forward declarations
static err_t tcp_connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err);
static err_t tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t tcp_sent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len);
static void tcp_err_callback(void *arg, err_t err);
static err_t tcp_poll_callback(void *arg, struct tcp_pcb *tpcb);
static void dns_callback(const char *name, const ip_addr_t *ipaddr, void *arg);
static int bio_send(void *ctx, const unsigned char *buf, size_t len);
static int bio_recv(void *ctx, unsigned char *buf, size_t len);

// Error string conversion
const char* tls_error_to_string(int error) {
    switch (error) {
        case TLS_OK: return "Success";
        case TLS_ERR_INVALID_PARAM: return "Invalid parameter";
        case TLS_ERR_DNS: return "DNS resolution failed";
        case TLS_ERR_CONNECT: return "Connection failed";
        case TLS_ERR_HANDSHAKE: return "TLS handshake failed";
        case TLS_ERR_SEND: return "Send failed";
        case TLS_ERR_RECV: return "Receive failed";
        case TLS_ERR_TIMEOUT: return "Timeout";
        case TLS_ERR_MEMORY: return "Out of memory";
        case TLS_ERR_CLOSED: return "Connection closed";
        case TLS_ERR_MBEDTLS: return "mbedtls error";
        default: return "Unknown error";
    }
}

// Initialize connection context
int tls_connection_init(tls_connection_t *conn, mbedtls_ssl_context *ssl) {
    if (conn == NULL || ssl == NULL) {
        return TLS_ERR_INVALID_PARAM;
    }

    // Completely zero out the structure to clear any stale state
    memset(conn, 0, sizeof(tls_connection_t));

    conn->ssl = ssl;
    conn->state = TLS_STATE_IDLE;
    conn->last_error = TLS_OK;
    conn->pcb = NULL;
    conn->connection_closed = false;
    conn->handshake_complete = false;
    conn->recv_head = 0;
    conn->recv_tail = 0;
    conn->bytes_sent = 0;
    conn->bytes_received = 0;

    DEBUG_PRINT("TLS connection context fully reset");

    return TLS_OK;
}

// Get ring buffer available bytes
uint16_t tls_connection_available(tls_connection_t *conn) {
    if (conn == NULL) {
        return 0;
    }
    uint16_t head = conn->recv_head;
    uint16_t tail = conn->recv_tail;
    return (head - tail) & (TLS_RECV_RING_SIZE - 1);
}

// TCP connected callback
static err_t tcp_connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err) {
    tls_connection_t *conn = (tls_connection_t *)arg;

    if (err != ERR_OK) {
        DEBUG_PRINT("TCP connection failed: %d", err);
        conn->state = TLS_STATE_ERROR;
        conn->last_error = TLS_ERR_CONNECT;
        return err;
    }

    DEBUG_PRINT("TCP connected");
    conn->state = TLS_STATE_CONNECTED;
    return ERR_OK;
}

// TCP receive callback - stores data in ring buffer
static err_t tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    tls_connection_t *conn = (tls_connection_t *)arg;

    if (err != ERR_OK || p == NULL) {
        // Connection closed or error
        if (p != NULL) {
            pbuf_free(p);
        }
        conn->connection_closed = true;
        DEBUG_PRINT("TCP connection closed by peer");
        return ERR_OK;
    }

    DEBUG_PRINT("TCP recv: %d bytes", p->tot_len);

    // Copy data from pbuf chain to ring buffer
    struct pbuf *q;
    uint16_t head = conn->recv_head;
    uint16_t copied = 0;

    for (q = p; q != NULL; q = q->next) {
        uint8_t *data = (uint8_t *)q->payload;
        for (uint16_t i = 0; i < q->len; i++) {
            uint16_t next_head = (head + 1) & (TLS_RECV_RING_SIZE - 1);
            if (next_head == conn->recv_tail) {
                // Ring buffer full - drop remaining data
                DEBUG_PRINT("WARNING: Ring buffer full, dropping data");
                break;
            }
            conn->recv_ring[head] = data[i];
            head = next_head;
            copied++;
        }
    }

    conn->recv_head = head;
    conn->bytes_received += copied;

    // Tell TCP that we received the data at TCP layer (must ACK full amount!)
    // Even if we didn't copy everything to ring buffer, TCP needs the ACK
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    return ERR_OK;
}

// TCP sent callback
static err_t tcp_sent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    tls_connection_t *conn = (tls_connection_t *)arg;
    conn->bytes_sent += len;
    return ERR_OK;
}

// TCP error callback
static void tcp_err_callback(void *arg, err_t err) {
    tls_connection_t *conn = (tls_connection_t *)arg;
    DEBUG_PRINT("TCP error callback: err=%d, state was=%d", err, conn->state);

    // Decode common error codes
    const char *err_str = "UNKNOWN";
    switch (err) {
        case ERR_ABRT: err_str = "ERR_ABRT (Connection aborted)"; break;
        case ERR_RST: err_str = "ERR_RST (Connection reset)"; break;
        case ERR_CLSD: err_str = "ERR_CLSD (Connection closed)"; break;
        case ERR_CONN: err_str = "ERR_CONN (Not connected)"; break;
        case ERR_VAL: err_str = "ERR_VAL (Illegal value)"; break;
        case ERR_ARG: err_str = "ERR_ARG (Illegal argument)"; break;
        case ERR_USE: err_str = "ERR_USE (Address in use)"; break;
        case ERR_ALREADY: err_str = "ERR_ALREADY (Already connecting)"; break;
        case ERR_ISCONN: err_str = "ERR_ISCONN (Already connected)"; break;
        case ERR_MEM: err_str = "ERR_MEM (Out of memory)"; break;
        case ERR_BUF: err_str = "ERR_BUF (Buffer error)"; break;
        case ERR_TIMEOUT: err_str = "ERR_TIMEOUT (Timeout)"; break;
        case ERR_RTE: err_str = "ERR_RTE (Routing problem)"; break;
        case ERR_INPROGRESS: err_str = "ERR_INPROGRESS (Operation in progress)"; break;
    }
    DEBUG_PRINT("  %s", err_str);

    conn->state = TLS_STATE_ERROR;
    conn->last_error = TLS_ERR_CONNECT;
    conn->pcb = NULL;  // PCB is already freed by lwIP
}

// TCP poll callback
static err_t tcp_poll_callback(void *arg, struct tcp_pcb *tpcb) {
    // Nothing to do here, just maintain connection
    return ERR_OK;
}

// DNS resolution callback
static void dns_callback(const char *name, const ip_addr_t *ipaddr, void *arg) {
    tls_connection_t *conn = (tls_connection_t *)arg;

    if (ipaddr == NULL) {
        DEBUG_PRINT("DNS resolution failed for %s", name);
        conn->state = TLS_STATE_ERROR;
        conn->last_error = TLS_ERR_DNS;
        return;
    }

    DEBUG_PRINT("DNS resolved: %s -> %s", name, ipaddr_ntoa(ipaddr));
    conn->resolved_ip = *ipaddr;
    conn->state = TLS_STATE_DNS_RESOLVED;
}

// mbedtls BIO send function - writes to TCP
static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
    tls_connection_t *conn = (tls_connection_t *)ctx;

    if (conn->pcb == NULL || conn->connection_closed) {
        DEBUG_PRINT("bio_send: connection closed or NULL PCB");
        return MBEDTLS_ERR_SSL_CONN_EOF;
    }

    // Check how much space is available in TCP send buffer
    size_t available = tcp_sndbuf(conn->pcb);
    if (available == 0) {
        DEBUG_PRINT("bio_send: no send buffer space, WANT_WRITE");
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }

    // Limit to available space
    size_t to_send = (len < available) ? len : available;

    // Write to TCP
    err_t err = tcp_write(conn->pcb, buf, to_send, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        DEBUG_PRINT("tcp_write failed: %d", err);
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }

    // Flush the data
    err = tcp_output(conn->pcb);
    if (err != ERR_OK) {
        DEBUG_PRINT("tcp_output failed: %d", err);
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }

    DEBUG_PRINT("bio_send: sent %d bytes (requested %d)", (int)to_send, (int)len);
    return (int)to_send;
}

// mbedtls BIO receive function - reads from ring buffer
static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
    tls_connection_t *conn = (tls_connection_t *)ctx;

    uint16_t available = tls_connection_available(conn);
    if (available == 0) {
        if (conn->connection_closed) {
            return MBEDTLS_ERR_SSL_CONN_EOF;
        }
        return MBEDTLS_ERR_SSL_WANT_READ;
    }

    // Read from ring buffer
    size_t to_read = (len < available) ? len : available;
    uint16_t tail = conn->recv_tail;

    for (size_t i = 0; i < to_read; i++) {
        buf[i] = conn->recv_ring[tail];
        tail = (tail + 1) & (TLS_RECV_RING_SIZE - 1);
    }

    conn->recv_tail = tail;
    return (int)to_read;
}

// Connect with DNS resolution, TCP connection, and TLS handshake
int tls_connection_connect(tls_connection_t *conn, const char *hostname,
                          uint16_t port, uint32_t timeout_ms) {
    if (conn == NULL || hostname == NULL || conn->ssl == NULL) {
        return TLS_ERR_INVALID_PARAM;
    }

    DEBUG_PRINT("Connecting to %s:%d", hostname, port);
    conn->timeout = make_timeout_time_ms(timeout_ms);

    // Phase 1: DNS Resolution
    DEBUG_PRINT("Resolving DNS...");
    conn->state = TLS_STATE_DNS_RESOLVING;

    err_t err = dns_gethostbyname(hostname, &conn->resolved_ip, dns_callback, conn);
    if (err == ERR_OK) {
        // Already cached
        DEBUG_PRINT("DNS cached: %s", ipaddr_ntoa(&conn->resolved_ip));
        conn->state = TLS_STATE_DNS_RESOLVED;
    } else if (err != ERR_INPROGRESS) {
        DEBUG_PRINT("DNS lookup failed: %d", err);
        return TLS_ERR_DNS;
    }

    // Poll until DNS resolves or timeout
    while (conn->state == TLS_STATE_DNS_RESOLVING) {
        cyw43_arch_poll();
        if (time_reached(conn->timeout)) {
            DEBUG_PRINT("DNS timeout");
            return TLS_ERR_TIMEOUT;
        }
        sleep_ms(10);
    }

    if (conn->state == TLS_STATE_ERROR) {
        return conn->last_error;
    }

    // Phase 2: TCP Connection
    DEBUG_PRINT("Creating TCP connection...");

    // Check local network interface status
    struct netif *netif = netif_default;
    if (netif == NULL) {
        DEBUG_PRINT("ERROR: No default network interface");
        return TLS_ERR_CONNECT;
    }
    DEBUG_PRINT("Default netif: %s, IP: %s",
                netif->name,
                ipaddr_ntoa(&netif->ip_addr));
    // Print gateway and netmask separately to avoid ipaddr_ntoa() static buffer issue
    DEBUG_PRINT("Gateway: %s", ipaddr_ntoa(&netif->gw));
    DEBUG_PRINT("Netmask: %s", ipaddr_ntoa(&netif->netmask));
    DEBUG_PRINT("Netif flags: 0x%02x, link up: %d", netif->flags, netif_is_link_up(netif));

    conn->pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (conn->pcb == NULL) {
        DEBUG_PRINT("ERROR: Failed to create TCP PCB - out of memory");
        return TLS_ERR_MEMORY;
    }
    DEBUG_PRINT("TCP PCB created successfully (IPv4)");
    DEBUG_PRINT("PCB state: %d, local port: %d", conn->pcb->state, conn->pcb->local_port);

    // Bind to any local port (helps in NO_SYS mode)
    err = tcp_bind(conn->pcb, IP_ADDR_ANY, 0);
    if (err != ERR_OK) {
        DEBUG_PRINT("tcp_bind failed: %d", err);
        tcp_abort(conn->pcb);
        conn->pcb = NULL;
        return TLS_ERR_CONNECT;
    }
    DEBUG_PRINT("TCP PCB bound to local port");

    // Set up callbacks
    tcp_arg(conn->pcb, conn);
    tcp_err(conn->pcb, tcp_err_callback);
    tcp_recv(conn->pcb, tcp_recv_callback);
    tcp_sent(conn->pcb, tcp_sent_callback);
    tcp_poll(conn->pcb, tcp_poll_callback, 4);
    DEBUG_PRINT("TCP callbacks set");

    // Connect
    conn->state = TLS_STATE_CONNECTING;
    DEBUG_PRINT("Calling tcp_connect to %s:%d", ipaddr_ntoa(&conn->resolved_ip), port);
    DEBUG_PRINT("IP address bytes: %d.%d.%d.%d",
                ((uint8_t*)&conn->resolved_ip.addr)[0],
                ((uint8_t*)&conn->resolved_ip.addr)[1],
                ((uint8_t*)&conn->resolved_ip.addr)[2],
                ((uint8_t*)&conn->resolved_ip.addr)[3]);

    // Check if we're on the same subnet - this affects routing
    ip4_addr_t our_ip = *netif_ip4_addr(netif);
    ip4_addr_t their_ip;
    ip4_addr_copy(their_ip, conn->resolved_ip);
    bool same_subnet = ip4_addr_netcmp(&our_ip, &their_ip, netif_ip4_netmask(netif));
    DEBUG_PRINT("Same subnet check: %d (our IP vs their IP)", same_subnet);

    DEBUG_PRINT("About to call tcp_connect...");
    err = tcp_connect(conn->pcb, &conn->resolved_ip, port, tcp_connected_callback);
    DEBUG_PRINT("tcp_connect returned: %d (ERR_OK=%d)", err, ERR_OK);

    if (err != ERR_OK) {
        const char *err_str = "UNKNOWN";
        switch (err) {
            case ERR_VAL: err_str = "ERR_VAL (Invalid value)"; break;
            case ERR_BUF: err_str = "ERR_BUF (Buffer error)"; break;
            case ERR_MEM: err_str = "ERR_MEM (Out of memory)"; break;
            case ERR_ISCONN: err_str = "ERR_ISCONN (Already connected)"; break;
            case ERR_RTE: err_str = "ERR_RTE (Routing problem)"; break;
            case ERR_USE: err_str = "ERR_USE (Address in use)"; break;
        }
        DEBUG_PRINT("tcp_connect failed immediately: %d - %s", err, err_str);
        tcp_abort(conn->pcb);
        conn->pcb = NULL;
        return TLS_ERR_CONNECT;
    }
    DEBUG_PRINT("tcp_connect initiated, waiting for callback...");
    DEBUG_PRINT("Initial state after tcp_connect: %d", conn->state);

    // Aggressively poll immediately after tcp_connect to give lwIP time to send SYN
    DEBUG_PRINT("Starting aggressive poll loop...");
    for (int i = 0; i < 100; i++) {
        cyw43_arch_poll();
        if (conn->state != TLS_STATE_CONNECTING) {
            DEBUG_PRINT("State changed during aggressive poll at iteration %d: state=%d", i, conn->state);
            break;
        }
        sleep_us(100);  // Very short delay
    }

    if (conn->state == TLS_STATE_CONNECTING) {
        DEBUG_PRINT("Entering main polling loop, state still CONNECTING");
    }

    // Poll until connected or timeout
    int poll_count = 0;
    while (conn->state == TLS_STATE_CONNECTING) {
        cyw43_arch_poll();
        poll_count++;

        if (time_reached(conn->timeout)) {
            DEBUG_PRINT("TCP connect timeout after %d polls", poll_count);
            tcp_abort(conn->pcb);
            conn->pcb = NULL;
            return TLS_ERR_TIMEOUT;
        }
        sleep_ms(10);
    }

    if (conn->state == TLS_STATE_ERROR) {
        if (conn->pcb != NULL) {
            tcp_abort(conn->pcb);
            conn->pcb = NULL;
        }
        return conn->last_error;
    }

    // Phase 3: TLS Handshake
    DEBUG_PRINT("Starting TLS handshake...");
    conn->state = TLS_STATE_HANDSHAKING;

    // Don't set SNI when connecting to IP addresses (some servers reject IP addresses in SNI)
    // For hostname-based connections, mbedtls_ssl_set_hostname() should be called before this
    // mbedtls_ssl_set_hostname(conn->ssl, hostname);

    // Set BIO callbacks
    mbedtls_ssl_set_bio(conn->ssl, conn, bio_send, bio_recv, NULL);

    // Perform handshake
    int ret;
    int handshake_attempts = 0;
    conn->timeout = make_timeout_time_ms(15000);  // 15s for handshake
    while ((ret = mbedtls_ssl_handshake(conn->ssl)) != 0) {
        // Poll lwIP BEFORE checking the error to process any pending packets
        cyw43_arch_poll();

        handshake_attempts++;
        if (handshake_attempts % 100 == 0) {
            uint16_t ring_avail = tls_connection_available(conn);
            DEBUG_PRINT("Handshake loop: attempt %d, ret=-0x%04x, ring_avail=%d", handshake_attempts, -ret, ring_avail);
        }

        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            DEBUG_PRINT("TLS handshake failed: -0x%04x", -ret);

            // Decode the specific error
            if (ret == MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE) {
                // Extract alert level and description from the error
                // The lower byte contains the alert description
                DEBUG_PRINT("Server sent fatal TLS alert");
            } else if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
                DEBUG_PRINT("Certificate verification failed");
            }

            // Always print verification result to see if cert verification is the issue
            uint32_t flags = mbedtls_ssl_get_verify_result(conn->ssl);
            DEBUG_PRINT("Verification flags: 0x%08lx", flags);
            if (flags != 0) {
                DEBUG_PRINT("Certificate verification FAILED:");
                if (flags & MBEDTLS_X509_BADCERT_EXPIRED) DEBUG_PRINT("  - Certificate expired");
                if (flags & MBEDTLS_X509_BADCERT_REVOKED) DEBUG_PRINT("  - Certificate revoked");
                if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) DEBUG_PRINT("  - CN mismatch");
                if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) DEBUG_PRINT("  - Not trusted");
            } else {
                DEBUG_PRINT("Certificate verification passed (server cert OK)");
                DEBUG_PRINT("Server likely rejected OUR client certificate");
            }

            tcp_abort(conn->pcb);
            conn->pcb = NULL;
            return TLS_ERR_HANDSHAKE;
        }

        // Poll lwIP again after handshake attempt
        cyw43_arch_poll();

        if (time_reached(conn->timeout)) {
            DEBUG_PRINT("TLS handshake timeout");
            tcp_abort(conn->pcb);
            conn->pcb = NULL;
            return TLS_ERR_TIMEOUT;
        }

        // Short sleep to avoid busy-waiting (reduced from 10ms to 1ms for better responsiveness)
        sleep_ms(1);
    }

    DEBUG_PRINT("TLS handshake complete");
    conn->state = TLS_STATE_READY;
    conn->handshake_complete = true;

    return TLS_OK;
}

// Send data over TLS
int tls_connection_send(tls_connection_t *conn, const uint8_t *data,
                       size_t len, uint32_t timeout_ms) {
    if (conn == NULL || data == NULL || conn->state != TLS_STATE_READY) {
        return TLS_ERR_INVALID_PARAM;
    }

    conn->timeout = make_timeout_time_ms(timeout_ms);
    size_t total_sent = 0;

    while (total_sent < len) {
        int ret = mbedtls_ssl_write(conn->ssl, data + total_sent, len - total_sent);

        if (ret > 0) {
            total_sent += ret;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ) {
            // Need to poll and retry
            cyw43_arch_poll();
            if (time_reached(conn->timeout)) {
                DEBUG_PRINT("Send timeout");
                return TLS_ERR_TIMEOUT;
            }
            sleep_ms(10);
        } else {
            DEBUG_PRINT("TLS send failed: -0x%04x", -ret);
            return TLS_ERR_SEND;
        }
    }

    return (int)total_sent;
}

// Receive data from TLS
int tls_connection_recv(tls_connection_t *conn, uint8_t *buffer,
                       size_t buffer_size, uint32_t timeout_ms) {
    if (conn == NULL || buffer == NULL || conn->state != TLS_STATE_READY) {
        return TLS_ERR_INVALID_PARAM;
    }

    conn->timeout = make_timeout_time_ms(timeout_ms);

    while (true) {
        int ret = mbedtls_ssl_read(conn->ssl, buffer, buffer_size);

        if (ret > 0) {
            return ret;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            // Need to poll and retry
            cyw43_arch_poll();
            if (time_reached(conn->timeout)) {
                DEBUG_PRINT("Receive timeout");
                return TLS_ERR_TIMEOUT;
            }
            sleep_ms(10);
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            // Connection closed gracefully
            DEBUG_PRINT("Connection closed by peer");
            conn->connection_closed = true;
            return 0;
        } else {
            DEBUG_PRINT("TLS receive failed: -0x%04x", -ret);
            return TLS_ERR_RECV;
        }
    }
}

// Close connection
void tls_connection_close(tls_connection_t *conn) {
    if (conn == NULL) {
        return;
    }

    DEBUG_PRINT("Closing TLS connection");

    // Close TLS session gracefully
    if (conn->handshake_complete && conn->ssl != NULL) {
        mbedtls_ssl_close_notify(conn->ssl);
    }

    // Close TCP connection
    if (conn->pcb != NULL) {
        tcp_arg(conn->pcb, NULL);
        tcp_err(conn->pcb, NULL);
        tcp_recv(conn->pcb, NULL);
        tcp_sent(conn->pcb, NULL);
        tcp_poll(conn->pcb, NULL, 0);

        tcp_close(conn->pcb);
        conn->pcb = NULL;
    }

    conn->state = TLS_STATE_CLOSED;
    conn->connection_closed = true;
}

// Get connection state
tls_conn_state_t tls_connection_get_state(tls_connection_t *conn) {
    if (conn == NULL) {
        return TLS_STATE_ERROR;
    }
    return conn->state;
}

// Get last error
int tls_connection_get_error(tls_connection_t *conn) {
    if (conn == NULL) {
        return TLS_ERR_INVALID_PARAM;
    }
    return conn->last_error;
}
