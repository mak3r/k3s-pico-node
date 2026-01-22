#include "http_client.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Convert HTTP method to string
static const char* method_to_string(http_method_t method) {
    switch (method) {
        case HTTP_METHOD_GET: return "GET";
        case HTTP_METHOD_POST: return "POST";
        case HTTP_METHOD_PATCH: return "PATCH";
        default: return "GET";
    }
}

// Build HTTP request
int http_build_request(char *buffer, size_t buffer_size,
                      http_method_t method,
                      const char *host, uint16_t port,
                      const char *path,
                      const char *body,
                      const char *content_type) {
    if (buffer == NULL || host == NULL || path == NULL) {
        return -1;
    }

    int written = 0;
    const char *method_str = method_to_string(method);

    // Request line: METHOD /path HTTP/1.1
    written = snprintf(buffer, buffer_size,
                      "%s %s HTTP/1.1\r\n",
                      method_str, path);

    if (written < 0 || (size_t)written >= buffer_size) {
        return -1;
    }

    // Host header
    int n = snprintf(buffer + written, buffer_size - written,
                    "Host: %s:%d\r\n", host, port);
    if (n < 0 || written + n >= (int)buffer_size) {
        return -1;
    }
    written += n;

    // User-Agent header
    n = snprintf(buffer + written, buffer_size - written,
                "User-Agent: k3s-pico-node/1.0\r\n");
    if (n < 0 || written + n >= (int)buffer_size) {
        return -1;
    }
    written += n;

    // Accept header
    n = snprintf(buffer + written, buffer_size - written,
                "Accept: application/json\r\n");
    if (n < 0 || written + n >= (int)buffer_size) {
        return -1;
    }
    written += n;

    // Connection header (close for simplicity)
    n = snprintf(buffer + written, buffer_size - written,
                "Connection: close\r\n");
    if (n < 0 || written + n >= (int)buffer_size) {
        return -1;
    }
    written += n;

    // For POST/PATCH requests, add Content-Type and Content-Length
    if (body != NULL) {
        size_t body_len = strlen(body);

        // Content-Type
        const char *ct = content_type ? content_type : "application/json";
        n = snprintf(buffer + written, buffer_size - written,
                    "Content-Type: %s\r\n", ct);
        if (n < 0 || written + n >= (int)buffer_size) {
            return -1;
        }
        written += n;

        // Content-Length
        n = snprintf(buffer + written, buffer_size - written,
                    "Content-Length: %zu\r\n", body_len);
        if (n < 0 || written + n >= (int)buffer_size) {
            return -1;
        }
        written += n;

        // End of headers
        n = snprintf(buffer + written, buffer_size - written, "\r\n");
        if (n < 0 || written + n >= (int)buffer_size) {
            return -1;
        }
        written += n;

        // Body
        if (written + body_len >= buffer_size) {
            return -1;
        }
        memcpy(buffer + written, body, body_len);
        written += body_len;
    } else {
        // Just end of headers for GET
        n = snprintf(buffer + written, buffer_size - written, "\r\n");
        if (n < 0 || written + n >= (int)buffer_size) {
            return -1;
        }
        written += n;
    }

    return written;
}

// Case-insensitive string comparison
static int strcasecmp_custom(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        if (tolower((unsigned char)*s1) != tolower((unsigned char)*s2)) {
            return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
        }
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

// Case-insensitive string prefix comparison
static int strncasecmp_custom(const char *s1, const char *s2, size_t n) {
    while (n > 0 && *s1 && *s2) {
        if (tolower((unsigned char)*s1) != tolower((unsigned char)*s2)) {
            return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
        }
        s1++;
        s2++;
        n--;
    }
    return (n == 0) ? 0 : (tolower((unsigned char)*s1) - tolower((unsigned char)*s2));
}

// Extract header value
int http_get_header(const char *response_buffer, const char *header_name,
                   char *value_buffer, size_t value_buffer_size) {
    if (response_buffer == NULL || header_name == NULL || value_buffer == NULL) {
        return -1;
    }

    // Find the header line
    const char *line = response_buffer;
    size_t header_name_len = strlen(header_name);

    while (*line != '\0') {
        // Check if this line starts with the header name
        if (strncasecmp_custom(line, header_name, header_name_len) == 0 &&
            line[header_name_len] == ':') {

            // Skip header name and colon
            const char *value_start = line + header_name_len + 1;

            // Skip leading whitespace
            while (*value_start == ' ' || *value_start == '\t') {
                value_start++;
            }

            // Find end of line
            const char *value_end = value_start;
            while (*value_end != '\r' && *value_end != '\n' && *value_end != '\0') {
                value_end++;
            }

            // Copy value
            size_t value_len = value_end - value_start;
            if (value_len >= value_buffer_size) {
                value_len = value_buffer_size - 1;
            }
            memcpy(value_buffer, value_start, value_len);
            value_buffer[value_len] = '\0';

            return 0;
        }

        // Move to next line
        while (*line != '\n' && *line != '\0') {
            line++;
        }
        if (*line == '\n') {
            line++;
        }
    }

    return -1;  // Header not found
}

// Parse HTTP response
int http_parse_response(char *response_buffer, size_t response_length,
                       http_response_t *response) {
    if (response_buffer == NULL || response == NULL || response_length == 0) {
        return -1;
    }

    // Initialize response structure
    memset(response, 0, sizeof(http_response_t));

    // Parse status line: HTTP/1.1 200 OK
    char *line = response_buffer;
    char *line_end = strstr(line, "\r\n");
    if (line_end == NULL) {
        DEBUG_PRINT("Invalid HTTP response: no CRLF found");
        return -1;
    }

    // Extract status code
    char *status_start = strchr(line, ' ');
    if (status_start == NULL) {
        DEBUG_PRINT("Invalid HTTP status line");
        return -1;
    }
    status_start++;  // Skip space

    response->status_code = atoi(status_start);
    DEBUG_PRINT("HTTP status code: %d", response->status_code);

    // Move to headers
    line = line_end + 2;

    // Parse headers until blank line
    while (true) {
        line_end = strstr(line, "\r\n");
        if (line_end == NULL) {
            DEBUG_PRINT("Malformed headers");
            return -1;
        }

        // Check for end of headers (blank line)
        if (line == line_end) {
            // Found blank line, body starts after this
            response->body = line_end + 2;
            break;
        }

        // Check for Content-Length header
        if (strncasecmp_custom(line, "Content-Length:", 15) == 0) {
            response->content_length = atoi(line + 15);
            DEBUG_PRINT("Content-Length: %zu", response->content_length);
        }

        // Check for Transfer-Encoding: chunked
        if (strncasecmp_custom(line, "Transfer-Encoding:", 18) == 0) {
            if (strstr(line, "chunked") != NULL) {
                response->chunked = true;
                DEBUG_PRINT("Transfer-Encoding: chunked");
            }
        }

        // Move to next line
        line = line_end + 2;
    }

    // Calculate body length
    if (response->body != NULL) {
        size_t header_length = response->body - response_buffer;
        if (response_length > header_length) {
            response->body_length = response_length - header_length;
            DEBUG_PRINT("Body length: %zu bytes", response->body_length);
        }
    }

    return 0;
}

// Get HTTP status string
const char* http_status_string(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 409: return "Conflict";
        case 422: return "Unprocessable Entity";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}
