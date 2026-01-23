#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Time Synchronization Module
 *
 * Since the RP2040 has no RTC, this module synchronizes time from HTTP Date headers
 * received from the k8s API server. It maintains a time reference that can be used
 * to generate timestamps for status updates.
 *
 * Strategy:
 * - Parse Date header from HTTP responses (RFC 1123 format)
 * - Store base timestamp + boot milliseconds when received
 * - Calculate current time = base + (current_boot_ms - base_boot_ms)
 * - Resync on every HTTP response to prevent drift
 */

/**
 * Initialize time sync module
 */
void time_sync_init(void);

/**
 * Update time reference from HTTP Date header
 *
 * Parses RFC 1123 date format: "Fri, 23 Jan 2026 16:30:45 GMT"
 *
 * @param date_header The Date header value from HTTP response
 * @return 0 on success, -1 on parse error
 */
int time_sync_update_from_header(const char *date_header);

/**
 * Check if time has been synchronized
 *
 * @return true if we have a valid time reference, false otherwise
 */
bool time_sync_is_synced(void);

/**
 * Get current timestamp in ISO 8601 format
 *
 * Format: "2026-01-23T16:30:45Z"
 *
 * @param buffer Buffer to write timestamp string
 * @param buffer_size Size of buffer (should be at least 21 bytes)
 * @return 0 on success, -1 if not synced or buffer too small
 */
int time_sync_get_iso8601(char *buffer, size_t buffer_size);

/**
 * Get Unix timestamp (seconds since epoch)
 *
 * @return Unix timestamp, or 0 if not synced
 */
uint64_t time_sync_get_unix_time(void);

#endif // TIME_SYNC_H
