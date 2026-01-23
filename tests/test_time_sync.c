/**
 * Unit tests for time synchronization module
 *
 * Tests RFC 1123 date parsing, Unix timestamp conversion,
 * ISO 8601 formatting, and time calculations.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

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

// Mock functions to simulate Pico SDK time functions
static uint64_t mock_boot_ms = 0;

// Time reference
static struct {
    uint64_t base_unix_time;
    uint64_t base_boot_ms;
    bool is_synced;
} time_ref = {0};

// Month name to number mapping
static const char *month_names[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

// Convert month name to number (1-12)
static int parse_month(const char *month_str) {
    for (int i = 0; i < 12; i++) {
        if (strncmp(month_str, month_names[i], 3) == 0) {
            return i + 1;
        }
    }
    return -1;
}

// Convert date/time to Unix timestamp
static uint64_t datetime_to_unix(int year, int month, int day, int hour, int min, int sec) {
    static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    int days = 0;
    for (int y = 1970; y < year; y++) {
        days += 365;
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) {
            days++;
        }
    }

    for (int m = 1; m < month; m++) {
        days += days_in_month[m - 1];
        if (m == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
            days++;
        }
    }

    days += (day - 1);

    uint64_t seconds = (uint64_t)days * 86400ULL;
    seconds += hour * 3600ULL;
    seconds += min * 60ULL;
    seconds += sec;

    return seconds;
}

void time_sync_init(void) {
    time_ref.base_unix_time = 0;
    time_ref.base_boot_ms = 0;
    time_ref.is_synced = false;
}

int time_sync_update_from_header(const char *date_header) {
    if (date_header == NULL) {
        return -1;
    }

    char day_name[4];
    int day, year, hour, min, sec;
    char month_str[4];
    char tz[4];

    int parsed = sscanf(date_header, "%3s, %d %3s %d %d:%d:%d %3s",
                       day_name, &day, month_str, &year,
                       &hour, &min, &sec, tz);

    if (parsed != 8) {
        return -1;
    }

    int month = parse_month(month_str);
    if (month < 0) {
        return -1;
    }

    if (year < 2020 || year > 2100 || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour < 0 || hour > 23 ||
        min < 0 || min > 59 || sec < 0 || sec > 59) {
        return -1;
    }

    uint64_t unix_time = datetime_to_unix(year, month, day, hour, min, sec);

    time_ref.base_unix_time = unix_time;
    time_ref.base_boot_ms = mock_boot_ms;
    time_ref.is_synced = true;

    return 0;
}

bool time_sync_is_synced(void) {
    return time_ref.is_synced;
}

uint64_t time_sync_get_unix_time(void) {
    if (!time_ref.is_synced) {
        return 0;
    }

    uint64_t elapsed_ms = mock_boot_ms - time_ref.base_boot_ms;
    uint64_t elapsed_sec = elapsed_ms / 1000;

    return time_ref.base_unix_time + elapsed_sec;
}

int time_sync_get_iso8601(char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size < 21) {
        return -1;
    }

    if (!time_ref.is_synced) {
        snprintf(buffer, buffer_size, "1970-01-01T00:00:00Z");
        return -1;
    }

    uint64_t unix_time = time_sync_get_unix_time();

    uint64_t days_since_epoch = unix_time / 86400;
    uint64_t seconds_today = unix_time % 86400;

    int hour = seconds_today / 3600;
    int min = (seconds_today % 3600) / 60;
    int sec = seconds_today % 60;

    int year = 1970;
    int days_remaining = days_since_epoch;

    while (true) {
        int days_in_year = 365;
        if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
            days_in_year = 366;
        }

        if (days_remaining < days_in_year) {
            break;
        }

        days_remaining -= days_in_year;
        year++;
    }

    static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int month = 1;

    for (int m = 0; m < 12; m++) {
        int days_this_month = days_in_month[m];

        if (m == 1 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
            days_this_month = 29;
        }

        if (days_remaining < days_this_month) {
            month = m + 1;
            break;
        }

        days_remaining -= days_this_month;
    }

    int day = days_remaining + 1;

    snprintf(buffer, buffer_size, "%04d-%02d-%02dT%02d:%02d:%02dZ",
            year, month, day, hour, min, sec);

    return 0;
}

void test_rfc1123_parsing(void) {
    printf("\n[TEST] RFC 1123 Date Header Parsing\n");

    // Test valid date
    const char *valid_date = "Fri, 23 Jan 2026 16:30:45 GMT";
    int result = time_sync_update_from_header(valid_date);
    TEST_ASSERT(result == 0, "Parse valid RFC 1123 date");
    TEST_ASSERT(time_sync_is_synced(), "Time marked as synced");

    // Test various valid dates
    const char *dates[] = {
        "Mon, 01 Jan 2024 00:00:00 GMT",
        "Sun, 31 Dec 2023 23:59:59 GMT",
        "Wed, 15 Mar 2025 12:30:15 GMT",
        "Sat, 29 Feb 2024 18:45:30 GMT",  // Leap year
    };

    for (int i = 0; i < 4; i++) {
        result = time_sync_update_from_header(dates[i]);
        char msg[128];
        snprintf(msg, sizeof(msg), "Parse date: %s", dates[i]);
        TEST_ASSERT(result == 0, msg);
    }

    // Test invalid dates
    const char *invalid_dates[] = {
        "Not a date",
        "2026-01-23T16:30:45Z",  // Wrong format (ISO 8601)
        "Fri, 32 Jan 2026 16:30:45 GMT",  // Invalid day
        "Fri, 23 Xxx 2026 16:30:45 GMT",  // Invalid month
        "Fri, 23 Jan 2026 25:00:00 GMT",  // Invalid hour
    };

    for (int i = 0; i < 5; i++) {
        result = time_sync_update_from_header(invalid_dates[i]);
        char msg[128];
        snprintf(msg, sizeof(msg), "Reject invalid date: %.30s...", invalid_dates[i]);
        TEST_ASSERT(result != 0, msg);
    }
}

void test_unix_timestamp_conversion(void) {
    printf("\n[TEST] Unix Timestamp Conversion\n");

    // Known Unix timestamps
    struct {
        const char *date;
        uint64_t expected_unix;
    } test_cases[] = {
        {"Thu, 01 Jan 1970 00:00:00 GMT", 0},           // Epoch
        {"Fri, 01 Jan 2021 00:00:00 GMT", 1609459200},  // 2021 start
        {"Sat, 01 Jan 2022 00:00:00 GMT", 1640995200},  // 2022 start
        {"Sun, 01 Jan 2023 00:00:00 GMT", 1672531200},  // 2023 start
        {"Mon, 01 Jan 2024 00:00:00 GMT", 1704067200},  // 2024 start (leap year)
    };

    for (int i = 0; i < 5; i++) {
        mock_boot_ms = 0;  // Reset boot time
        time_sync_update_from_header(test_cases[i].date);
        uint64_t unix_time = time_sync_get_unix_time();

        char msg[128];
        snprintf(msg, sizeof(msg), "Convert %s to unix %llu",
                 test_cases[i].date, (unsigned long long)test_cases[i].expected_unix);
        TEST_ASSERT(unix_time == test_cases[i].expected_unix, msg);
    }
}

void test_iso8601_formatting(void) {
    printf("\n[TEST] ISO 8601 Timestamp Formatting\n");

    char timestamp[32];

    // Test with known date
    mock_boot_ms = 0;
    time_sync_update_from_header("Fri, 23 Jan 2026 16:30:45 GMT");

    int result = time_sync_get_iso8601(timestamp, sizeof(timestamp));
    TEST_ASSERT(result == 0, "Generate ISO 8601 timestamp");
    TEST_ASSERT(strcmp(timestamp, "2026-01-23T16:30:45Z") == 0,
                "Correct ISO 8601 format");

    // Test various dates
    struct {
        const char *rfc1123;
        const char *iso8601;
    } formats[] = {
        {"Mon, 01 Jan 2024 00:00:00 GMT", "2024-01-01T00:00:00Z"},
        {"Sun, 31 Dec 2023 23:59:59 GMT", "2023-12-31T23:59:59Z"},
        {"Wed, 15 Mar 2025 12:30:15 GMT", "2025-03-15T12:30:15Z"},
    };

    for (int i = 0; i < 3; i++) {
        mock_boot_ms = 0;
        time_sync_update_from_header(formats[i].rfc1123);
        time_sync_get_iso8601(timestamp, sizeof(timestamp));

        char msg[128];
        snprintf(msg, sizeof(msg), "Format %s correctly", formats[i].rfc1123);
        TEST_ASSERT(strcmp(timestamp, formats[i].iso8601) == 0, msg);
    }

    // Test buffer size validation
    char small_buffer[10];
    result = time_sync_get_iso8601(small_buffer, sizeof(small_buffer));
    TEST_ASSERT(result == -1, "Reject buffer too small");

    // Test NULL buffer
    result = time_sync_get_iso8601(NULL, 32);
    TEST_ASSERT(result == -1, "Reject NULL buffer");
}

void test_time_progression(void) {
    printf("\n[TEST] Time Progression\n");

    // Sync at boot time 0
    mock_boot_ms = 0;
    time_sync_update_from_header("Fri, 23 Jan 2026 16:30:00 GMT");

    uint64_t base_time = time_sync_get_unix_time();
    TEST_ASSERT(base_time > 0, "Initial time set");

    // Advance boot time by 10 seconds (10000 ms)
    mock_boot_ms = 10000;
    uint64_t new_time = time_sync_get_unix_time();
    TEST_ASSERT(new_time == base_time + 10, "Time advances by 10 seconds");

    // Advance by 1 minute
    mock_boot_ms = 60000;
    new_time = time_sync_get_unix_time();
    TEST_ASSERT(new_time == base_time + 60, "Time advances by 60 seconds");

    // Test ISO 8601 time progression
    char timestamp1[32], timestamp2[32];

    mock_boot_ms = 0;
    time_sync_update_from_header("Fri, 23 Jan 2026 16:30:00 GMT");
    time_sync_get_iso8601(timestamp1, sizeof(timestamp1));

    mock_boot_ms = 5000;  // +5 seconds
    time_sync_get_iso8601(timestamp2, sizeof(timestamp2));

    TEST_ASSERT(strcmp(timestamp1, "2026-01-23T16:30:00Z") == 0,
                "Initial timestamp correct");
    TEST_ASSERT(strcmp(timestamp2, "2026-01-23T16:30:05Z") == 0,
                "Timestamp advances by 5 seconds");
}

void test_resync(void) {
    printf("\n[TEST] Time Resynchronization\n");

    // Initial sync
    mock_boot_ms = 0;
    time_sync_update_from_header("Fri, 23 Jan 2026 16:30:00 GMT");
    uint64_t time1 = time_sync_get_unix_time();

    // Simulate boot time advancing
    mock_boot_ms = 5000;
    uint64_t time2 = time_sync_get_unix_time();
    TEST_ASSERT(time2 == time1 + 5, "Time advances with boot time");

    // Resync with new time (simulating next HTTP response)
    // Boot time is at 5000ms, server time is 16:30:10 (10 seconds after initial sync)
    time_sync_update_from_header("Fri, 23 Jan 2026 16:30:10 GMT");
    uint64_t time3 = time_sync_get_unix_time();

    // After resync, time should be server time (16:30:10)
    TEST_ASSERT(time3 == time1 + 10, "Time resyncs to server time");

    // Boot time stays at 5000ms, but base time updated
    uint64_t time4 = time_sync_get_unix_time();
    TEST_ASSERT(time4 == time3, "Time stable after resync");

    // Advance boot time further
    mock_boot_ms = 10000;  // +5 more seconds
    uint64_t time5 = time_sync_get_unix_time();
    TEST_ASSERT(time5 == time3 + 5, "Time continues from new base");
}

void test_not_synced_state(void) {
    printf("\n[TEST] Not Synced State\n");

    // Reset to unsynced state
    time_sync_init();

    TEST_ASSERT(!time_sync_is_synced(), "Initially not synced");
    TEST_ASSERT(time_sync_get_unix_time() == 0, "Returns 0 when not synced");

    char timestamp[32];
    int result = time_sync_get_iso8601(timestamp, sizeof(timestamp));
    TEST_ASSERT(result == -1, "ISO 8601 fails when not synced");
    TEST_ASSERT(strcmp(timestamp, "1970-01-01T00:00:00Z") == 0,
                "Returns epoch placeholder");
}

void test_leap_year_handling(void) {
    printf("\n[TEST] Leap Year Handling\n");

    // 2024 is a leap year
    mock_boot_ms = 0;
    time_sync_update_from_header("Thu, 29 Feb 2024 12:00:00 GMT");

    char timestamp[32];
    time_sync_get_iso8601(timestamp, sizeof(timestamp));
    TEST_ASSERT(strcmp(timestamp, "2024-02-29T12:00:00Z") == 0,
                "Handles leap day correctly");

    // 2023 is not a leap year (would have rejected Feb 29 in parsing)
    mock_boot_ms = 0;
    time_sync_update_from_header("Wed, 01 Mar 2023 00:00:00 GMT");
    time_sync_get_iso8601(timestamp, sizeof(timestamp));
    TEST_ASSERT(strcmp(timestamp, "2023-03-01T00:00:00Z") == 0,
                "Handles non-leap year correctly");

    // Test day after leap day
    mock_boot_ms = 0;
    time_sync_update_from_header("Fri, 01 Mar 2024 00:00:00 GMT");
    time_sync_get_iso8601(timestamp, sizeof(timestamp));
    TEST_ASSERT(strcmp(timestamp, "2024-03-01T00:00:00Z") == 0,
                "Handles day after leap day");
}

void test_edge_cases(void) {
    printf("\n[TEST] Edge Cases\n");

    char timestamp[32];

    // End of month boundaries
    mock_boot_ms = 0;
    time_sync_update_from_header("Sat, 31 Jan 2026 23:59:59 GMT");
    time_sync_get_iso8601(timestamp, sizeof(timestamp));
    TEST_ASSERT(strcmp(timestamp, "2026-01-31T23:59:59Z") == 0,
                "End of January");

    // Advance 1 second to next month
    mock_boot_ms = 1000;
    time_sync_get_iso8601(timestamp, sizeof(timestamp));
    TEST_ASSERT(strcmp(timestamp, "2026-02-01T00:00:00Z") == 0,
                "Rolls over to February");

    // End of year
    mock_boot_ms = 0;
    time_sync_update_from_header("Wed, 31 Dec 2025 23:59:59 GMT");
    time_sync_get_iso8601(timestamp, sizeof(timestamp));
    TEST_ASSERT(strcmp(timestamp, "2025-12-31T23:59:59Z") == 0,
                "End of year");

    // Advance 1 second to next year
    mock_boot_ms = 1000;
    time_sync_get_iso8601(timestamp, sizeof(timestamp));
    TEST_ASSERT(strcmp(timestamp, "2026-01-01T00:00:00Z") == 0,
                "Rolls over to new year");

    // Midnight
    mock_boot_ms = 0;
    time_sync_update_from_header("Thu, 15 May 2025 00:00:00 GMT");
    time_sync_get_iso8601(timestamp, sizeof(timestamp));
    TEST_ASSERT(strcmp(timestamp, "2025-05-15T00:00:00Z") == 0,
                "Midnight time");

    // Just before midnight
    mock_boot_ms = 0;
    time_sync_update_from_header("Thu, 15 May 2025 23:59:59 GMT");
    time_sync_get_iso8601(timestamp, sizeof(timestamp));
    TEST_ASSERT(strcmp(timestamp, "2025-05-15T23:59:59Z") == 0,
                "Just before midnight");
}

int main() {
    printf("========================================\n");
    printf("  Time Sync Module Unit Tests\n");
    printf("========================================\n");

    test_rfc1123_parsing();
    test_unix_timestamp_conversion();
    test_iso8601_formatting();
    test_time_progression();
    test_resync();
    test_not_synced_state();
    test_leap_year_handling();
    test_edge_cases();

    printf("\n========================================\n");
    printf("  Test Results\n");
    printf("========================================\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed == 0 ? 0 : 1;
}
