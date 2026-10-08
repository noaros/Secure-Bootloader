#include "update.h"
#include "board.h"
#include "crc32.h"
#include "flash.h"
#include "layout.h"

#define BYTE_TIMEOUT_MS 5000u

static const char magic[] = "UPD1";

int update_detect(int c)
{
    static unsigned pos;
    if (c < 0)
        return 0;
    if (c == magic[pos]) {
        if (++pos == 4) {
            pos = 0;
            return 1;
        }
    } else {
        pos = (c == magic[0]);
    }
    return 0;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void send_err(uint8_t code, uint8_t detail)
{
    uint8_t r[3] = { UPD_ERR, code, detail };
    uart_write(r, 3);
}

static void send_byte(uint8_t b)
{
    uart_write(&b, 1);
}

void update_refuse(void)
{
    send_err(UPD_E_REFUSED, 0);
}

uint32_t update_session(unsigned slot, uint32_t min_sc, image_header_t *hdr)
{
    static uint8_t buf[UPD_CHUNK_MAX + 4];
    static uint8_t header[IMAGE_HEADER_SIZE];
    const uint32_t base = slot_addr(slot);

    uint8_t hello[9];
    hello[0] = UPD_ACK;
    put32(hello + 1, base);
    put32(hello + 5, SLOT_SIZE);
    uart_write(hello, sizeof(hello));

    if (uart_read(buf, 4, BYTE_TIMEOUT_MS)) {
        send_err(UPD_E_TIMEOUT, 0);
        return IMG_ERR_EMPTY;
    }
    uint32_t total = get32(buf);
    if (total < IMAGE_HEADER_SIZE + 8u || total > SLOT_SIZE || (total & 3u)) {
        send_err(UPD_E_SIZE, 0);
        return IMG_ERR_SIZE;
    }

    if (flash_erase_range(base, SLOT_SIZE, iwdg_kick)) {
        send_err(UPD_E_ERASE, 0);
        return IMG_ERR_EMPTY;
    }
    send_byte(UPD_ACK);

    uint32_t done = 0;
    while (done < total) {
        iwdg_kick();
        if (uart_read(buf, 2, BYTE_TIMEOUT_MS)) {
            send_err(UPD_E_TIMEOUT, 1);
            return IMG_ERR_EMPTY;
        }
        uint32_t len = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8);
        if (len == 0 || len > UPD_CHUNK_MAX || (len & 3u) || len > total - done) {
            send_err(UPD_E_SIZE, 1);
            return IMG_ERR_SIZE;
        }
        if (uart_read(buf, len + 4, BYTE_TIMEOUT_MS)) {
            send_err(UPD_E_TIMEOUT, 2);
            return IMG_ERR_EMPTY;
        }
        if (crc32(buf, len) != get32(buf + len)) {
            send_byte(UPD_NAK);
            continue;
        }

        /*
         * Hold the header back in RAM and program it last, so an interrupted
         * upload leaves a slot that reads as empty rather than half-written.
         */
        uint32_t n_hdr = 0;
        if (done < IMAGE_HEADER_SIZE) {
            n_hdr = IMAGE_HEADER_SIZE - done;
            if (n_hdr > len)
                n_hdr = len;
            for (uint32_t i = 0; i < n_hdr; i++)
                header[done + i] = buf[i];
        }
        if (len > n_hdr && flash_program(base + done + n_hdr, buf + n_hdr, len - n_hdr)) {
            send_err(UPD_E_PROGRAM, 0);
            return IMG_ERR_EMPTY;
        }
        done += len;
        if (done < total)
            send_byte(UPD_ACK);
    }

    if (flash_program(base, header, IMAGE_HEADER_SIZE)) {
        send_err(UPD_E_PROGRAM, 1);
        return IMG_ERR_EMPTY;
    }

    iwdg_kick();
    uint32_t rc = image_verify(slot, min_sc, hdr);
    if (rc != IMG_OK) {
        send_err(UPD_E_IMAGE, (uint8_t)rc);
        return rc;
    }
    send_byte(UPD_ACK);
    return IMG_OK;
}
