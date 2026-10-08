#include "console.h"
#include <stdarg.h>

void con_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            con_putc('\r');
        con_putc(*s++);
    }
}

static void put_num(uint32_t v, unsigned base, int width, char padc, int neg)
{
    char buf[12];
    int n = 0;
    do {
        unsigned d = v % base;
        buf[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= base;
    } while (v);
    if (neg)
        buf[n++] = '-';
    for (int i = n; i < width; i++)
        con_putc(padc);
    while (n)
        con_putc(buf[--n]);
}

void con_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            if (*fmt == '\n')
                con_putc('\r');
            con_putc(*fmt);
            continue;
        }
        fmt++;
        char padc = ' ';
        int width = 0;
        if (*fmt == '0') {
            padc = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        switch (*fmt) {
        case 's': con_puts(va_arg(ap, const char *)); break;
        case 'c': con_putc((char)va_arg(ap, int)); break;
        case 'u': put_num(va_arg(ap, unsigned), 10, width, padc, 0); break;
        case 'x': put_num(va_arg(ap, unsigned), 16, width, padc, 0); break;
        case 'd': {
            int v = va_arg(ap, int);
            put_num(v < 0 ? 0u - (unsigned)v : (unsigned)v, 10, width, padc, v < 0);
            break;
        }
        case '%': con_putc('%'); break;
        case '\0': fmt--; break;
        default: con_putc('%'); con_putc(*fmt); break;
        }
    }
    va_end(ap);
}

void con_hex(const uint8_t *p, uint32_t len)
{
    static const char hx[] = "0123456789abcdef";
    for (uint32_t i = 0; i < len; i++) {
        con_putc(hx[p[i] >> 4]);
        con_putc(hx[p[i] & 15]);
    }
}
