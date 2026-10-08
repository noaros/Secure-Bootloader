/* STM32F4 flash controller driver (register level, x32 parallelism @ 2.7-3.6 V). */
#include "flash.h"
#include "layout.h"
#include "stm32f429.h"

const uint8_t *flash_map(uint32_t addr)
{
    return (const uint8_t *)(uintptr_t)addr;
}

static void wait_idle(void)
{
    while (FLASH_SR & FLASH_SR_BSY)
        ;
}

static void unlock(void)
{
    wait_idle();
    if (FLASH_CR & FLASH_CR_LOCK) {
        FLASH_KEYR = FLASH_KEY1;
        FLASH_KEYR = FLASH_KEY2;
    }
    FLASH_SR = FLASH_SR_ERRORS | FLASH_SR_EOP;   /* write-1-to-clear */
}

static void lock(void)
{
    wait_idle();
    FLASH_CR = FLASH_CR_LOCK;
}

/* The ART accelerator may hold stale lines for erased/reprogrammed flash. */
static void flush_caches(void)
{
    uint32_t acr = FLASH_ACR;
    FLASH_ACR = acr & ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    FLASH_ACR = (acr & ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN)) | FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    FLASH_ACR = acr & ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
}

static int erase_sector(int sector)
{
    /* SNB encoding: bank-2 sectors 12..23 map to 16..27 */
    uint32_t snb = (uint32_t)(sector < 12 ? sector : sector + 4);

    FLASH_CR = FLASH_CR_PSIZE_X32 | FLASH_CR_SER | (snb << FLASH_CR_SNB_POS);
    FLASH_CR |= FLASH_CR_STRT;
    wait_idle();
    uint32_t sr = FLASH_SR;
    FLASH_CR = 0;
    return (sr & FLASH_SR_ERRORS) ? FLASH_ERR_HW : FLASH_OK;
}

int flash_erase_range(uint32_t addr, uint32_t len, void (*tick)(void))
{
    if (len == 0 || addr < BOOTLOADER_ADDR + BOOTLOADER_SIZE ||
        addr + len > FLASH_BASE_ADDR + FLASH_TOTAL_SIZE || addr + len < addr)
        return FLASH_ERR_RANGE;

    int rc = FLASH_OK;
    uint32_t end = addr + len;
    unlock();
    while (addr < end) {
        uint32_t base, size;
        int sec = flash_sector_of(addr, &base, &size);
        if (sec < 0 || base < BOOTLOADER_ADDR + BOOTLOADER_SIZE) {
            rc = FLASH_ERR_RANGE;
            break;
        }
        if (tick)
            tick();
        rc = erase_sector(sec);
        if (rc)
            break;
        addr = base + size;
    }
    lock();
    flush_caches();
    return rc;
}

int flash_program(uint32_t addr, const void *data, uint32_t len)
{
    if ((addr & 3u) || (len & 3u) || addr < BOOTLOADER_ADDR + BOOTLOADER_SIZE ||
        addr + len > FLASH_BASE_ADDR + FLASH_TOTAL_SIZE || addr + len < addr)
        return FLASH_ERR_RANGE;

    const uint8_t *src = data;
    int rc = FLASH_OK;
    unlock();
    FLASH_CR = FLASH_CR_PSIZE_X32 | FLASH_CR_PG;
    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t w = (uint32_t)src[i] | ((uint32_t)src[i + 1] << 8) |
                     ((uint32_t)src[i + 2] << 16) | ((uint32_t)src[i + 3] << 24);
        REG32(addr + i) = w;
        __asm volatile("dsb" ::: "memory");
        wait_idle();
        if (FLASH_SR & FLASH_SR_ERRORS) {
            rc = FLASH_ERR_HW;
            break;
        }
        if (REG32(addr + i) != w) {
            rc = FLASH_ERR_VERIFY;
            break;
        }
    }
    FLASH_CR = 0;
    lock();
    flush_caches();
    return rc;
}
