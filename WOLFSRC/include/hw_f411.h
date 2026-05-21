#pragma once
#include <stdint.h>
#include "stm32f4xx.h" // The official CMSIS header!

/* ═══════════════════════════════════════════════════════════════════════════
 * DISPLAY GEOMETRY
 * ═══════════════════════════════════════════════════════════════════════════ */
#ifndef TFT_WIDTH
#  define TFT_WIDTH  320
#  define TFT_HEIGHT 240
#endif

extern uint16_t dma_tft_buffer[TFT_WIDTH * TFT_HEIGHT];

/* ═══════════════════════════════════════════════════════════════════════════
 * SYSTICK
 * ═══════════════════════════════════════════════════════════════════════════ */
volatile uint32_t systick_ms = 0;

static inline void systick_init(void) {
    SysTick->LOAD = 100000UL - 1UL;
    SysTick->VAL  = 0UL;
    SysTick->CTRL = 0x00000007UL;
}

static inline void delay_ms(uint32_t ms) {
    uint32_t start = systick_ms;
    while ((systick_ms - start) < ms);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * RCC & GPIO (100MHz)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void rcc_init(void) {
    RCC->APB1ENR |= (1UL << 28);
    PWR->CR |= (2UL << 14);

    FLASH->ACR = 0x00000003UL | (1UL<<8) | (1UL<<9) | (1UL<<10);
    while ((FLASH->ACR & 0xFUL) != 0x00000003UL);

    RCC->CR |= (1UL << 16);
    while (!(RCC->CR & (1UL << 17)));

    RCC->PLLCFGR = (25UL << 0) | (400UL << 6) | (1UL << 16) | (1UL << 22) | (4UL << 24);
    
    RCC->CFGR = (0b000UL << 4) | (0b100UL << 10) | (0b000UL << 13);

    RCC->CR |= (1UL << 24);
    while (!(RCC->CR & (1UL << 25)));

    RCC->CFGR |= (2UL << 0);
    while ((RCC->CFGR & (3UL << 2)) != (2UL << 2));
}

#define LCD_CS_LOW()   (GPIOA->BSRR = (1UL << (4 + 16)))
#define LCD_CS_HIGH()  (GPIOA->BSRR = (1UL <<  4))
#define LCD_DC_LOW()   (GPIOA->BSRR = (1UL << (3 + 16))) 
#define LCD_DC_HIGH()  (GPIOA->BSRR = (1UL <<  3))       
#define LCD_RST_LOW()  (GPIOA->BSRR = (1UL << (2 + 16)))
#define LCD_RST_HIGH() (GPIOA->BSRR = (1UL <<  2))

static void gpio_init(void) {
    RCC->AHB1ENR |= (1UL << 0);
    (void)RCC->AHB1ENR;

    GPIOA->MODER &= ~((3UL << 4) | (3UL << 6) | (3UL << 8) | (3UL << 10) | (3UL << 14));
    GPIOA->MODER |=  ((1UL << 4) | (1UL << 6) | (1UL << 8) | (2UL << 10) | (2UL << 14));
    GPIOA->OSPEEDR |= ((3UL << 4) | (3UL << 6) | (3UL << 8) | (3UL << 10) | (3UL << 14));
    
    GPIOA->AFR[0] &= ~((0xFUL << 20) | (0xFUL << 28));
    GPIOA->AFR[0] |=  ((5UL   << 20) | (5UL   << 28));

    LCD_CS_HIGH();
    LCD_DC_HIGH();
    LCD_RST_HIGH();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SPI1 & DMA2
 * ═══════════════════════════════════════════════════════════════════════════ */
static void spi_init(void) {
    RCC->APB2ENR |= (1UL << 12);
    (void)RCC->APB2ENR;

    SPI1->CR1 = (1UL << 9) | (1UL << 8) | (1UL << 2) | (0UL << 3);
    SPI1->CR2 = (1UL << 1);
    SPI1->CR1 |= (1UL << 6);
}

volatile uint8_t dma_transfer_done = 1;

static void dma_init(void) {
    RCC->AHB1ENR |= (1UL << 22);
    (void)RCC->AHB1ENR;

    DMA2_Stream3->CR &= ~(1UL << 0);
    while (DMA2_Stream3->CR & (1UL << 0));
    DMA2->LIFCR = (0x3FUL << 22);

    DMA2_Stream3->PAR  = (uint32_t)(&SPI1->DR);
    DMA2_Stream3->M0AR = (uint32_t)(dma_tft_buffer);
    DMA2_Stream3->CR = (3UL << 25) | (3UL << 16) | (0UL << 13) | (0UL << 11) | (1UL << 10) | (0UL << 9) | (0UL << 8) | (1UL << 6) | (1UL << 4);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ST7789 INIT & DRIVER
 * ═══════════════════════════════════════════════════════════════════════════ */
static inline void spi_send_byte(uint8_t b) {
    while (!(SPI1->SR & (1UL << 1)));
    *((volatile uint8_t *)&SPI1->DR) = b;
}

static inline void spi_wait_idle(void) {
    while (SPI1->SR & (1UL << 7));
}

static inline void lcd_cmd(uint8_t cmd) {
    LCD_DC_LOW(); LCD_CS_LOW();
    spi_send_byte(cmd); spi_wait_idle();
    LCD_CS_HIGH(); LCD_DC_HIGH();
}

static inline void lcd_data(uint8_t dat) {
    LCD_CS_LOW(); spi_send_byte(dat); spi_wait_idle(); LCD_CS_HIGH();
}

static inline void lcd_data16(uint16_t dat) {
    LCD_CS_LOW();
    spi_send_byte(dat >> 8); spi_send_byte(dat & 0xFF);
    spi_wait_idle(); LCD_CS_HIGH();
}

static void st7789_init(void) {
    LCD_RST_LOW(); delay_ms(50);
    LCD_RST_HIGH(); delay_ms(120);

    lcd_cmd(0x01); delay_ms(150);
    lcd_cmd(0x11); delay_ms(50);
    
    lcd_cmd(0x36); lcd_data(0x60);
    lcd_cmd(0x3A); lcd_data(0x55);
    
    lcd_cmd(0xB2); lcd_data(0x0C); lcd_data(0x0C); lcd_data(0x00); lcd_data(0x33); lcd_data(0x33);
    lcd_cmd(0xB7); lcd_data(0x35);
    lcd_cmd(0xBB); lcd_data(0x19);
    lcd_cmd(0xC0); lcd_data(0x2C);
    lcd_cmd(0xC2); lcd_data(0x01);
    lcd_cmd(0xC3); lcd_data(0x12);
    lcd_cmd(0xC4); lcd_data(0x20);
    lcd_cmd(0xC6); lcd_data(0x0F);
    lcd_cmd(0xD0); lcd_data(0xA4); lcd_data(0xA1);
    
    lcd_cmd(0x21); // IPS Inversion ON
    lcd_cmd(0x13); delay_ms(10);
    lcd_cmd(0x29); delay_ms(10);
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    lcd_cmd(0x2A); lcd_data16(x0); lcd_data16(x1);
    lcd_cmd(0x2B); lcd_data16(y0); lcd_data16(y1);
    lcd_cmd(0x2C); 
}

#define FRAME_BYTES  ((uint32_t)(TFT_WIDTH) * TFT_HEIGHT * 2UL)
#define DMA_MAX_NDTR 65535UL

static void lcd_dma_send_chunk(uint8_t *src, uint32_t len) {
    while (!dma_transfer_done);
    dma_transfer_done = 0;

    DMA2_Stream3->CR &= ~(1UL << 0);
    while (DMA2_Stream3->CR & (1UL << 0));
    DMA2->LIFCR = (0x3FUL << 22);

    DMA2_Stream3->M0AR = (uint32_t)src;
    DMA2_Stream3->NDTR = (uint16_t)len;

    LCD_CS_LOW();
    DMA2_Stream3->CR |= (1UL << 0);
}

static void lcd_dma_send_frame(void) {
    uint8_t *buf = (uint8_t *)dma_tft_buffer;
    lcd_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    LCD_DC_HIGH();

    uint32_t chunkA = DMA_MAX_NDTR;
    uint32_t chunkB = FRAME_BYTES - chunkA;

    lcd_dma_send_chunk(buf, chunkA);
    while (!dma_transfer_done); 
    LCD_CS_LOW();
    lcd_dma_send_chunk(buf + chunkA, chunkB);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * BUTTONS
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct {
    uint8_t forward, backward, left, right, action;
} ButtonState;

static void hw_buttons_init(void) {
    RCC->AHB1ENR |= (1UL << 1);
    (void)RCC->AHB1ENR;
    GPIOB->MODER &= ~(0x3FFUL << 0);
    GPIOB->PUPDR &= ~(0x3FFUL << 0);
    GPIOB->PUPDR |=   0x155UL;
}

static ButtonState hw_buttons_read(void) {
    uint32_t idr = GPIOB->IDR;
    ButtonState b;
    b.forward  = !(idr & (1UL << 0));
    b.backward = !(idr & (1UL << 1));
    b.left     = !(idr & (1UL << 2));
    b.right    = !(idr & (1UL << 3));
    b.action   = !(idr & (1UL << 4));
    return b;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TOP-LEVEL INIT
 * ═══════════════════════════════════════════════════════════════════════════ */
static void hw_init(void) {
    rcc_init();
    systick_init();
    gpio_init();
    spi_init();
    dma_init();
    
    // Enable DMA2_Stream3 interrupt (IRQ 59)
    *((volatile uint32_t *)0xE000E104UL) |= (1UL << 27);

    st7789_init();
    hw_buttons_init();
}
