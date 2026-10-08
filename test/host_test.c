/*
 * Host-side tests: SHA-256, image verification against images signed by
 * tools/imgtool.py, the boot-state log (incl. power loss), and the A/B
 * boot policy end to end on a simulated flash.
 *
 * Usage: host_test <dir produced by test/gen_images.py>
 */
#include "bootstate.h"
#include "console.h"
#include "fake_flash.h"
#include "flash.h"
#include "image.h"
#include "layout.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks, verbose;
static const char *dir;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

void con_putc(char c)
{
    if (verbose && c != '\r')
        putchar(c);
}

static void install(unsigned slot, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.signed.bin", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    static uint8_t buf[SLOT_SIZE];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    CHECK(flash_erase_range(slot_addr(slot), SLOT_SIZE, NULL) == FLASH_OK);
    CHECK(flash_program(slot_addr(slot), buf, (uint32_t)n) == FLASH_OK);
}

static void test_sha256(void)
{
    static const struct { const char *msg; const char *hex; } v[] = {
        { "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
        { "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
        { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" },
    };
    for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
        uint8_t d[32];
        char hex[65];
        sha256(v[i].msg, strlen(v[i].msg), d);
        for (int j = 0; j < 32; j++)
            sprintf(hex + 2 * j, "%02x", d[j]);
        CHECK(strcmp(hex, v[i].hex) == 0);
    }
    /* 1,000,000 x 'a' in odd-sized pieces exercises the buffering path. */
    sha256_ctx c;
    uint8_t a[997], d[32];
    memset(a, 'a', sizeof(a));
    sha256_init(&c);
    size_t left = 1000000;
    while (left) {
        size_t n = left < sizeof(a) ? left : sizeof(a);
        sha256_update(&c, a, n);
        left -= n;
    }
    sha256_final(&c, d);
    static const uint8_t want[4] = { 0xcd, 0xc7, 0x6e, 0x5c };
    CHECK(memcmp(d, want, 4) == 0);
}

static void test_image_verify(void)
{
    image_header_t h;
    fake_flash_reset();
    CHECK(image_verify(0, 0, &h) == IMG_ERR_EMPTY);

    install(0, "A_v1");
    CHECK(image_verify(0, 0, &h) == IMG_OK);
    CHECK(h.version == 0x01000000u && h.security_counter == 1);
    CHECK(image_verify(0, 1, NULL) == IMG_OK);
    CHECK(image_verify(0, 2, NULL) == IMG_ERR_ROLLBACK);

    /* An image linked for slot A can't be booted from slot B. */
    install(1, "A_v1");
    CHECK(image_verify(1, 0, NULL) == IMG_ERR_ADDR);

    install(1, "B_badkey");
    CHECK(image_verify(1, 0, NULL) == IMG_ERR_SIG);

    /* Tamper with the payload: hash mismatch. Flipping 1->0 is legal on NOR. */
    install(1, "B_v2");
    CHECK(image_verify(1, 0, NULL) == IMG_OK);
    uint8_t *p = fake_flash_raw(SLOT_B_ADDR + IMAGE_HEADER_SIZE + 100);
    *p ^= 0x01;
    CHECK(image_verify(1, 0, NULL) == IMG_ERR_HASH);

    /* Tamper with a signed header field (security counter): signature fails. */
    install(1, "B_v2");
    image_header_t *raw = (image_header_t *)fake_flash_raw(SLOT_B_ADDR);
    raw->security_counter = 100;
    CHECK(image_verify(1, 0, NULL) == IMG_ERR_SIG);

    /* Tamper with the signature itself. */
    install(1, "B_v2");
    raw->signature[10] ^= 0x80;
    CHECK(image_verify(1, 0, NULL) == IMG_ERR_SIG);

    /* Oversized length claims are rejected before hashing anything. */
    install(1, "B_v2");
    raw->img_size = SLOT_SIZE;
    CHECK(image_verify(1, 0, NULL) == IMG_ERR_SIZE);
}

static void test_bootstate_log(void)
{
    boot_state_t st;
    fake_flash_reset();
    CHECK(!bs_load(&st));

    memset(&st, 0, sizeof(st));
    st.active_slot = 1;
    st.state = BS_PENDING;
    st.min_security_counter = 7;
    CHECK(bs_store(&st) == FLASH_OK);
    memset(&st, 0, sizeof(st));
    CHECK(bs_load(&st) && st.active_slot == 1 && st.state == BS_PENDING &&
          st.min_security_counter == 7 && st.seq == 1);

    /* Fill both sectors several times over: the newest record must always win. */
    for (unsigned i = 0; i < 1500; i++) {
        st.attempts = (uint8_t)i;
        CHECK(bs_store(&st) == FLASH_OK);
    }
    boot_state_t got;
    CHECK(bs_load(&got) && got.attempts == (uint8_t)1499 && got.seq == 1501);

    /* Power loss mid-record: previous state survives, next write skips the debris. */
    st.attempts = 42;
    fake_flash_fail_after(12);
    CHECK(bs_store(&st) != FLASH_OK);
    fake_flash_fail_after(-1);
    CHECK(bs_load(&got) && got.attempts == (uint8_t)1499);
    st.attempts = 43;
    CHECK(bs_store(&st) == FLASH_OK);
    CHECK(bs_load(&got) && got.attempts == 43);

    /* Power loss right before the CRC word. */
    st.attempts = 44;
    fake_flash_fail_after(28);
    CHECK(bs_store(&st) != FLASH_OK);
    fake_flash_fail_after(-1);
    CHECK(bs_load(&got) && got.attempts == 43);
}

static boot_decision_t decide(void)
{
    boot_decision_t d;
    boot_decide(&d);
    return d;
}

static void test_policy(void)
{
    boot_decision_t d;
    boot_state_t st;

    /* Factory: only slot A programmed. */
    fake_flash_reset();
    CHECK(decide().slot == -1);              /* nothing at all -> recovery */
    install(0, "A_v1");
    d = decide();
    CHECK(d.slot == 0 && !d.trial && d.st.state == BS_CONFIRMED && d.st.min_security_counter == 1);
    d = decide();
    CHECK(d.slot == 0 && !d.trial);

    /* Update to B that never confirms: 3 trial boots, then rollback to A. */
    install(1, "B_v2");
    CHECK(bs_request_trial(1, 0) == FLASH_OK);
    for (unsigned i = 1; i <= BOOT_MAX_ATTEMPTS; i++) {
        d = decide();
        CHECK(d.slot == 1 && d.trial && d.st.attempts == i);
    }
    CHECK(bs_request_trial(0, 0) == BS_ERR_UNCONFIRMED);   /* can't update while on trial */
    d = decide();
    CHECK(d.slot == 0 && !d.trial && d.st.state == BS_CONFIRMED);
    CHECK(d.st.min_security_counter == 1);

    /* Same update, this time it confirms: counter floor rises to 2. */
    CHECK(bs_request_trial(1, 0) == FLASH_OK);
    d = decide();
    CHECK(d.slot == 1 && d.trial);
    CHECK(bs_confirm(1, d.hdr.security_counter) == 1);
    CHECK(bs_confirm(1, d.hdr.security_counter) == 0);
    CHECK(bs_load(&st) && st.state == BS_CONFIRMED && st.min_security_counter == 2);
    d = decide();
    CHECK(d.slot == 1 && !d.trial);

    /* Downgrade attempt to the old v1 (counter 1) in slot A: blocked, stays on B. */
    CHECK(bs_request_trial(0, 0) == FLASH_OK);
    d = decide();
    CHECK(d.slot == 1 && !d.trial && d.st.state == BS_CONFIRMED);

    /* Pending image that's been tampered with: immediate rollback, no trial. */
    install(0, "A_v3");
    *fake_flash_raw(SLOT_A_ADDR + IMAGE_HEADER_SIZE + 64) ^= 0x10;
    CHECK(bs_request_trial(0, 0) == FLASH_OK);
    d = decide();
    CHECK(d.slot == 1 && !d.trial);

    /* Image signed with an unknown key: rejected as well. */
    install(0, "B_badkey");
    CHECK(bs_request_trial(0, 0) == FLASH_OK);
    CHECK(decide().slot == 1);

    /* Confirmed slot B gets corrupted; A holds only rolled-back v1 -> recovery. */
    install(0, "A_v1");
    *fake_flash_raw(SLOT_B_ADDR + IMAGE_HEADER_SIZE + 8) ^= 0x01;
    CHECK(decide().slot == -1);

    /* Recovery installs v3 into A as a forced trial; it boots, confirms, floor -> 3. */
    install(0, "A_v3");
    CHECK(bs_request_trial(0, 1) == FLASH_OK);
    d = decide();
    CHECK(d.slot == 0 && d.trial);
    CHECK(bs_confirm(0, d.hdr.security_counter) == 1);
    d = decide();
    CHECK(d.slot == 0 && !d.trial && d.st.min_security_counter == 3);

    /* A damaged confirmed slot falls back to an authentic image in the other slot. */
    fake_flash_reset();
    install(0, "A_v1");
    install(1, "B_v2");
    d = decide();                             /* factory scan picks the newest */
    CHECK(d.slot == 1 && d.st.min_security_counter == 2);
    install(0, "A_v3");
    *fake_flash_raw(SLOT_B_ADDR + 0x300) ^= 0x01;
    d = decide();
    CHECK(d.slot == 0 && !d.trial);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <test image dir> [-v]\n", argv[0]);
        return 2;
    }
    dir = argv[1];
    verbose = argc > 2 && strcmp(argv[2], "-v") == 0;

    test_sha256();
    test_image_verify();
    test_bootstate_log();
    test_policy();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
