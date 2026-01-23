#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

// Minimal TLS configuration for RP2040's memory constraints
// This enables only essential features for TLS 1.2 client with RSA

// System support
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_MEMORY_BUFFER_ALLOC_C

// Enable platform-specific memory allocation
#define MBEDTLS_PLATFORM_CALLOC_MACRO   calloc
#define MBEDTLS_PLATFORM_FREE_MACRO     free

// Core cryptographic features
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_CIPHER_PADDING_PKCS7

// Symmetric ciphers - only AES
#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_C

// Asymmetric cryptography - RSA and ECC (needed for K3s ECDSA certs)
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15          // RSA PKCS#1 v1.5
#define MBEDTLS_PKCS1_V21          // RSA PKCS#1 v2.1 (PSS)

// Elliptic Curve Cryptography (required for ECDSA certificates)
#define MBEDTLS_ECP_C              // Elliptic curve point operations
#define MBEDTLS_ECDSA_C            // ECDSA signature verification
#define MBEDTLS_ECDH_C             // ECDH key exchange
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED  // P-256 curve (most common)

// Hash functions
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA1_C             // Still needed by some CAs
#define MBEDTLS_MD_C

// TLS protocol support
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C          // TLS client only (no server for API calls)
#define MBEDTLS_SSL_PROTO_TLS1_2   // TLS 1.2 only

// TLS features
#define MBEDTLS_SSL_MAX_CONTENT_LEN    8192  // Smaller TLS record size (default 16KB)
#define MBEDTLS_SSL_IN_CONTENT_LEN     MBEDTLS_SSL_MAX_CONTENT_LEN
#define MBEDTLS_SSL_OUT_CONTENT_LEN    MBEDTLS_SSL_MAX_CONTENT_LEN

// X.509 certificate support
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_PEM_PARSE_C        // Parse PEM format certificates
#define MBEDTLS_BASE64_C           // Needed for PEM parsing
#define MBEDTLS_OID_C              // Object Identifier support
#define MBEDTLS_ASN1_PARSE_C       // ASN.1 parsing
#define MBEDTLS_ASN1_WRITE_C       // ASN.1 writing
#define MBEDTLS_PK_PARSE_C         // Parse private keys
#define MBEDTLS_PK_C               // Public key abstraction layer
#define MBEDTLS_PK_WRITE_C

// Big number arithmetic (required for RSA)
#define MBEDTLS_BIGNUM_C

// Entropy and random number generation
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_NO_PLATFORM_ENTROPY     // No built-in platform entropy
#define MBEDTLS_ENTROPY_HARDWARE_ALT    // Use custom mbedtls_hardware_poll()

// Disable unnecessary features to save memory
#undef MBEDTLS_SSL_PROTO_TLS1_3    // No TLS 1.3
#undef MBEDTLS_SSL_SRV_C           // No TLS server for outbound connections
#undef MBEDTLS_SSL_PROTO_DTLS      // No DTLS
#define MBEDTLS_GCM_C               // Enable GCM mode (required for ECDHE-GCM ciphers)
#undef MBEDTLS_CCM_C               // No CCM mode
// Note: ECC and ECDHE now enabled above for K3s ECDSA certificate support
// #undef MBEDTLS_ECP_C               // Re-enabled for ECDSA
// #undef MBEDTLS_ECDH_C              // Re-enabled for ECDSA
// #undef MBEDTLS_ECDSA_C             // Re-enabled for ECDSA
#undef MBEDTLS_DHM_C               // No Diffie-Hellman
// #undef MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED    // Re-enabled
// #undef MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED  // Re-enabled
#undef MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_ECDH_RSA_ENABLED

// Enable RSA and ECDHE key exchange (ECDHE required for modern servers)
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

// Cipher suites - try CBC first to debug GCM issues
#define MBEDTLS_SSL_CIPHERSUITES                        \
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256, \
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, \
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,  \
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,  \
        MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA256

// Enable Extended Master Secret (RFC 7627) - required by modern TLS servers like Go
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET

// Disable additional features
#undef MBEDTLS_SSL_RENEGOTIATION
#undef MBEDTLS_SSL_MAX_FRAGMENT_LENGTH
#undef MBEDTLS_SSL_SESSION_TICKETS
#undef MBEDTLS_SSL_EXPORT_KEYS
#undef MBEDTLS_SSL_TRUNCATED_HMAC
#undef MBEDTLS_SSL_SERVER_NAME_INDICATION  // Disable SNI (causes issues with Go TLS servers)

// Enable debug output for troubleshooting
#define MBEDTLS_DEBUG_C

// Disable testing and development features
#undef MBEDTLS_SSL_RECORD_CHECKING
#undef MBEDTLS_SELF_TEST
#undef MBEDTLS_VERSION_FEATURES

// Enable certificate verification
#define MBEDTLS_X509_CHECK_KEY_USAGE
#define MBEDTLS_X509_CHECK_EXTENDED_KEY_USAGE

// SSL server support (for kubelet server)
#define MBEDTLS_SSL_SRV_C

// Enable both client and server support
// (client for API calls, server for kubelet endpoints)
#define MBEDTLS_SSL_COOKIE_C

#endif /* MBEDTLS_CONFIG_H */
