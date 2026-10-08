/*
 * Signed image format. A slot contains:
 *
 *   +0x000  image_header_t (128 bytes, rest of the 512-byte block is 0xFF)
 *   +0x200  application payload (starts with its vector table)
 *
 * payload_sha256 = SHA-256(payload)
 * signature      = ECDSA-P256(r||s) over SHA-256(header bytes [0, 64))
 *
 * The signed region covers every field the bootloader acts on, including
 * load address, version and security counter. Keep in sync with tools/imgtool.py.
 */
#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

#define IMAGE_MAGIC        0x31494253u   /* "SBI1" */
#define IMAGE_HDR_VERSION  1u
#define IMAGE_SIGNED_LEN   64u

typedef struct {
    uint32_t magic;
    uint16_t hdr_version;
    uint16_t hdr_size;
    uint32_t load_addr;          /* slot base this build is linked for */
    uint32_t img_size;           /* payload bytes after the header */
    uint32_t version;            /* major << 24 | minor << 16 | patch */
    uint32_t security_counter;   /* anti-rollback counter */
    uint32_t flags;
    uint32_t reserved;
    uint8_t payload_sha256[32];
    uint8_t signature[64];
} image_header_t;

_Static_assert(sizeof(image_header_t) == 128, "header layout");
_Static_assert(__builtin_offsetof(image_header_t, signature) == IMAGE_SIGNED_LEN, "signed len");

/* Non-trivial success value so a single glitched branch/bit can't fake it. */
#define IMG_OK 0x5AA5C33Cu

enum {
    IMG_ERR_EMPTY = 1,
    IMG_ERR_HEADER,
    IMG_ERR_ADDR,
    IMG_ERR_SIZE,
    IMG_ERR_VECTORS,
    IMG_ERR_HASH,
    IMG_ERR_SIG,
    IMG_ERR_ROLLBACK,
};

/* Public key compiled into the firmware (generated from keys/ at build time). */
extern const uint8_t boot_public_key[64];

/*
 * Fully verify the image in `slot`: header sanity, vector table plausibility,
 * payload hash, ECDSA signature and anti-rollback counter. Returns IMG_OK or
 * an IMG_ERR_* code. The verified header is copied to *hdr when non-NULL.
 */
uint32_t image_verify(unsigned slot, uint32_t min_security_counter, image_header_t *hdr);

const char *image_strerror(uint32_t rc);

#define VER_MAJOR(v) ((unsigned)((v) >> 24))
#define VER_MINOR(v) ((unsigned)(((v) >> 16) & 0xFF))
#define VER_PATCH(v) ((unsigned)((v) & 0xFFFF))

#endif
