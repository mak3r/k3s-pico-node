#include "k3s_client.h"
#include "tcp_connection.h"
#include "http_client.h"
#include "time_sync.h"
#include "config.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Connection state
static bool client_initialized = false;

// Request timeout (30 seconds)
#define REQUEST_TIMEOUT_MS 30000

// Connection timeout (10 seconds)
#define CONNECT_TIMEOUT_MS 10000

int k3s_client_init(void) {
    DEBUG_PRINT("Initializing k3s API client (HTTP-only mode)...");
    DEBUG_PRINT("Will connect to nginx proxy at %s:%d", K3S_SERVER_IP, K3S_SERVER_PORT);
    DEBUG_PRINT("Proxy will forward to k3s API with TLS termination");

    client_initialized = true;
    DEBUG_PRINT("K3s client initialized successfully");

    return 0;
}

// Helper function to send HTTP request and receive response
// This implements the full HTTP communication flow
static int k3s_request(const char *method, const char *path, const char *body,
                      char *response, int response_size) {
    if (!client_initialized) {
        printf("ERROR: K3s client not initialized\n");
        return -1;
    }

    if (path == NULL) {
        printf("ERROR: Invalid parameters\n");
        return -1;
    }

    DEBUG_PRINT("K3s %s request: %s", method, path);

    int ret = -1;
    tcp_connection_t conn;
    char *request_buffer = NULL;
    char *response_buffer = NULL;

    // Initialize connection
    if (tcp_connection_init(&conn) != 0) {
        printf("ERROR: Failed to initialize TCP connection\n");
        goto cleanup;
    }

    // Connect to nginx proxy (not k3s API directly)
    DEBUG_PRINT("Connecting to nginx proxy at %s:%d...", K3S_SERVER_IP, K3S_SERVER_PORT);
    ret = tcp_connection_connect(&conn, K3S_SERVER_IP, K3S_SERVER_PORT, CONNECT_TIMEOUT_MS);
    if (ret != TCP_OK) {
        printf("ERROR: Failed to connect to nginx proxy: %s\n", tcp_error_to_string(ret));
        goto cleanup;
    }
    DEBUG_PRINT("Connected to nginx proxy");

    // Allocate request buffer
    request_buffer = malloc(HTTP_REQUEST_BUFFER_SIZE);
    if (request_buffer == NULL) {
        printf("ERROR: Failed to allocate request buffer\n");
        ret = -1;
        goto cleanup;
    }

    // Determine HTTP method
    http_method_t http_method;
    if (strcmp(method, "GET") == 0) {
        http_method = HTTP_METHOD_GET;
    } else if (strcmp(method, "POST") == 0) {
        http_method = HTTP_METHOD_POST;
    } else if (strcmp(method, "PATCH") == 0) {
        http_method = HTTP_METHOD_PATCH;
    } else {
        printf("ERROR: Unsupported HTTP method: %s\n", method);
        ret = -1;
        goto cleanup;
    }

    // Build HTTP request
    const char *content_type = NULL;
    if (http_method == HTTP_METHOD_PATCH) {
        // K8s PATCH uses strategic merge patch by default
        content_type = "application/strategic-merge-patch+json";
    } else if (body != NULL) {
        content_type = "application/json";
    }

    int request_len = http_build_request(
        request_buffer, HTTP_REQUEST_BUFFER_SIZE,
        http_method,
        K3S_SERVER_IP, K3S_SERVER_PORT,
        path,
        body,
        content_type
    );

    if (request_len < 0) {
        printf("ERROR: Failed to build HTTP request\n");
        ret = -1;
        goto cleanup;
    }

    DEBUG_PRINT("Sending HTTP request (%d bytes)...", request_len);
    if (DEBUG_ENABLE) {
        // Print first 200 chars of request for debugging
        int preview_len = (request_len < 200) ? request_len : 200;
        printf("[DEBUG] Request preview:\n%.200s%s\n",
               request_buffer,
               (request_len > 200) ? "..." : "");
    }

    // Send request
    ret = tcp_connection_send(&conn, (uint8_t *)request_buffer, request_len, REQUEST_TIMEOUT_MS);
    if (ret < 0) {
        printf("ERROR: Failed to send HTTP request: %s\n", tcp_error_to_string(ret));
        goto cleanup;
    }
    DEBUG_PRINT("Request sent successfully");

    // Allocate response buffer
    response_buffer = malloc(HTTP_RESPONSE_BUFFER_SIZE);
    if (response_buffer == NULL) {
        printf("ERROR: Failed to allocate response buffer\n");
        ret = -1;
        goto cleanup;
    }

    // Receive response
    DEBUG_PRINT("Receiving HTTP response...");
    int total_received = 0;
    absolute_time_t start_time = get_absolute_time();

    while (total_received < HTTP_RESPONSE_BUFFER_SIZE - 1) {
        // Check for timeout
        if (absolute_time_diff_us(start_time, get_absolute_time()) > REQUEST_TIMEOUT_MS * 1000) {
            printf("ERROR: Response timeout\n");
            ret = TCP_ERR_TIMEOUT;
            goto cleanup;
        }

        // Try to receive more data
        int received = tcp_connection_recv(
            &conn,
            (uint8_t *)(response_buffer + total_received),
            HTTP_RESPONSE_BUFFER_SIZE - total_received - 1,
            1000  // 1 second timeout per recv
        );

        if (received < 0) {
            printf("ERROR: Failed to receive response: %s\n", tcp_error_to_string(received));
            ret = received;
            goto cleanup;
        } else if (received == 0) {
            // Connection closed - this is expected with Connection: close
            DEBUG_PRINT("Connection closed by server (received %d bytes total)", total_received);
            break;
        }

        total_received += received;

        // Check if we have a complete HTTP response
        // Look for end of headers + body
        response_buffer[total_received] = '\0';

        // If we see "\r\n\r\n", we have headers
        char *body_start = strstr(response_buffer, "\r\n\r\n");
        if (body_start != NULL) {
            // Check if we have Content-Length
            char content_length_str[32];
            if (http_get_header(response_buffer, "Content-Length",
                              content_length_str, sizeof(content_length_str)) == 0) {
                int content_length = atoi(content_length_str);
                int headers_length = (body_start + 4) - response_buffer;
                int expected_total = headers_length + content_length;

                if (total_received >= expected_total) {
                    DEBUG_PRINT("Received complete response with Content-Length");
                    break;
                }
            }
        }

        // Poll WiFi to keep connection alive
        cyw43_arch_poll();
    }

    if (total_received == 0) {
        printf("ERROR: No response received\n");
        ret = -1;
        goto cleanup;
    }

    // Null-terminate response
    response_buffer[total_received] = '\0';
    DEBUG_PRINT("Received %d bytes", total_received);

    // Parse HTTP response
    http_response_t http_response;
    ret = http_parse_response(response_buffer, total_received, &http_response);
    if (ret != 0) {
        printf("ERROR: Failed to parse HTTP response\n");
        goto cleanup;
    }

    DEBUG_PRINT("HTTP %d %s", http_response.status_code,
                http_status_string(http_response.status_code));

    // Extract and sync time from Date header
    char date_header[64];
    if (http_get_header(response_buffer, "Date", date_header, sizeof(date_header)) == 0) {
        if (time_sync_update_from_header(date_header) == 0) {
            if (!time_sync_is_synced()) {
                DEBUG_PRINT("Time synchronized from server");
            }
        }
    }

    // Check for HTTP errors
    if (http_response.status_code >= 400) {
        printf("ERROR: HTTP %d %s\n", http_response.status_code,
               http_status_string(http_response.status_code));
        if (http_response.body != NULL && http_response.body_length > 0) {
            // Print error body (truncated)
            int error_preview = (http_response.body_length < 200) ?
                               http_response.body_length : 200;
            printf("Error response: %.*s%s\n",
                   error_preview, http_response.body,
                   (http_response.body_length > 200) ? "..." : "");
        }
        ret = -1;
        goto cleanup;
    }

    // Copy response body to output buffer if provided
    if (response != NULL && response_size > 0) {
        if (http_response.body != NULL && http_response.body_length > 0) {
            int copy_len = (http_response.body_length < (size_t)(response_size - 1)) ?
                          http_response.body_length : (response_size - 1);
            memcpy(response, http_response.body, copy_len);
            response[copy_len] = '\0';
            DEBUG_PRINT("Copied %d bytes to response buffer", copy_len);
        } else {
            response[0] = '\0';
        }
    }

    ret = 0;  // Success

cleanup:
    // Close connection
    tcp_connection_close(&conn);

    // Free buffers
    if (request_buffer != NULL) {
        free(request_buffer);
    }
    if (response_buffer != NULL) {
        free(response_buffer);
    }

    if (ret == 0) {
        DEBUG_PRINT("Request completed successfully");
    } else {
        DEBUG_PRINT("Request failed with error code %d", ret);
    }

    return ret;
}

int k3s_client_get(const char *path, char *response, int response_size) {
    if (response == NULL || response_size <= 0) {
        printf("ERROR: Invalid response buffer\n");
        return -1;
    }

    return k3s_request("GET", path, NULL, response, response_size);
}

int k3s_client_post(const char *path, const char *body) {
    if (body == NULL) {
        printf("ERROR: POST requires a body\n");
        return -1;
    }

    return k3s_request("POST", path, body, NULL, 0);
}

int k3s_client_patch(const char *path, const char *body) {
    if (body == NULL) {
        printf("ERROR: PATCH requires a body\n");
        return -1;
    }

    return k3s_request("PATCH", path, body, NULL, 0);
}

void k3s_client_shutdown(void) {
    if (!client_initialized) {
        return;
    }

    client_initialized = false;
    DEBUG_PRINT("K3s client shutdown");
}
