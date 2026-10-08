/*
 * Demo application. Built twice (once linked for each slot) and signed by
 * tools/imgtool.py. It:
 *   - blinks green in slot A, blue in slot B (fast while on trial),
 *   - confirms itself after a short self-test so the bootloader keeps it,
 *   - accepts over-the-air style updates into the *other* slot over UART,
 *   - optionally (DEMO_FAULT=1) simulates a broken release that hangs before
 *     confirming, to demonstrate watchdog-driven rollback.
 */
#include "board.h"
#include "bootstate.h"
#include "console.h"
#include "image.h"
#include "layout.h"
#include "update.h"

#ifndef APP_SLOT
#error "APP_SLOT must be 0 (A) or 1 (B)"
#endif
#ifndef DEMO_FAULT
#define DEMO_FAULT 0
#endif

#define SELFTEST_MS 5000u

static const image_header_t *my_header(void)
{
    return (const image_header_t *)slot_addr(APP_SLOT);
}

static int on_trial(void)
{
    boot_state_t st;
    return bs_load(&st) && st.active_slot == APP_SLOT && st.state == BS_PENDING;
}

static void print_version(const image_header_t *h)
{
    con_printf("v%u.%u.%u", VER_MAJOR(h->version), VER_MINOR(h->version), VER_PATCH(h->version));
}

static void cmd_info(void)
{
    boot_state_t st;
    uint32_t min_sc = 0;
    if (bs_load(&st)) {
        con_printf("boot state: active=%c %s attempts=%u min_counter=%u seq=%u\n",
                   slot_name(st.active_slot), st.state == BS_PENDING ? "PENDING" : "CONFIRMED",
                   st.attempts, (unsigned)st.min_security_counter, (unsigned)st.seq);
        min_sc = st.min_security_counter;
    }
    for (unsigned s = 0; s < NUM_SLOTS; s++) {
        image_header_t h;
        uint32_t rc = image_verify(s, min_sc, &h);
        con_printf("slot %c%s: ", slot_name(s), s == APP_SLOT ? " (running)" : "");
        if (rc == IMG_OK) {
            print_version(&h);
            con_printf("  counter %u  authentic\n", (unsigned)h.security_counter);
        } else {
            con_printf("%s\n", image_strerror(rc));
        }
    }
}

static void cmd_confirm(void)
{
    int rc = bs_confirm(APP_SLOT, my_header()->security_counter);
    if (rc == 1)
        con_printf("image confirmed - anti-rollback floor is now %u\n",
                   (unsigned)my_header()->security_counter);
    else if (rc == 0)
        con_puts("already confirmed\n");
    else
        con_printf("confirm failed (%d)\n", rc);
}

static void cmd_update(void)
{
    const unsigned target = APP_SLOT ^ 1u;
    boot_state_t st;

    if (on_trial() || !bs_load(&st)) {
        /* Overwriting the other slot now would destroy our only fallback. */
        update_refuse();
        con_puts("\nupdate refused: confirm the running image first ('c')\n");
        return;
    }

    image_header_t h;
    uint32_t rc = update_session(target, st.min_security_counter, &h);
    board_delay_ms(20);
    if (rc != IMG_OK) {
        con_printf("\nupdate failed: %s\n", image_strerror(rc));
        return;
    }
    con_printf("\nslot %c now holds ", slot_name(target));
    print_version(&h);
    con_puts(" (signature verified). Rebooting into it as a trial image...\n");
    if (bs_request_trial(target, 0) != 0) {
        con_puts("failed to write boot state\n");
        return;
    }
    system_reset();
}

static void help(void)
{
    con_puts("commands: i=info  c=confirm now  r=reboot  h=help  (updates: tools/update.py)\n");
}

int main(void)
{
    board_init();
    iwdg_kick();
    const char *cause = reset_cause(1);
    const image_header_t *h = my_header();
    const enum led led = APP_SLOT == 0 ? LED_GREEN : LED_BLUE;

    con_puts("\n----------------------------------------------\n");
    con_printf(" Demo application ");
    print_version(h);
    con_printf("  running from slot %c\n", slot_name(APP_SLOT));
    con_printf(" security counter %u, reset cause: %s\n", (unsigned)h->security_counter, cause);
    con_puts("----------------------------------------------\n");

    int trial = on_trial();
    if (trial)
        con_printf("trial boot: will confirm after a %u ms self-test\n", SELFTEST_MS);

#if DEMO_FAULT
    con_puts("!!! DEMO_FAULT build: simulating a broken release that hangs during\n");
    con_puts("!!! start-up. The watchdog will reset the board; after 3 failed trial\n");
    con_puts("!!! boots the bootloader rolls back to the previous slot.\n");
    led_set(LED_RED, 1);
    for (;;)
        ;   /* no watchdog kicks */
#endif

    help();

    uint32_t last_blink = 0;
    for (;;) {
        iwdg_kick();

        uint32_t now = board_millis();
        if (now - last_blink >= (trial ? 100u : 500u)) {
            last_blink = now;
            led_toggle(led);
        }

        if (trial && now >= SELFTEST_MS) {
            con_puts("self-test passed\n");
            cmd_confirm();
            trial = 0;
        }

        int c = uart_getc(0);
        if (c < 0)
            continue;
        if (update_detect(c)) {
            cmd_update();
            trial = on_trial();
            continue;
        }
        switch (c) {
        case 'i': cmd_info(); break;
        case 'c': cmd_confirm(); trial = on_trial(); break;
        case 'r': con_puts("rebooting\n"); system_reset();
        case 'h': case '?': help(); break;
        default: break;
        }
    }
}
