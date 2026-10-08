/*
 * Tiny console layer. con_putc() is supplied by the platform (board.c on
 * target, the test harness on host); everything else is portable.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

void con_putc(char c);
void con_puts(const char *s);
/* Supports %s %c %d %u %x %08x-style zero padding/width, and %%. */
void con_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void con_hex(const uint8_t *p, uint32_t len);

#endif
