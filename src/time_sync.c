#include "time_sync.h"
#include "config.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// Time reference
static struct {
    uint64_t base_unix_time;      // Unix timestamp when reference was set
    uint64_t base_boot_ms;        // Boot milliseconds when reference was set
    bool is_synced;               // Whether we have a valid time reference
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
// Simple implementation - doesn't handle all edge cases but sufficient for our needs
static uint64_t datetime_to_unix(int year, int month, int day, int hour, int min, int sec) {
    // Days since Unix epoch (Jan 1, 1970)
    // Simple algorithm - good enough for dates 2000-2100

    // Days per month (non-leap year)
    static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // Calculate days since epoch
    int days = 0;

    // Years
    for (int y = 1970; y < year; y++) {
        days += 365;
        // Leap year check
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) {
            days++;
        }
    }

    // Months in current year
    for (int m = 1; m < month; m++) {
        days += days_in_month[m - 1];
        // Add leap day if February and leap year
        if (m == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
            days++;
        }
    }

    // Days in current month
    days += (day - 1);

    // Convert to seconds
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
    DEBUG_PRINT("Time sync initialized (not synced)");
}

int time_sync_update_from_header(const char *date_header) {
    if (date_header == NULL) {
        return -1;
    }

    // Parse RFC 1123 format: "Fri, 23 Jan 2026 16:30:45 GMT"
    // Format: "DDD, DD MMM YYYY HH:MM:SS GMT"

    char day_name[4];
    int day, year, hour, min, sec;
    char month_str[4];
    char tz[4];

    int parsed = sscanf(date_header, "%3s, %d %3s %d %d:%d:%d %3s",
                       day_name, &day, month_str, &year,
                       &hour, &min, &sec, tz);

    if (parsed != 8) {
        DEBUG_PRINT("Failed to parse Date header: %s", date_header);
        return -1;
    }

    // Convert month name to number
    int month = parse_month(month_str);
    if (month < 0) {
        DEBUG_PRINT("Invalid month in Date header: %s", month_str);
        return -1;
    }

    // Validate ranges
    if (year < 2020 || year > 2100 || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour < 0 || hour > 23 ||
        min < 0 || min > 59 || sec < 0 || sec > 59) {
        DEBUG_PRINT("Date values out of range");
        return -1;
    }

    // Convert to Unix timestamp
    uint64_t unix_time = datetime_to_unix(year, month, day, hour, min, sec);

    // Store time reference
    time_ref.base_unix_time = unix_time;
    time_ref.base_boot_ms = to_ms_since_boot(get_absolute_time());
    time_ref.is_synced = true;

    DEBUG_PRINT("Time synced: %04d-%02d-%02d %02d:%02d:%02d UTC (unix: %llu)",
                year, month, day, hour, min, sec, unix_time);

    return 0;
}

bool time_sync_is_synced(void) {
    return time_ref.is_synced;
}

uint64_t time_sync_get_unix_time(void) {
    if (!time_ref.is_synced) {
        return 0;
    }

    // Calculate elapsed time since sync
    uint64_t current_boot_ms = to_ms_since_boot(get_absolute_time());
    uint64_t elapsed_ms = current_boot_ms - time_ref.base_boot_ms;
    uint64_t elapsed_sec = elapsed_ms / 1000;

    return time_ref.base_unix_time + elapsed_sec;
}

int time_sync_get_iso8601(char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size < 21) {
        return -1;
    }

    if (!time_ref.is_synced) {
        // Not synced - return a zero timestamp as placeholder
        snprintf(buffer, buffer_size, "1970-01-01T00:00:00Z");
        return -1;
    }

    uint64_t unix_time = time_sync_get_unix_time();

    // Convert Unix timestamp to date/time components
    // Simple algorithm - good enough for our needs

    uint64_t days_since_epoch = unix_time / 86400;
    uint64_t seconds_today = unix_time % 86400;

    int hour = seconds_today / 3600;
    int min = (seconds_today % 3600) / 60;
    int sec = seconds_today % 60;

    // Calculate year, month, day from days since epoch
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

    // Calculate month and day
    static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int month = 1;

    for (int m = 0; m < 12; m++) {
        int days_this_month = days_in_month[m];

        // Adjust for leap year February
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

    // Format as ISO 8601: "2026-01-23T16:30:45Z"
    snprintf(buffer, buffer_size, "%04d-%02d-%02dT%02d:%02d:%02dZ",
            year, month, day, hour, min, sec);

    return 0;
}
