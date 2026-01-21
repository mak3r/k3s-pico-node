#include "k3s_client.h"
#include "config.h"
#include "certs.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include <stdio.h>
#include <string.h>

// TLS context for API server connection
static mbedtls_ssl_context ssl;
static mbedtls_ssl_config conf;
static mbedtls_x509_crt ca_cert;
static mbedtls_x509_crt client_cert;
static mbedtls_pk_context client_key;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;

// Connection state
static bool tls_initialized = false;

// Note: mbedtls_hardware_poll() is provided by pico_mbedtls library
// It uses the Pico's hardware RNG automatically

int k3s_client_init(void) {
    int ret;

    DEBUG_PRINT("Initializing k3s API client...");

    // Initialize mbedtls structures
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&ca_cert);
    mbedtls_x509_crt_init(&client_cert);
    mbedtls_pk_init(&client_key);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    DEBUG_PRINT("Using hardware RNG via mbedtls_hardware_poll()");

    // Seed the random number generator
    const char *pers = "k3s_pico_client";
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        printf("ERROR: mbedtls_ctr_drbg_seed failed: -0x%04x\n", -ret);
        return -1;
    }
    DEBUG_PRINT("Random number generator seeded successfully");

    // Load CA certificate (for verifying API server)
    size_t cert_len = strlen(server_ca_cert);
    DEBUG_PRINT("Server CA cert length: %d bytes", cert_len);
    DEBUG_PRINT("First 40 chars: %.40s", server_ca_cert);

    ret = mbedtls_x509_crt_parse(&ca_cert,
                                 (const unsigned char *)server_ca_cert,
                                 cert_len + 1);
    if (ret != 0) {
        printf("ERROR: Failed to parse server CA cert: -0x%04x (%d)\n", -ret, ret);
        printf("  Certificate length: %zu\n", cert_len);
        printf("  Last 40 chars: %.40s\n", &server_ca_cert[cert_len - 40]);
        return -1;
    }
    DEBUG_PRINT("Server CA certificate loaded");

    // Load client certificate (for authenticating to API server)
    ret = mbedtls_x509_crt_parse(&client_cert,
                                 (const unsigned char *)client_kubelet_cert,
                                 strlen(client_kubelet_cert) + 1);
    if (ret != 0) {
        printf("ERROR: Failed to parse client cert: -0x%04x\n", -ret);
        return -1;
    }
    DEBUG_PRINT("Client certificate loaded");

    // Load client private key
    ret = mbedtls_pk_parse_key(&client_key,
                               (const unsigned char *)client_kubelet_key,
                               strlen(client_kubelet_key) + 1,
                               NULL, 0,
                               mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        printf("ERROR: Failed to parse client key: -0x%04x\n", -ret);
        return -1;
    }
    DEBUG_PRINT("Client private key loaded");

    // Configure TLS for client mode
    ret = mbedtls_ssl_config_defaults(&conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        printf("ERROR: mbedtls_ssl_config_defaults failed: -0x%04x\n", -ret);
        return -1;
    }

    // Set CA chain for server verification
    mbedtls_ssl_conf_ca_chain(&conf, &ca_cert, NULL);

    // Set client certificate and key for mutual TLS
    ret = mbedtls_ssl_conf_own_cert(&conf, &client_cert, &client_key);
    if (ret != 0) {
        printf("ERROR: mbedtls_ssl_conf_own_cert failed: -0x%04x\n", -ret);
        return -1;
    }

    // Set RNG
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    // Require certificate verification
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);

    tls_initialized = true;
    DEBUG_PRINT("K3s client initialized successfully");

    return 0;
}

// Helper function to send HTTP request and receive response
// This is a simplified version - full implementation would need proper
// TLS integration with lwIP raw API
static int k3s_request(const char *method, const char *path, const char *body,
                      char *response, int response_size) {
    if (!tls_initialized) {
        printf("ERROR: K3s client not initialized\n");
        return -1;
    }

    DEBUG_PRINT("K3s %s request: %s", method, path);

    // NOTE: This is a placeholder for the actual TLS connection code
    // A full implementation requires:
    // 1. Create TCP connection using lwIP
    // 2. Set up mbedtls_ssl_set_bio() with custom send/recv functions
    // 3. Perform TLS handshake
    // 4. Send HTTP request
    // 5. Receive and parse HTTP response
    // 6. Close connection
    //
    // This is complex and requires careful integration of mbedtls with lwIP's
    // raw API. See pico-examples for reference implementations.

    printf("WARNING: k3s_request() is not fully implemented yet\n");
    printf("  This requires complete TLS+lwIP integration\n");

    // For now, return error to allow compilation
    // TODO: Implement full TLS connection handling
    return -1;
}

int k3s_client_get(const char *path, char *response, int response_size) {
    if (response == NULL || response_size <= 0) {
        return -1;
    }

    return k3s_request("GET", path, NULL, response, response_size);
}

int k3s_client_post(const char *path, const char *body) {
    if (body == NULL) {
        return -1;
    }

    return k3s_request("POST", path, body, NULL, 0);
}

int k3s_client_patch(const char *path, const char *body) {
    if (body == NULL) {
        return -1;
    }

    // For PATCH requests, we need to set Content-Type: application/strategic-merge-patch+json
    // or application/merge-patch+json
    return k3s_request("PATCH", path, body, NULL, 0);
}

void k3s_client_shutdown(void) {
    if (!tls_initialized) {
        return;
    }

    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&ca_cert);
    mbedtls_x509_crt_free(&client_cert);
    mbedtls_pk_free(&client_key);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);

    tls_initialized = false;
    DEBUG_PRINT("K3s client shutdown");
}
