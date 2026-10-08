/*
 * Persistent A/B boot state.
 *
 * Stored as an append-only log of 32-byte CRC-protected records spread over
 * two 16K flash sectors. The valid record with the highest sequence number
 * wins. A record is only trusted once its final CRC word is programmed, so a
 * power cut mid-write simply leaves the previous state in effect. When a
 * sector fills up, the other one is erased and the log continues there.
 */
#ifndef BOOTSTATE_H
#define BOOTSTATE_H

#include <stdint.h>
#include "image.h"

#define BS_MAGIC          0x41545342u   /* "BSTA" */
#define BS_RECORD_SIZE    32u
#define BOOT_MAX_ATTEMPTS 3u

enum bs_state {
    BS_CONFIRMED = 0xC3,   /* active slot is known-good */
    BS_PENDING   = 0x5A,   /* active slot is on trial, not yet confirmed */
};

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint8_t active_slot;
    uint8_t state;
    uint8_t attempts;              /* trial boots started so far */
    uint8_t reserved0;
    uint32_t min_security_counter; /* anti-rollback floor */
    uint32_t reserved[3];
    uint32_t crc;                  /* CRC-32 of the preceding 28 bytes */
} boot_state_t;

_Static_assert(sizeof(boot_state_t) == BS_RECORD_SIZE, "record layout");

int bs_load(boot_state_t *st);           /* 1 if a valid record was found */
int bs_store(boot_state_t *st);          /* assigns seq/magic/crc; FLASH_OK on success */

/* ---- Bootloader policy ---- */

typedef struct {
    int slot;              /* slot to boot, or -1 to enter recovery */
    int trial;             /* booting a not-yet-confirmed image */
    boot_state_t st;       /* state after the decision was persisted */
    image_header_t hdr;    /* verified header of the chosen slot */
} boot_decision_t;

/* Decide what to boot, persisting attempt counters / rollbacks as needed. */
void boot_decide(boot_decision_t *d);

/* ---- Application helpers ---- */

#define BS_ERR_NO_STATE  (-10)
#define BS_ERR_UNCONFIRMED (-11)

/* Mark the running image good and raise the anti-rollback floor. 1 = changed. */
int bs_confirm(unsigned running_slot, uint32_t security_counter);

/*
 * Ask the bootloader to trial-boot `slot` on next reset. Refused while the
 * running image is itself unconfirmed (its fallback would be overwritten)
 * unless `force` is set, which only the bootloader's recovery mode does.
 */
int bs_request_trial(unsigned slot, int force);

#endif
