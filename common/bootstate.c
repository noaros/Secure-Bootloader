#include "bootstate.h"
#include "console.h"
#include "crc32.h"
#include "flash.h"
#include "layout.h"
#include <string.h>

#define RECORDS_PER_SECTOR (BOOTSTATE_SECTOR_SIZE / BS_RECORD_SIZE)

static const uint32_t sector_addr[2] = { BOOTSTATE_ADDR0, BOOTSTATE_ADDR1 };

static int record_valid(const boot_state_t *r)
{
    return r->magic == BS_MAGIC &&
           r->crc == crc32(r, offsetof(boot_state_t, crc)) &&
           r->active_slot < NUM_SLOTS &&
           (r->state == BS_CONFIRMED || r->state == BS_PENDING);
}

/* Locate the newest valid record. Returns 1 and fills sec/idx/out when found. */
static int find_latest(int *sec, int *idx, boot_state_t *out)
{
    int found = 0;
    for (int s = 0; s < 2; s++) {
        for (int i = 0; i < (int)RECORDS_PER_SECTOR; i++) {
            boot_state_t r;
            memcpy(&r, flash_map(sector_addr[s] + (uint32_t)i * BS_RECORD_SIZE), sizeof(r));
            if (!record_valid(&r))
                continue;
            if (!found || (int32_t)(r.seq - out->seq) > 0) {
                *out = r;
                *sec = s;
                *idx = i;
                found = 1;
            }
        }
    }
    return found;
}

int bs_load(boot_state_t *st)
{
    int s, i;
    return find_latest(&s, &i, st);
}

int bs_store(boot_state_t *st)
{
    boot_state_t latest;
    int sec = 0, idx = -1;
    uint32_t seq = 1;

    if (find_latest(&sec, &idx, &latest))
        seq = latest.seq + 1;

    /* First blank slot after the newest record (skips torn writes). */
    uint32_t addr = 0;
    for (int i = idx + 1; i < (int)RECORDS_PER_SECTOR; i++) {
        uint32_t a = sector_addr[sec] + (uint32_t)i * BS_RECORD_SIZE;
        if (flash_is_blank(a, BS_RECORD_SIZE)) {
            addr = a;
            break;
        }
    }
    if (!addr) {
        /* Sector full (or never used): continue in the other/first sector. */
        if (idx >= 0)
            sec ^= 1;
        int rc = flash_erase_range(sector_addr[sec], BOOTSTATE_SECTOR_SIZE, 0);
        if (rc)
            return rc;
        addr = sector_addr[sec];
    }

    st->magic = BS_MAGIC;
    st->seq = seq;
    st->reserved0 = 0;
    memset(st->reserved, 0, sizeof(st->reserved));
    st->crc = crc32(st, offsetof(boot_state_t, crc));

    /* Body first, CRC word last: the record only becomes valid at the end. */
    int rc = flash_program(addr, st, offsetof(boot_state_t, crc));
    if (!rc)
        rc = flash_program(addr + offsetof(boot_state_t, crc), &st->crc, 4);
    return rc;
}

/* ------------------------------------------------------------------------ */

static void log_verify(unsigned slot, uint32_t rc, const image_header_t *h)
{
    if (rc == IMG_OK)
        con_printf("[boot] slot %c: v%u.%u.%u  security counter %u  signature OK\n",
                   slot_name(slot), VER_MAJOR(h->version), VER_MINOR(h->version),
                   VER_PATCH(h->version), (unsigned)h->security_counter);
    else
        con_printf("[boot] slot %c: rejected - %s\n", slot_name(slot), image_strerror(rc));
}

static uint32_t verify(unsigned slot, uint32_t min_sc, image_header_t *h)
{
    uint32_t rc = image_verify(slot, min_sc, h);
    log_verify(slot, rc, h);
    return rc;
}

static void persist(boot_state_t *st)
{
    if (bs_store(st) != 0)
        con_puts("[boot] WARNING: failed to write boot state\n");
}

