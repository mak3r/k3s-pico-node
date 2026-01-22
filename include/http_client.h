#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * HTTP Client Layer
 *
 * Simple HTTP/1.1 client for Kubernetes API communication.
 * Supports GET, POST, and PATCH methods with JSON bodies.
 */

// HTTP methods
typedef enum {
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    HTTP_METHOD_PATCH
} http_method_t;

// HTTP response structure
typedef struct {
    int status_code;           // HTTP status code (e.g., 200, 404)
    char *body;                // Response body (points into response buffer)
    size_t body_length;        // Length of response body
    size_t content_length;     // Content-Length from header
    bool chunked;              // True if Transfer-Encoding: chunked
} http_response_t;

/**
 * Build an HTTP request
 *
 * Constructs a complete HTTP/1.1 request with proper headers for Kubernetes API.
 *
 * @param buffer Buffer to store the request
 * @param buffer_size Size of buffer
 * @param method HTTP method (GET, POST, PATCH)
 * @param host Hostname (for Host header)
 * @param port Port number
 * @param path Request path (e.g., "/api/v1/nodes")
 * @param body Request body (NULL for GET, JSON string for POST/PATCH)
 * @param content_type Content-Type header value (NULL for GET)
 * @return Length of request on success, -1 on error
 */
int http_build_request(char *buffer, size_t buffer_size,
                      http_method_t method,
                      const char *host, uint16_t port,
                      const char *path,
                      const char *body,
                      const char *content_type);

/**
 * Parse HTTP response
 *
 * Parses an HTTP response and extracts status code, headers, and body.
 * This modifies the response buffer (inserts null terminators).
 *
 * @param response_buffer Raw response data
 * @param response_length Length of response data
 * @param response Output structure with parsed response
 * @return 0 on success, -1 on error
 */
int http_parse_response(char *response_buffer, size_t response_length,
                       http_response_t *response);

/**
 * Get HTTP status code description
 *
 * @param status_code HTTP status code
 * @return Human-readable description
 */
const char* http_status_string(int status_code);

/**
 * Extract a specific header value from response
 *
 * @param response_buffer Raw response data (before body)
 * @param header_name Header name to search for (case-insensitive)
 * @param value_buffer Buffer to store header value
 * @param value_buffer_size Size of value buffer
 * @return 0 on success, -1 if header not found
 */
int http_get_header(const char *response_buffer, const char *header_name,
                   char *value_buffer, size_t value_buffer_size);

#endif // HTTP_CLIENT_H
