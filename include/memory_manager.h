#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <stdint.h>

/**
 * Memory Manager
 *
 * Manages a designated SRAM region that can be updated
 * via Kubernetes ConfigMaps
 */

/**
 * Initialize the memory manager
 * Zeroes out the configurable memory region
 */
void memory_manager_init(void);

/**
 * Update memory from a string representation
 * Format: "offset=value,offset=value,..."
 * Example: "0=0x42,1=0x43,10=0xFF"
 * @param updates String containing memory updates
 */
void memory_manager_update_from_string(const char *updates);

/**
 * Write a single byte to the memory region
 * @param offset Offset within the memory region
 * @param value Byte value to write
 * @return 0 on success, -1 if offset out of bounds
 */
int memory_manager_write_byte(uint32_t offset, uint8_t value);

/**
 * Read a single byte from the memory region
 * @param offset Offset within the memory region
 * @param value Pointer to store the read value
 * @return 0 on success, -1 if offset out of bounds
 */
int memory_manager_read_byte(uint32_t offset, uint8_t *value);

/**
 * Dump memory contents to console (for debugging)
 */
void memory_manager_dump(void);

/**
 * Get pointer to the memory region
 * @return Pointer to start of memory region
 */
uint8_t *memory_manager_get_region(void);

#endif // MEMORY_MANAGER_H
