/* In-memory model of the F429 flash with real NOR semantics (program only erased words). */
#include "fake_flash.h"
#include "flash.h"
#include "layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t mem[FLASH_TOTAL_SIZE];
static long fail_budget = -1;

void fake_flash_reset(void)
{
    memset(mem, 0xFF, sizeof(mem));
    fail_budget = -1;
}

uint8_t *fake_flash_raw(uint32_t addr)
{
    return (uint8_t *)flash_map(addr);
}

void fake_flash_fail_after(long bytes)
{
    fail_budget = bytes;
}

const uint8_t *flash_map(uint32_t addr)
{
    if (addr < FLASH_BASE_ADDR || addr >= FLASH_BASE_ADDR + FLASH_TOTAL_SIZE) {
        fprintf(stderr, "flash_map: address 0x%08x out of range\n", addr);
        abort();
    }
    return &mem[addr - FLASH_BASE_ADDR];
}

int flash_erase_range(uint32_t addr, uint32_t len, void (*tick)(void))
{
    if (len == 0 || addr < BOOTLOADER_ADDR + BOOTLOADER_SIZE ||
        addr + len > FLASH_BASE_ADDR + FLASH_TOTAL_SIZE)
        return FLASH_ERR_RANGE;
    uint32_t end = addr + len;
    while (addr < end) {
        uint32_t base, size;
        if (flash_sector_of(addr, &base, &size) < 0)
            return FLASH_ERR_RANGE;
        if (tick)
            tick();
        memset(&mem[base - FLASH_BASE_ADDR], 0xFF, size);
        addr = base + size;
    }
    return FLASH_OK;
}

int flash_program(uint32_t addr, const void *data, uint32_t len)
{
    if ((addr & 3u) || (len & 3u) || addr < BOOTLOADER_ADDR + BOOTLOADER_SIZE ||
        addr + len > FLASH_BASE_ADDR + FLASH_TOTAL_SIZE)
        return FLASH_ERR_RANGE;
    const uint8_t *src = data;
    for (uint32_t i = 0; i < len; i += 4) {
        uint8_t *dst = &mem[addr + i - FLASH_BASE_ADDR];
        if (memcmp(dst, "\xff\xff\xff\xff", 4) != 0) {
            fprintf(stderr, "flash_program: 0x%08x is not erased\n", addr + i);
            abort();
        }
        if (fail_budget == 0)
            return FLASH_ERR_HW;
        if (fail_budget > 0)
            fail_budget -= 4;
        memcpy(dst, src + i, 4);
    }
    return FLASH_OK;
}
