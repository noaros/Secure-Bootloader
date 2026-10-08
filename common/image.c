#include "image.h"
#include "flash.h"
#include "layout.h"
#include "sha256.h"
#include "uECC.h"
#include <string.h>

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Constant-time compare; returns 0 when equal. */
static uint32_t ct_diff(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint32_t d = 0;
    for (uint32_t i = 0; i < n; i++)
        d |= (uint32_t)(a[i] ^ b[i]);
    return d;
}

uint32_t image_verify(unsigned slot, uint32_t min_security_counter, image_header_t *out)
{
    if (slot >= NUM_SLOTS)
        return IMG_ERR_ADDR;

    const uint32_t base = slot_addr(slot);
    image_header_t h;
    memcpy(&h, flash_map(base), sizeof(h));   /* work on a RAM copy */

    if (h.magic == 0xFFFFFFFFu)
        return IMG_ERR_EMPTY;
    if (h.magic != IMAGE_MAGIC || h.hdr_version != IMAGE_HDR_VERSION ||
        h.hdr_size != IMAGE_HEADER_SIZE)
        return IMG_ERR_HEADER;
    if (h.load_addr != base)
        return IMG_ERR_ADDR;
    if (h.img_size < 8u || h.img_size > SLOT_SIZE - IMAGE_HEADER_SIZE)
        return IMG_ERR_SIZE;

    /* Vector table: initial SP inside SRAM, reset handler inside the payload. */
    const uint8_t *payload = flash_map(base + IMAGE_HEADER_SIZE);
    uint32_t sp = rd32(payload), pc = rd32(payload + 4);
    uint32_t code_lo = base + IMAGE_HEADER_SIZE, code_hi = code_lo + h.img_size;
    if (sp < SRAM_BASE_ADDR + 8u || sp > SRAM_BASE_ADDR + SRAM_SIZE || (sp & 7u) ||
        !(pc & 1u) || (pc & ~1u) < code_lo || (pc & ~1u) >= code_hi)
        return IMG_ERR_VECTORS;

    uint8_t digest[32];
    sha256(payload, h.img_size, digest);
    volatile uint32_t hash_diff = ct_diff(digest, h.payload_sha256, 32);
    if (hash_diff != 0)
        return IMG_ERR_HASH;

    sha256(&h, IMAGE_SIGNED_LEN, digest);
    volatile int sig_ok = uECC_verify(boot_public_key, digest, sizeof(digest),
                                      h.signature, uECC_secp256r1());
    if (sig_ok != 1)
        return IMG_ERR_SIG;

    if (h.security_counter < min_security_counter)
        return IMG_ERR_ROLLBACK;

    /* Re-check the critical results (cheap glitch hardening). */
    if (hash_diff != 0 || sig_ok != 1 || h.load_addr != base)
        return IMG_ERR_SIG;

    if (out)
        *out = h;
    return IMG_OK;
}

const char *image_strerror(uint32_t rc)
{
    switch (rc) {
    case IMG_OK:           return "OK";
    case IMG_ERR_EMPTY:    return "empty slot";
    case IMG_ERR_HEADER:   return "bad header";
    case IMG_ERR_ADDR:     return "linked for a different slot";
    case IMG_ERR_SIZE:     return "bad size";
    case IMG_ERR_VECTORS:  return "bad vector table";
    case IMG_ERR_HASH:     return "payload hash mismatch";
    case IMG_ERR_SIG:      return "SIGNATURE INVALID";
    case IMG_ERR_ROLLBACK: return "rollback blocked (security counter too low)";
    default:               return "unknown error";
    }
}
