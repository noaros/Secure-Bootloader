/* Sector geometry and helpers shared by the real and the fake flash driver. */
#include "flash.h"
#include "layout.h"

int flash_sector_of(uint32_t addr, uint32_t *base, uint32_t *size)
{
    if (addr < FLASH_BASE_ADDR || addr >= FLASH_BASE_ADDR + FLASH_TOTAL_SIZE)
        return -1;

    uint32_t off = addr - FLASH_BASE_ADDR;
    int bank = off >= 0x100000u;
    uint32_t boff = off & 0xFFFFFu;
    uint32_t bbase = FLASH_BASE_ADDR + (bank ? 0x100000u : 0u);
    int sec;
    uint32_t sbase, ssize;

    if (boff < 0x10000u) {          /* 4 x 16K */
        sec = (int)(boff / 0x4000u);
        sbase = (uint32_t)sec * 0x4000u;
        ssize = 0x4000u;
    } else if (boff < 0x20000u) {   /* 1 x 64K */
        sec = 4;
        sbase = 0x10000u;
        ssize = 0x10000u;
    } else {                        /* 7 x 128K */
        sec = 5 + (int)((boff - 0x20000u) / 0x20000u);
        sbase = 0x20000u + (uint32_t)(sec - 5) * 0x20000u;
        ssize = 0x20000u;
    }
    if (base)
        *base = bbase + sbase;
    if (size)
        *size = ssize;
    return sec + (bank ? 12 : 0);
}

int flash_is_blank(uint32_t addr, uint32_t len)
{
    const uint8_t *p = flash_map(addr);
    for (uint32_t i = 0; i < len; i++)
        if (p[i] != 0xFF)
            return 0;
    return 1;
}
