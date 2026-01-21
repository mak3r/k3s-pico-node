#include "memory_manager.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Designated memory region - allocated in SRAM
static uint8_t memory_region[MEMORY_REGION_SIZE] __attribute__((aligned(4)));

void memory_manager_init(void) {
    // Initialize memory region (zero it out)
    memset(memory_region, 0, MEMORY_REGION_SIZE);

    DEBUG_PRINT("Memory manager initialized");
    DEBUG_PRINT("  Region: 0x%p - 0x%p",
               memory_region,
               memory_region + MEMORY_REGION_SIZE - 1);
    DEBUG_PRINT("  Size: %d bytes", MEMORY_REGION_SIZE);
}

void memory_manager_update_from_string(const char *updates) {
    if (updates == NULL || strlen(updates) == 0) {
        DEBUG_PRINT("Empty memory update string");
        return;
    }

    DEBUG_PRINT("Processing memory updates: %s", updates);

    // Make a copy since strtok modifies the string
    char *copy = strdup(updates);
    if (copy == NULL) {
        printf("ERROR: Failed to allocate memory for parsing\n");
        return;
    }

    char *token = strtok(copy, ",");
    int update_count = 0;

    while (token != NULL) {
        unsigned int offset;
        unsigned int value;

        // Parse format: "offset=value" where value is hex (0xFF) or decimal
        if (sscanf(token, "%u=0x%x", &offset, &value) == 2 ||
            sscanf(token, "%u=%x", &offset, &value) == 2 ||
            sscanf(token, "%u=%u", &offset, &value) == 2) {

            if (memory_manager_write_byte(offset, (uint8_t)value) == 0) {
                DEBUG_PRINT("  Memory[%u] = 0x%02X", offset, value);
                update_count++;
            }
        } else {
            DEBUG_PRINT("  Skipping invalid token: %s", token);
        }

        token = strtok(NULL, ",");
    }

    free(copy);
    printf("Memory manager: Applied %d updates\n", update_count);
}

int memory_manager_write_byte(uint32_t offset, uint8_t value) {
    if (offset >= MEMORY_REGION_SIZE) {
        DEBUG_PRINT("ERROR: Write offset %u out of bounds (max %u)",
                   offset, MEMORY_REGION_SIZE - 1);
        return -1;
    }

    memory_region[offset] = value;
    return 0;
}

int memory_manager_read_byte(uint32_t offset, uint8_t *value) {
    if (offset >= MEMORY_REGION_SIZE) {
        DEBUG_PRINT("ERROR: Read offset %u out of bounds (max %u)",
                   offset, MEMORY_REGION_SIZE - 1);
        return -1;
    }

    if (value == NULL) {
        return -1;
    }

    *value = memory_region[offset];
    return 0;
}

void memory_manager_dump(void) {
    printf("\n=== Memory Region Dump ===\n");
    printf("Address: 0x%p\n", memory_region);
    printf("Size: %d bytes\n\n", MEMORY_REGION_SIZE);

    for (uint32_t i = 0; i < MEMORY_REGION_SIZE; i += 16) {
        printf("%04x: ", i);

        // Print hex values
        for (uint32_t j = 0; j < 16 && i + j < MEMORY_REGION_SIZE; j++) {
            printf("%02x ", memory_region[i + j]);
        }

        // Pad if last line is incomplete
        for (uint32_t j = (i + 16 > MEMORY_REGION_SIZE) ?
                          (MEMORY_REGION_SIZE - i) : 16;
             j < 16; j++) {
            printf("   ");
        }

        printf(" | ");

        // Print ASCII representation
        for (uint32_t j = 0; j < 16 && i + j < MEMORY_REGION_SIZE; j++) {
            uint8_t byte = memory_region[i + j];
            printf("%c", (byte >= 32 && byte <= 126) ? byte : '.');
        }

        printf("\n");
    }

    printf("========================\n\n");
}

uint8_t *memory_manager_get_region(void) {
    return memory_region;
}
