#include "kubelet_server.h"
#include "config.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include <stdio.h>
#include <string.h>

// HTTP responses
static const char *healthz_response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 2\r\n"
    "Connection: close\r\n"
    "\r\n"
    "ok";

static const char *metrics_response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain; version=0.0.4\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n"
    "\r\n";

static const char *not_found_response =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 9\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Not Found";

// TCP listener
static struct tcp_pcb *kubelet_listen_pcb = NULL;

// Connection state structure
typedef struct {
    struct tcp_pcb *pcb;
    char recv_buffer[512];
    int recv_len;
    bool response_sent;
} kubelet_conn_t;

// Forward declarations
static err_t kubelet_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t kubelet_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
static void kubelet_err(void *arg, err_t err);
static err_t kubelet_sent(void *arg, struct tcp_pcb *pcb, u16_t len);

int kubelet_server_init(void) {
    DEBUG_PRINT("Initializing kubelet server on port %d", KUBELET_PORT);

    // Create TCP listening socket
    kubelet_listen_pcb = tcp_new();
    if (kubelet_listen_pcb == NULL) {
        printf("ERROR: Failed to create TCP PCB for kubelet server\n");
        return -1;
    }

    // Bind to port
    err_t err = tcp_bind(kubelet_listen_pcb, IP_ADDR_ANY, KUBELET_PORT);
    if (err != ERR_OK) {
        printf("ERROR: Failed to bind kubelet server to port %d: %d\n",
               KUBELET_PORT, err);
        tcp_close(kubelet_listen_pcb);
        kubelet_listen_pcb = NULL;
        return -1;
    }

    // Start listening
    kubelet_listen_pcb = tcp_listen(kubelet_listen_pcb);
    if (kubelet_listen_pcb == NULL) {
        printf("ERROR: Failed to listen on kubelet port\n");
        return -1;
    }

    // Set accept callback
    tcp_accept(kubelet_listen_pcb, kubelet_accept);

    printf("Kubelet server listening on port %d\n", KUBELET_PORT);
    DEBUG_PRINT("  Endpoints: /healthz, /metrics");

    return 0;
}

static err_t kubelet_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    if (err != ERR_OK || newpcb == NULL) {
        return ERR_VAL;
    }

    DEBUG_PRINT("Kubelet: New connection from %s:%d",
               ip4addr_ntoa(ip_2_ip4(&newpcb->remote_ip)),
               newpcb->remote_port);

    // Allocate connection state
    kubelet_conn_t *conn = (kubelet_conn_t *)calloc(1, sizeof(kubelet_conn_t));
    if (conn == NULL) {
        printf("ERROR: Failed to allocate connection state\n");
        tcp_abort(newpcb);
        return ERR_ABRT;
    }

    conn->pcb = newpcb;
    conn->recv_len = 0;
    conn->response_sent = false;

    // Set up callbacks
    tcp_arg(newpcb, conn);
    tcp_recv(newpcb, kubelet_recv);
    tcp_err(newpcb, kubelet_err);
    tcp_sent(newpcb, kubelet_sent);

    return ERR_OK;
}

static err_t kubelet_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    kubelet_conn_t *conn = (kubelet_conn_t *)arg;

    if (p == NULL) {
        // Connection closed
        DEBUG_PRINT("Kubelet: Connection closed");
        tcp_close(pcb);
        free(conn);
        return ERR_OK;
    }

    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }

    // Copy data to buffer (up to buffer size)
    int space_left = sizeof(conn->recv_buffer) - conn->recv_len;
    int copy_len = (p->tot_len < space_left) ? p->tot_len : space_left;

    pbuf_copy_partial(p, conn->recv_buffer + conn->recv_len, copy_len, 0);
    conn->recv_len += copy_len;

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    // Null-terminate for string operations
    if (conn->recv_len < sizeof(conn->recv_buffer)) {
        conn->recv_buffer[conn->recv_len] = '\0';
    } else {
        conn->recv_buffer[sizeof(conn->recv_buffer) - 1] = '\0';
    }

    // Simple HTTP parsing - look for GET requests
    const char *response = NULL;

    if (strstr(conn->recv_buffer, "GET /healthz") != NULL) {
        DEBUG_PRINT("Kubelet: GET /healthz");
        response = healthz_response;
    } else if (strstr(conn->recv_buffer, "GET /metrics") != NULL) {
        DEBUG_PRINT("Kubelet: GET /metrics");
        response = metrics_response;
    } else if (strstr(conn->recv_buffer, "GET ") != NULL) {
        DEBUG_PRINT("Kubelet: GET (unknown path)");
        response = not_found_response;
    }

    // Send response if we have one
    if (response != NULL && !conn->response_sent) {
        err_t write_err = tcp_write(pcb, response, strlen(response),
                                    TCP_WRITE_FLAG_COPY);
        if (write_err == ERR_OK) {
            tcp_output(pcb);
            conn->response_sent = true;
            DEBUG_PRINT("Kubelet: Response sent (%d bytes)", strlen(response));
        } else {
            printf("ERROR: Failed to write response: %d\n", write_err);
        }

        // Close connection after sending response
        tcp_close(pcb);
        free(conn);
    }

    return ERR_OK;
}

static void kubelet_err(void *arg, err_t err) {
    kubelet_conn_t *conn = (kubelet_conn_t *)arg;

    if (conn != NULL) {
        DEBUG_PRINT("Kubelet: Connection error: %d", err);
        free(conn);
    }
}

static err_t kubelet_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    DEBUG_PRINT("Kubelet: Sent %d bytes", len);
    return ERR_OK;
}

void kubelet_server_poll(void) {
    // Polling is handled by cyw43_arch_poll() in main loop
    // Nothing to do here for lwIP raw API
}

void kubelet_server_shutdown(void) {
    if (kubelet_listen_pcb != NULL) {
        tcp_close(kubelet_listen_pcb);
        kubelet_listen_pcb = NULL;
        DEBUG_PRINT("Kubelet server shut down");
    }
}
