/*
 * Secure A/B bootloader for the Nucleo-F429ZI.
 *
 *  - Verifies the ECDSA-P256 signature and SHA-256 hash of an image before
 *    every boot; unsigned or tampered images are never executed.
 *  - Enforces a monotonic security counter (anti-rollback).
 *  - Trial-boots freshly installed images and rolls back to the previous slot
 *    if the new one doesn't confirm itself within BOOT_MAX_ATTEMPTS resets.
 *  - Arms the independent watchdog before handing over, so a hung image
 *    resets instead of bricking the board.
 *  - Recovery mode (no bootable image, or USER button held at reset) accepts a
 *    signed image over the ST-LINK virtual COM port.
 */
#include "board.h"
#include "bootstate.h"
#include "console.h"
#include "flash.h"
#include "image.h"
#include "layout.h"
#include "stm32f429.h"
#include "update.h"

static void print_protection(void)
{
    uint32_t optcr = FLASH_OPTCR;
    uint32_t rdp = (optcr >> 8) & 0xFF;
    int level = rdp == 0xAA ? 0 : rdp == 0xCC ? 2 : 1;
    int pcrop = (optcr >> 31) & 1;
    uint32_t nwrp = (optcr >> 16) & 0xFFF;
    int boot_wp = !pcrop && (nwrp & 0x3u) == 0;

    con_printf("[sec]  RDP level %d%s, bootloader write-protect %s\n", level,
               level == 0 ? " (debug port open)" : "", boot_wp ? "ON" : "OFF");
    if (level == 0 || !boot_wp)
        con_puts("[sec]  development configuration - run `make protect` to lock it down\n");
}

static void __attribute__((noreturn)) jump_to_image(const boot_decision_t *d)
{
    const uint32_t vt = slot_addr((unsigned)d->slot) + IMAGE_HEADER_SIZE;

    /* Second, independent sanity check of the decision before committing. */
    if (d->hdr.magic != IMAGE_MAGIC || d->hdr.load_addr != slot_addr((unsigned)d->slot))
        system_reset();

    const uint32_t sp = *(const volatile uint32_t *)vt;
    const uint32_t pc = *(const volatile uint32_t *)(vt + 4);

    con_printf("[boot] starting slot %c%s\n\n", slot_name((unsigned)d->slot),
               d->trial ? " (trial - image must confirm itself)" : "");

    iwdg_start();
    board_deinit();

    __asm volatile("cpsid i" ::: "memory");
    for (int i = 0; i < 8; i++) {
        NVIC_ICER(i) = 0xFFFFFFFFu;
        NVIC_ICPR(i) = 0xFFFFFFFFu;
    }
    SCB_VTOR = vt;
    __asm volatile(
        "msr msp, %0\n"
        "dsb\n"
        "isb\n"
        "cpsie i\n"
        "bx %1\n"
        :: "r"(sp), "r"(pc) : "memory");
    __builtin_unreachable();
}

/* Choose where a recovery upload goes without destroying a good fallback. */
static unsigned recovery_target(uint32_t *min_sc)
{
    boot_state_t st;
    int have = bs_load(&st);
    *min_sc = have ? st.min_security_counter : 0;

    int ok_a = image_verify(0, *min_sc, 0) == IMG_OK;
    int ok_b = image_verify(1, *min_sc, 0) == IMG_OK;
    if (ok_a && ok_b)
        return have ? st.active_slot ^ 1u : 1u;
    if (ok_a)
        return 1;
    return 0;
}

static void __attribute__((noreturn)) recovery(const char *why)
{
    uint32_t min_sc;
    unsigned target = recovery_target(&min_sc);

    con_printf("\n[recovery] %s\n", why);
    con_printf("[recovery] waiting for a signed image for slot %c over this UART\n",
               slot_name(target));
    con_puts("[recovery] host: tools/update.py --port <tty> <images...>   ('r' = reboot)\n");

    for (;;) {
        int c = uart_getc(150);
        if (c < 0) {
            led_toggle(LED_RED);
            continue;
        }
        if (c == 'r')
            system_reset();
        if (!update_detect(c))
            continue;

        led_set(LED_RED, 1);
        image_header_t h;
        uint32_t rc = update_session(target, min_sc, &h);
        board_delay_ms(20);   /* let the final protocol byte go out first */
        if (rc == IMG_OK) {
            con_printf("\n[recovery] slot %c: v%u.%u.%u verified, installing as trial image\n",
                       slot_name(target), VER_MAJOR(h.version), VER_MINOR(h.version),
                       VER_PATCH(h.version));
            if (bs_request_trial(target, 1) != 0)
                con_puts("[recovery] failed to write boot state\n");
            system_reset();
        }
        con_printf("\n[recovery] upload failed: %s\n", image_strerror(rc));
        target = recovery_target(&min_sc);
    }
}

int main(void)
{
    board_init();
    const char *cause = reset_cause(0);   /* the application clears the flags */

    con_puts("\n\n==============================================\n");
    con_puts(" Secure A/B bootloader - NUCLEO-F429ZI\n");
    con_puts("==============================================\n");
    con_printf("[boot] reset cause: %s\n", cause);
    print_protection();

    if (button_pressed()) {
        board_delay_ms(50);
        if (button_pressed())
            recovery("USER button held at reset");
    }

    led_set(LED_RED, 1);
    boot_decision_t d;
    boot_decide(&d);
    led_set(LED_RED, 0);

    if (d.slot < 0)
        recovery("no authentic bootable image found");
    jump_to_image(&d);
}
