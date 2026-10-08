#include "board.h"
#include "console.h"
#include "stm32f429.h"

#define LED_PORT   GPIOB_BASE
#define BTN_PORT   GPIOC_BASE
#define BTN_PIN    13
#define UART       USART3_BASE

static const uint8_t led_pins[] = { 0, 7, 14 };   /* LD1 green, LD2 blue, LD3 red */
static volatile uint32_t ticks;
static int last_reset_wdg;

void SysTick_Handler(void)
{
    ticks++;
}

static void gpio_mode(uint32_t port, unsigned pin, unsigned mode)
{
    GPIO_MODER(port) = (GPIO_MODER(port) & ~(3u << (2 * pin))) | (mode << (2 * pin));
}

static void gpio_af(uint32_t port, unsigned pin, unsigned af)
{
    volatile uint32_t *afr = pin < 8 ? &GPIO_AFRL(port) : &GPIO_AFRH(port);
    unsigned sh = 4 * (pin & 7);
    *afr = (*afr & ~(0xFu << sh)) | (af << sh);
    gpio_mode(port, pin, 2);
}

void board_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN;
    RCC_APB1ENR |= RCC_APB1ENR_USART3EN;
    (void)RCC_APB1ENR;

    for (unsigned i = 0; i < sizeof(led_pins); i++) {
        GPIO_BSRR(LED_PORT) = 1u << (led_pins[i] + 16);
        gpio_mode(LED_PORT, led_pins[i], 1);
    }
    gpio_mode(BTN_PORT, BTN_PIN, 0);   /* board has an external pull-down */

    gpio_af(GPIOD_BASE, 8, 7);         /* USART3_TX */
    gpio_af(GPIOD_BASE, 9, 7);         /* USART3_RX */
    GPIO_PUPDR(GPIOD_BASE) = (GPIO_PUPDR(GPIOD_BASE) & ~(3u << 18)) | (1u << 18);
    USART_BRR(UART) = (CPU_HZ + 115200u / 2) / 115200u;
    USART_CR1(UART) = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;

    SYSTICK_LOAD = CPU_HZ / 1000u - 1u;
    SYSTICK_VAL = 0;
    SYSTICK_CTRL = 7;                  /* core clock, interrupt, enable */

    /* Stop the watchdog while a debugger has the core halted. */
    DBGMCU_APB1_FZ |= DBGMCU_APB1_FZ_IWDG_STOP;
}

void board_deinit(void)
{
    uart_flush();
    SYSTICK_CTRL = 0;
    SCB_ICSR = 1u << 25;               /* PENDSTCLR */
    RCC_APB1RSTR |= RCC_APB1RSTR_USART3;
    RCC_APB1RSTR &= ~RCC_APB1RSTR_USART3;
    RCC_APB1ENR &= ~RCC_APB1ENR_USART3EN;
    for (unsigned i = 0; i < sizeof(led_pins); i++)
        GPIO_BSRR(LED_PORT) = 1u << (led_pins[i] + 16);
}

uint32_t board_millis(void)
{
    return ticks;
}

void board_delay_ms(uint32_t ms)
{
    uint32_t t0 = ticks;
    while (ticks - t0 < ms)
        ;
}

void led_set(enum led led, int on)
{
    unsigned pin = led_pins[led];
    GPIO_BSRR(LED_PORT) = on ? (1u << pin) : (1u << (pin + 16));
}

void led_toggle(enum led led)
{
    GPIO_ODR(LED_PORT) ^= 1u << led_pins[led];
}

int button_pressed(void)
{
    return (GPIO_IDR(BTN_PORT) >> BTN_PIN) & 1u;
}

int uart_getc(uint32_t timeout_ms)
{
    uint32_t t0 = ticks;
    for (;;) {
        uint32_t sr = USART_SR(UART);
        if (sr & USART_SR_RXNE)
            return (int)(USART_DR(UART) & 0xFF);
        if (sr & USART_SR_ORE)
            (void)USART_DR(UART);       /* SR-then-DR read clears overrun */
        if (ticks - t0 >= timeout_ms)
            return -1;
    }
}

int uart_read(void *buf, uint32_t len, uint32_t timeout_ms)
{
    uint8_t *p = buf;
    while (len--) {
        int c = uart_getc(timeout_ms);
        if (c < 0)
            return -1;
        *p++ = (uint8_t)c;
    }
    return 0;
}

void uart_write(const void *buf, uint32_t len)
{
    const uint8_t *p = buf;
    while (len--) {
        while (!(USART_SR(UART) & USART_SR_TXE))
            ;
        USART_DR(UART) = *p++;
    }
}

void uart_flush(void)
{
    while (!(USART_SR(UART) & USART_SR_TC))
        ;
}

void con_putc(char c)
{
    uart_write(&c, 1);
}

void iwdg_start(void)
{
    IWDG_KR = 0xCCCC;          /* start (irreversible until reset) */
    IWDG_KR = 0x5555;          /* unlock PR/RLR */
    IWDG_PR = 4;               /* LSI(~32 kHz)/64 = 500 Hz */
    IWDG_RLR = 4000;           /* ~8 s: covers worst-case 128K sector erase */
    while (IWDG_SR & 3u)
        ;
    IWDG_KR = 0xAAAA;
}

void iwdg_kick(void)
{
    IWDG_KR = 0xAAAA;
}

const char *reset_cause(int clear)
{
    uint32_t csr = RCC_CSR;
    const char *s;

    if (clear)
        RCC_CSR |= RCC_CSR_RMVF;
    last_reset_wdg = (csr & (RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF)) != 0;
    if (csr & RCC_CSR_IWDGRSTF)
        s = "independent watchdog";
    else if (csr & RCC_CSR_WWDGRSTF)
        s = "window watchdog";
    else if (csr & RCC_CSR_LPWRRSTF)
        s = "low-power";
    else if (csr & RCC_CSR_SFTRSTF)
        s = "software";
    else if (csr & RCC_CSR_PORRSTF)
        s = "power-on";
    else if (csr & RCC_CSR_BORRSTF)
        s = "brown-out";
    else if (csr & RCC_CSR_PINRSTF)
        s = "reset pin";
    else
        s = "unknown";
    return s;
}

int reset_was_watchdog(void)
{
    return last_reset_wdg;
}

void system_reset(void)
{
    uart_flush();
    __asm volatile("dsb" ::: "memory");
    SCB_AIRCR = (0x5FAu << 16) | (1u << 2);   /* SYSRESETREQ */
    __asm volatile("dsb" ::: "memory");
    for (;;)
        ;
}
