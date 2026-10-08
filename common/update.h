/*
 * Serial image upload protocol (device side), shared by the application and
 * the bootloader's recovery mode. Host side: tools/update.py.
 *
 *   host -> "UPD1"
 *   dev  -> ACK, u32 target_load_addr, u32 max_size        | ERR, code, detail
 *   host -> u32 total_size (multiple of 4)
 *   dev  -> ACK (after erasing the target slot)             | ERR, code, detail
 *   repeat:
 *     host -> u16 len (<= 1024, multiple of 4), data[len], u32 crc32(data)
 *     dev  -> ACK | NAK (crc mismatch, resend) | ERR, code, detail
 *   after the last chunk the device fully verifies the image:
 *   dev  -> ACK                                             | ERR, code, detail
 *
 * All integers little-endian.
 */
#ifndef UPDATE_H
#define UPDATE_H

#include <stdint.h>
#include "image.h"

#define UPD_ACK 0x06
#define UPD_NAK 0x15
#define UPD_ERR 0x18

#define UPD_CHUNK_MAX 1024u

enum {
    UPD_E_REFUSED = 1,  /* update not allowed right now */
    UPD_E_SIZE,
    UPD_E_ERASE,
    UPD_E_TIMEOUT,
    UPD_E_PROGRAM,
    UPD_E_IMAGE,        /* detail = IMG_ERR_* */
};

/* Feed received console bytes; returns 1 once "UPD1" has been seen. */
int update_detect(int c);

/* Send the refusal response for a detected session. */
void update_refuse(void);

/*
 * Run one upload session into `slot` (the magic was already consumed).
 * Returns IMG_OK when an authentic image was written and verified.
 */
uint32_t update_session(unsigned slot, uint32_t min_security_counter, image_header_t *hdr);

#endif
