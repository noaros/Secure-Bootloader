/* Nucleo-F429ZI board support: LEDs, user button, ST-LINK VCP UART, IWDG. */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#define CPU_HZ 16000000u   /* HSI, the reset default; this demo never changes it */

enum led { LED_GREEN, LED_BLUE, LED_RED };

void board_init(void);
void board_deinit(void);   /* undo board_init before handing over to an image */
uint32_t board_millis(void);
void board_delay_ms(uint32_t ms);

void led_set(enum led led, int on);
void led_toggle(enum led led);
int button_pressed(void);

int uart_getc(uint32_t timeout_ms);   /* -1 on timeout */
int uart_read(void *buf, uint32_t len, uint32_t timeout_ms);  /* 0 ok, -1 timeout */
void uart_write(const void *buf, uint32_t len);
void uart_flush(void);

void iwdg_start(void);                 /* ~8 s timeout; cannot be stopped */
void iwdg_kick(void);

const char *reset_cause(int clear);    /* reads (and optionally clears) RCC_CSR flags */
int reset_was_watchdog(void);          /* valid after reset_cause() */
void system_reset(void) __attribute__((noreturn));

#endif
