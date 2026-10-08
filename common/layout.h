/*
 * Flash memory map (STM32F429ZI, 2 MB dual-bank).
 *
 *  Bank 1                                   Bank 2
 *  0x08000000  sec 0-1   Bootloader (32K)   0x08100000  sec 12-16  unused
 *  0x08008000  sec 2     Boot state log A   0x08120000  sec 17-19  SLOT B (384K)
 *  0x0800C000  sec 3     Boot state log B   0x08180000  sec 20-23  unused
 *  0x08010000  sec 4     unused (64K)
 *  0x08020000  sec 5-7   SLOT A (384K)
 *  0x08080000  sec 8-11  unused
 *
 * The slots live in different banks, so the running image can erase and
 * program the other slot with read-while-write (no CPU stall).
 *
 * Each slot holds a 512-byte signed header followed by the application,
 * whose vector table therefore lands at slot base + 0x200 (VTOR-aligned).
 */
#ifndef LAYOUT_H
#define LAYOUT_H

#define FLASH_BASE_ADDR       0x08000000u
#define FLASH_TOTAL_SIZE      0x00200000u

#define BOOTLOADER_ADDR       0x08000000u
#define BOOTLOADER_SIZE       0x00008000u

#define BOOTSTATE_SECTOR0     2u
#define BOOTSTATE_SECTOR1     3u
#define BOOTSTATE_ADDR0       0x08008000u
#define BOOTSTATE_ADDR1       0x0800C000u
#define BOOTSTATE_SECTOR_SIZE 0x4000u

#define SLOT_A_ADDR           0x08020000u
#define SLOT_B_ADDR           0x08120000u
#define SLOT_SIZE             0x00060000u

#define IMAGE_HEADER_SIZE     0x200u

#define SRAM_BASE_ADDR        0x20000000u
#define SRAM_SIZE             0x00030000u   /* 192K contiguous SRAM1+2+3 */

#define NUM_SLOTS             2u

static inline unsigned slot_addr(unsigned slot)
{
    return slot == 0 ? SLOT_A_ADDR : SLOT_B_ADDR;
}

static inline char slot_name(unsigned slot)
{
    return slot == 0 ? 'A' : 'B';
}

#endif
