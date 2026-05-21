#include <stdint.h>

/* Register Base Offsets */
#define RCC_BASE      0x40023800
#define GPIOA_BASE    0x40020000
#define SYSTICK_BASE  0xE000E010
#define SCB_BASE      0xE000ED00

/* RCC Peripheral Registers */
#define RCC_AHB1ENR   (*(volatile uint32_t *)(RCC_BASE + 0x30))

/* GPIOA Configuration Registers */
#define GPIOA_MODER   (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_ODR     (*(volatile uint32_t *)(GPIOA_BASE + 0x14))
#define GPIOA_BSRR    (*(volatile uint32_t *)(GPIOA_BASE + 0x18))

/* Core System Timer Registers */
#define STK_CTRL      (*(volatile uint32_t *)(SYSTICK_BASE + 0x00))
#define STK_LOAD      (*(volatile uint32_t *)(SYSTICK_BASE + 0x04))
#define STK_VAL       (*(volatile uint32_t *)(SYSTICK_BASE + 0x08))

/* System Control Block Registers (VTOR) */
#define SCB_VTOR      (*(volatile uint32_t *)(SCB_BASE + 0x08))

/* Global System Metric */
static volatile uint32_t ms_ticks = 0;

/* Interrupt Service Vector Routine for SysTick clock tick tracking */
void SysTick_Handler(void) {
    ms_ticks++;
}

/* Hardened Polling delay execution wrapper */
void delay_ms(uint32_t ms) {
    uint32_t start_tick = ms_ticks;
    while ((ms_ticks - start_tick) < ms) {
        __asm__("nop"); // Wait explicitly for interrupt manipulation
    }
}

int main(void) {
    /* 1. Explicitly inform the CPU where the vector table resides in Flash */
    SCB_VTOR = 0x08000000;

    /* 2. Enable Clock for GPIO Port A (Bit 0 of AHB1ENR) */
    RCC_AHB1ENR |= (1 << 0);

    /* 3. Configure Pin PA0 as General Purpose Output Mode (01) */
    GPIOA_MODER &= ~(3 << (0 * 2));
    GPIOA_MODER |=  (1 << (0 * 2));

    /* 4. Initialize SysTick system time base counter to trigger every 1ms
     * Internal HSI oscillator defaults to 16 MHz out of reset.
     * 16,000,000 Hz / 1000 = 16,000 ticks per millisecond. */
    STK_VAL  = 0;
    STK_LOAD = 16000 - 1;
    
    /* Enable SysTick: 
     * bit 0 = ENABLE
     * bit 1 = TICKINT (Enables the exception interrupt request)
     * bit 2 = CLKSOURCE (1 = Processor Clock Source Core) */
    STK_CTRL = 7;

    /* 5. Globally Enable Interrupts on the ARM Core (cpsie i) */
    __asm__ volatile ("cpsie i" : : : "memory");

    /* Infinite hardware lifecycle execution loop */
    while (1) {
        /* Atomic bit toggle via Bit Set/Reset Register (BSRR) */
        if (GPIOA_ODR & (1 << 0)) {
            GPIOA_BSRR = (1 << (0 + 16)); /* Reset Bit (Turn OFF) */
        } else {
            GPIOA_BSRR = (1 << 0);        /* Set Bit (Turn ON) */
        }

        delay_ms(500); /* Gate process at 2 Hz flashing execution loop rate */
    }

    return 0;
}
