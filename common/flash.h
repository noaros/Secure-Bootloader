/*
 * Internal flash access. The target implementation lives in flash_stm32.c;
 * host unit tests swap in test/fake_flash.c.
 */
#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

#define FLASH_OK         0
#define FLASH_ERR_RANGE  (-1)  /* address not allowed / not aligned */
#define FLASH_ERR_HW     (-2)  /* controller reported an error */
#define FLASH_ERR_VERIFY (-3)  /* read-back mismatch */

/* Map a flash address to a readable pointer (identity on target). */
const uint8_t *flash_map(uint32_t addr);

/* Sector geometry. Returns sector number (0..23) or -1. */
int flash_sector_of(uint32_t addr, uint32_t *base, uint32_t *size);

/*
 * Erase every sector overlapping [addr, addr+len). Refuses anything inside the
 * bootloader region. `tick` (may be NULL) is called between sectors so the
 * caller can kick the watchdog.
 */
int flash_erase_range(uint32_t addr, uint32_t len, void (*tick)(void));

/* Program `len` bytes (multiple of 4, addr 4-aligned) into erased flash. */
int flash_program(uint32_t addr, const void *data, uint32_t len);

/* True if `len` bytes at addr are all 0xFF. */
int flash_is_blank(uint32_t addr, uint32_t len);

#endif
