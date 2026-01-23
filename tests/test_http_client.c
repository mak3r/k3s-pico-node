/**
 * Unit tests for HTTP client functions
 *
 * These tests run on the host machine (x86/ARM64) and validate
 * HTTP request building and response parsing without requiring
 * actual network communication or Pico hardware.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

// Mock config.h definitions
#define DEBUG_ENABLE 1
#define DEBUG_PRINT(fmt, ...) printf("[TEST] " fmt "\n", ##__VA_ARGS__)

// Include the actual HTTP client code (we'll compile it separately)
// For now, we'll declare the functions we want to test
extern int http_build_request(char *buffer, size_t buffer_size,
                              int method,
                              const char *host, unsigned short port,
                              const char *path,
                              const char *body,
                              const char *content_type);

extern int http_parse_response(char *response_buffer, size_t response_length,
                               void *response);

extern int http_get_header(const char *response_buffer, const char *header_name,
                          char *value_buffer, size_t value_buffer_size);

extern const char* http_status_string(int status_code);

// HTTP method enum
typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST = 1,
    HTTP_METHOD_PATCH = 2
} http_method_t;

// Test counters
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        if (condition) { \
            printf("  ✓ %s\n", message); \
            tests_passed++; \
        } else { \
            printf("  ✗ %s\n", message); \
            tests_failed++; \
        } \
    } while(0)

// Test: Build GET request
void test_build_get_request() {
    printf("\n[TEST] Building GET request\n");

    char buffer[1024];
    int len = http_build_request(
        buffer, sizeof(buffer),
        HTTP_METHOD_GET,
        "192.168.86.232", 6080,
        "/api/v1/nodes",
        NULL,
        NULL
    );

    TEST_ASSERT(len > 0, "Request built successfully");
    TEST_ASSERT(strstr(buffer, "GET /api/v1/nodes HTTP/1.1") != NULL, "GET request line present");
    TEST_ASSERT(strstr(buffer, "Host: 192.168.86.232:6080") != NULL, "Host header present");
    TEST_ASSERT(strstr(buffer, "Connection: close") != NULL, "Connection header present");
    TEST_ASSERT(strstr(buffer, "\r\n\r\n") != NULL, "Request ends with CRLF");

    printf("  Request preview:\n%.*s\n", len > 200 ? 200 : len, buffer);
}

// Test: Build POST request with body
void test_build_post_request() {
    printf("\n[TEST] Building POST request with JSON body\n");

    char buffer[2048];
    const char *body = "{\"kind\":\"Node\",\"metadata\":{\"name\":\"test-node\"}}";

    int len = http_build_request(
        buffer, sizeof(buffer),
        HTTP_METHOD_POST,
        "192.168.86.232", 6080,
        "/api/v1/nodes",
        body,
        "application/json"
    );

    TEST_ASSERT(len > 0, "Request built successfully");
    TEST_ASSERT(strstr(buffer, "POST /api/v1/nodes HTTP/1.1") != NULL, "POST request line present");
    TEST_ASSERT(strstr(buffer, "Content-Type: application/json") != NULL, "Content-Type header present");

    char content_length_str[64];
    snprintf(content_length_str, sizeof(content_length_str), "Content-Length: %zu", strlen(body));
    TEST_ASSERT(strstr(buffer, content_length_str) != NULL, "Content-Length header correct");
    TEST_ASSERT(strstr(buffer, body) != NULL, "Body included in request");
}

// Test: Build PATCH request
void test_build_patch_request() {
    printf("\n[TEST] Building PATCH request\n");

    char buffer[2048];
    const char *body = "{\"status\":{\"conditions\":[{\"type\":\"Ready\"}]}}";

    int len = http_build_request(
        buffer, sizeof(buffer),
        HTTP_METHOD_PATCH,
        "192.168.86.232", 6080,
        "/api/v1/nodes/pico-node-1/status",
        body,
        "application/strategic-merge-patch+json"
    );

    TEST_ASSERT(len > 0, "Request built successfully");
    TEST_ASSERT(strstr(buffer, "PATCH /api/v1/nodes/pico-node-1/status HTTP/1.1") != NULL,
                "PATCH request line present");
    TEST_ASSERT(strstr(buffer, "Content-Type: application/strategic-merge-patch+json") != NULL,
                "Strategic merge patch content type present");
}

// Test: Parse HTTP 200 response
void test_parse_200_response() {
    printf("\n[TEST] Parsing HTTP 200 OK response\n");

    char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 27\r\n"
        "\r\n"
        "{\"status\":\"success\",\"ok\":1}";

    char value_buffer[128];

    // Test header extraction
    int result = http_get_header(response, "Content-Type", value_buffer, sizeof(value_buffer));
    TEST_ASSERT(result == 0, "Content-Type header found");
    TEST_ASSERT(strcmp(value_buffer, "application/json") == 0, "Content-Type value correct");

    result = http_get_header(response, "Content-Length", value_buffer, sizeof(value_buffer));
    TEST_ASSERT(result == 0, "Content-Length header found");
    TEST_ASSERT(strcmp(value_buffer, "27") == 0, "Content-Length value correct");

    result = http_get_header(response, "X-Missing-Header", value_buffer, sizeof(value_buffer));
    TEST_ASSERT(result != 0, "Missing header returns error");
}

// Test: Parse HTTP error response
void test_parse_error_response() {
    printf("\n[TEST] Parsing HTTP error responses\n");

    // Test various status codes
    struct {
        int code;
        const char *expected_string;
    } test_cases[] = {
        {200, "OK"},
        {201, "Created"},
        {400, "Bad Request"},
        {401, "Unauthorized"},
        {404, "Not Found"},
        {409, "Conflict"},
        {500, "Internal Server Error"},
        {999, "Unknown"}
    };

    for (int i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        const char *status_str = http_status_string(test_cases[i].code);
        TEST_ASSERT(strcmp(status_str, test_cases[i].expected_string) == 0,
                   "Status code string correct");
        printf("    %d -> %s\n", test_cases[i].code, status_str);
    }
}

// Test: Buffer overflow protection
void test_buffer_overflow_protection() {
    printf("\n[TEST] Buffer overflow protection\n");

    char small_buffer[64];
    const char *large_body = "{\"very\":\"large\",\"json\":\"body\",\"that\":\"exceeds\",\"buffer\":\"size\"}";

    int len = http_build_request(
        small_buffer, sizeof(small_buffer),
        HTTP_METHOD_POST,
        "192.168.86.232", 6080,
        "/api/v1/nodes",
        large_body,
        "application/json"
    );

    TEST_ASSERT(len < 0, "Overflow detected and request building failed safely");
}

// Test: Header extraction edge cases
void test_header_extraction_edge_cases() {
    printf("\n[TEST] Header extraction edge cases\n");

    // Test case-insensitive matching
    char response[] =
        "HTTP/1.1 200 OK\r\n"
        "content-type: application/json\r\n"
        "CONTENT-LENGTH: 10\r\n"
        "\r\n"
        "0123456789";

    char value_buffer[128];

    // Should find lowercase header with any case query
    int result = http_get_header(response, "Content-Type", value_buffer, sizeof(value_buffer));
    TEST_ASSERT(result == 0, "Case-insensitive header lookup works");

    result = http_get_header(response, "CONTENT-TYPE", value_buffer, sizeof(value_buffer));
    TEST_ASSERT(result == 0, "Uppercase header lookup works");
}

// Test: Response with chunked encoding
void test_chunked_response() {
    printf("\n[TEST] Chunked transfer encoding detection\n");

    char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Content-Type: application/json\r\n"
        "\r\n"
        "1a\r\n"
        "{\"chunked\":\"response\"}\r\n"
        "0\r\n"
        "\r\n";

    char value_buffer[128];
    int result = http_get_header(response, "Transfer-Encoding", value_buffer, sizeof(value_buffer));
    TEST_ASSERT(result == 0, "Transfer-Encoding header found");
    TEST_ASSERT(strstr(value_buffer, "chunked") != NULL, "Chunked encoding detected");
}

int main() {
    printf("========================================\n");
    printf("  HTTP Client Unit Tests\n");
    printf("========================================\n");

    test_build_get_request();
    test_build_post_request();
    test_build_patch_request();
    test_parse_200_response();
    test_parse_error_response();
    test_buffer_overflow_protection();
    test_header_extraction_edge_cases();
    test_chunked_response();

    printf("\n========================================\n");
    printf("  Test Results\n");
    printf("========================================\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed == 0 ? 0 : 1;
}
