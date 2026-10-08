#ifndef FAKE_FLASH_H
#define FAKE_FLASH_H

#include <stdint.h>

void fake_flash_reset(void);                 /* whole device erased */
uint8_t *fake_flash_raw(uint32_t addr);      /* direct write access for tampering */
void fake_flash_fail_after(long bytes);      /* simulate power loss; -1 = never */

#endif