void boot_decide(boot_decision_t *d)
{
    boot_state_t *st = &d->st;
    d->slot = -1;
    d->trial = 0;

    if (!bs_load(st)) {
        /* Factory state: boot the newest valid image and mark it confirmed. */
        con_puts("[boot] no boot state found - scanning slots\n");
        image_header_t h[NUM_SLOTS];
        int best = -1;
        for (unsigned s = 0; s < NUM_SLOTS; s++)
            if (verify(s, 0, &h[s]) == IMG_OK &&
                (best < 0 || h[s].version > h[best].version))
                best = (int)s;
        if (best < 0)
            return;
        memset(st, 0, sizeof(*st));
        st->active_slot = (uint8_t)best;
        st->state = BS_CONFIRMED;
        st->min_security_counter = h[best].security_counter;
        persist(st);
        d->slot = best;
        d->hdr = h[best];
        return;
    }

    con_printf("[boot] state: active=%c %s attempts=%u min_counter=%u (seq %u)\n",
               slot_name(st->active_slot),
               st->state == BS_PENDING ? "PENDING" : "CONFIRMED",
               st->attempts, (unsigned)st->min_security_counter, (unsigned)st->seq);

    unsigned active = st->active_slot;

    if (st->state == BS_PENDING) {
        uint32_t rc = IMG_ERR_HEADER;
        if (st->attempts >= BOOT_MAX_ATTEMPTS)
            con_printf("[boot] slot %c failed %u trial boots without confirming\n",
                       slot_name(active), st->attempts);
        else
            rc = verify(active, st->min_security_counter, &d->hdr);

        if (rc == IMG_OK) {
            st->attempts++;
            persist(st);
            con_printf("[boot] trial boot %u/%u of slot %c\n",
                       st->attempts, BOOT_MAX_ATTEMPTS, slot_name(active));
            d->slot = (int)active;
            d->trial = 1;
            return;
        }

        /* Roll back to the previous (confirmed) slot. */
        active ^= 1u;
        con_printf("[boot] ROLLBACK to slot %c\n", slot_name(active));
        st->active_slot = (uint8_t)active;
        st->state = BS_CONFIRMED;
        st->attempts = 0;
        persist(st);
    }

    if (verify(active, st->min_security_counter, &d->hdr) == IMG_OK) {
        d->slot = (int)active;
        return;
    }

    /* Confirmed image is damaged: try the other slot if it is authentic. */
    unsigned other = active ^ 1u;
    if (verify(other, st->min_security_counter, &d->hdr) == IMG_OK) {
        con_printf("[boot] falling back to slot %c\n", slot_name(other));
        st->active_slot = (uint8_t)other;
        st->state = BS_CONFIRMED;
        st->attempts = 0;
        persist(st);
        d->slot = (int)other;
    }
}

int bs_confirm(unsigned running_slot, uint32_t security_counter)
{
    boot_state_t st;
    if (!bs_load(&st))
        return BS_ERR_NO_STATE;
    if (st.active_slot != running_slot)
        return BS_ERR_NO_STATE;
    if (st.state == BS_CONFIRMED && st.min_security_counter >= security_counter)
        return 0;
    st.state = BS_CONFIRMED;
    st.attempts = 0;
    if (security_counter > st.min_security_counter)
        st.min_security_counter = security_counter;
    int rc = bs_store(&st);
    return rc ? rc : 1;
}

int bs_request_trial(unsigned slot, int force)
{
    boot_state_t st;
    if (!bs_load(&st)) {
        if (!force)
            return BS_ERR_NO_STATE;
        memset(&st, 0, sizeof(st));
    } else if (st.state != BS_CONFIRMED && !force) {
        return BS_ERR_UNCONFIRMED;
    }
    st.active_slot = (uint8_t)slot;
    st.state = BS_PENDING;
    st.attempts = 0;
    return bs_store(&st);
}
