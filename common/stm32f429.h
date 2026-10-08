/*
 * Minimal STM32F429 register definitions (RM0090) - only what this demo uses.
 * Kept self-contained so the project builds without CMSIS/HAL downloads.
 */
#ifndef STM32F429_H
#define STM32F429_H

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(addr))

/* ---- RCC ---- */
#define RCC_BASE        0x40023800u
#define RCC_CR          REG32(RCC_BASE + 0x00)
#define RCC_CFGR        REG32(RCC_BASE + 0x08)
#define RCC_AHB1ENR     REG32(RCC_BASE + 0x30)
#define RCC_APB1RSTR    REG32(RCC_BASE + 0x20)
#define RCC_APB1ENR     REG32(RCC_BASE + 0x40)
#define RCC_CSR         REG32(RCC_BASE + 0x74)

#define RCC_AHB1ENR_GPIOBEN  (1u << 1)
#define RCC_AHB1ENR_GPIOCEN  (1u << 2)
#define RCC_AHB1ENR_GPIODEN  (1u << 3)
#define RCC_APB1ENR_USART3EN (1u << 18)
#define RCC_APB1RSTR_USART3  (1u << 18)

#define RCC_CSR_RMVF     (1u << 24)
#define RCC_CSR_BORRSTF  (1u << 25)
#define RCC_CSR_PINRSTF  (1u << 26)
#define RCC_CSR_PORRSTF  (1u << 27)
#define RCC_CSR_SFTRSTF  (1u << 28)
#define RCC_CSR_IWDGRSTF (1u << 29)
#define RCC_CSR_WWDGRSTF (1u << 30)
#define RCC_CSR_LPWRRSTF (1u << 31)

/* ---- GPIO ---- */
#define GPIOB_BASE      0x40020400u
#define GPIOC_BASE      0x40020800u
#define GPIOD_BASE      0x40020C00u
#define GPIO_MODER(b)   REG32((b) + 0x00)
#define GPIO_OSPEEDR(b) REG32((b) + 0x08)
#define GPIO_PUPDR(b)   REG32((b) + 0x0C)
#define GPIO_IDR(b)     REG32((b) + 0x10)
#define GPIO_ODR(b)     REG32((b) + 0x14)
#define GPIO_BSRR(b)    REG32((b) + 0x18)
#define GPIO_AFRL(b)    REG32((b) + 0x20)
#define GPIO_AFRH(b)    REG32((b) + 0x24)

/* ---- USART3 (ST-LINK virtual COM port on Nucleo-144: PD8 TX / PD9 RX) ---- */
#define USART3_BASE     0x40004800u
#define USART_SR(b)     REG32((b) + 0x00)
#define USART_DR(b)     REG32((b) + 0x04)
#define USART_BRR(b)    REG32((b) + 0x08)
#define USART_CR1(b)    REG32((b) + 0x0C)
#define USART_SR_ORE    (1u << 3)
#define USART_SR_RXNE   (1u << 5)
#define USART_SR_TC     (1u << 6)
#define USART_SR_TXE    (1u << 7)
#define USART_CR1_RE    (1u << 2)
#define USART_CR1_TE    (1u << 3)
#define USART_CR1_UE    (1u << 13)

/* ---- FLASH interface ---- */
#define FLASH_R_BASE    0x40023C00u
#define FLASH_ACR       REG32(FLASH_R_BASE + 0x00)
#define FLASH_KEYR      REG32(FLASH_R_BASE + 0x04)
#define FLASH_SR        REG32(FLASH_R_BASE + 0x0C)
#define FLASH_CR        REG32(FLASH_R_BASE + 0x10)
#define FLASH_OPTCR     REG32(FLASH_R_BASE + 0x14)
#define FLASH_OPTCR1    REG32(FLASH_R_BASE + 0x18)

#define FLASH_KEY1      0x45670123u
#define FLASH_KEY2      0xCDEF89ABu

#define FLASH_SR_EOP    (1u << 0)
#define FLASH_SR_OPERR  (1u << 1)
#define FLASH_SR_WRPERR (1u << 4)
#define FLASH_SR_PGAERR (1u << 5)
#define FLASH_SR_PGPERR (1u << 6)
#define FLASH_SR_PGSERR (1u << 7)
#define FLASH_SR_RDERR  (1u << 8)
#define FLASH_SR_BSY    (1u << 16)
#define FLASH_SR_ERRORS (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                         FLASH_SR_PGPERR | FLASH_SR_PGSERR | FLASH_SR_RDERR)

#define FLASH_CR_PG          (1u << 0)
#define FLASH_CR_SER         (1u << 1)
#define FLASH_CR_SNB_POS     3
#define FLASH_CR_SNB_MASK    (0x1Fu << FLASH_CR_SNB_POS)
#define FLASH_CR_PSIZE_X32   (2u << 8)
#define FLASH_CR_PSIZE_MASK  (3u << 8)
#define FLASH_CR_STRT        (1u << 16)
#define FLASH_CR_LOCK        (1u << 31)

#define FLASH_ACR_ICEN  (1u << 9)
#define FLASH_ACR_DCEN  (1u << 10)
#define FLASH_ACR_ICRST (1u << 11)
#define FLASH_ACR_DCRST (1u << 12)

/* ---- IWDG ---- */
#define IWDG_BASE       0x40003000u
#define IWDG_KR         REG32(IWDG_BASE + 0x00)
#define IWDG_PR         REG32(IWDG_BASE + 0x04)
#define IWDG_RLR        REG32(IWDG_BASE + 0x08)
#define IWDG_SR         REG32(IWDG_BASE + 0x0C)

/* ---- DBGMCU ---- */
#define DBGMCU_APB1_FZ  REG32(0xE0042008u)
#define DBGMCU_APB1_FZ_IWDG_STOP (1u << 12)

/* ---- Cortex-M4 core ---- */
#define SYSTICK_CTRL    REG32(0xE000E010u)
#define SYSTICK_LOAD    REG32(0xE000E014u)
#define SYSTICK_VAL     REG32(0xE000E018u)
#define NVIC_ICER(n)    REG32(0xE000E180u + 4u * (n))
#define NVIC_ICPR(n)    REG32(0xE000E280u + 4u * (n))
#define SCB_ICSR        REG32(0xE000ED04u)
#define SCB_VTOR        REG32(0xE000ED08u)
#define SCB_AIRCR       REG32(0xE000ED0Cu)
#define SCB_SHCSR       REG32(0xE000ED24u)

/* ---- Device info ---- */
#define UID_BASE        0x1FFF7A10u

#endif
