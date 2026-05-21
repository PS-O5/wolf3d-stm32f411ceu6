#include "include/display.h"

/* --- Register Base Offsets --- */
#define RCC_BASE        0x40023800
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x44))

#define GPIOA_BASE      0x40020000
#define GPIOA_MODER     (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_AFRL      (*(volatile uint32_t *)(GPIOA_BASE + 0x20))
#define GPIOA_BSRR      (*(volatile uint32_t *)(GPIOA_BASE + 0x18))

#define GPIOA_OSPEEDR   (*(volatile uint32_t *)(GPIOA_BASE + 0x08))
#define GPIOB_OSPEEDR   (*(volatile uint32_t *)(GPIOB_BASE + 0x08))

#define GPIOB_BASE      0x40020400
#define GPIOB_MODER     (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_BSRR      (*(volatile uint32_t *)(GPIOB_BASE + 0x18))

#define SPI1_BASE       0x40013000
#define SPI1_CR1        (*(volatile uint32_t *)(SPI1_BASE + 0x00))
#define SPI1_CR2        (*(volatile uint32_t *)(SPI1_BASE + 0x04))
#define SPI1_SR         (*(volatile uint32_t *)(SPI1_BASE + 0x08))
#define SPI1_DR         (*(volatile uint32_t *)(SPI1_BASE + 0x0C))

#define DMA2_BASE       0x40026400
#define DMA2_LISR       (*(volatile uint32_t *)(DMA2_BASE + 0x00))
#define DMA2_LIFCR      (*(volatile uint32_t *)(DMA2_BASE + 0x08))
#define DMA2_S3CR       (*(volatile uint32_t *)(DMA2_BASE + 0x58))
#define DMA2_S3NDTR     (*(volatile uint32_t *)(DMA2_BASE + 0x5C))
#define DMA2_S3PAR      (*(volatile uint32_t *)(DMA2_BASE + 0x60))
#define DMA2_S3M0AR     (*(volatile uint32_t *)(DMA2_BASE + 0x64))

/* Pin Control Macros */
#define CS_LOW()        GPIOA_BSRR = (1 << (4 + 16))
#define CS_HIGH()       GPIOA_BSRR = (1 << 4)
#define DC_CMD()        GPIOA_BSRR = (1 << (3 + 16))
#define DC_DATA()       GPIOA_BSRR = (1 << 3)
#define RST_LOW()       GPIOB_BSRR = (1 << (2 + 16))
#define RST_HIGH()      GPIOB_BSRR = (1 << 2)

extern void delay_ms(uint32_t ms);

/* Two 640-byte line buffers */
uint16_t dma_line_buf[2][TFT_WIDTH];

/* SINGLE Line Buffer */
//uint16_t dma_line_buf[TFT_WIDTH];

/* --- Low Level SPI Helpers --- */
static void spi_wait_idle(void) {
    while (SPI1_SR & (1 << 7));
}

static void set_spi_8bit(void) {
    spi_wait_idle();
    SPI1_CR1 &= ~(1 << 6);
    SPI1_CR1 &= ~(1 << 11);
    SPI1_CR1 |=  (1 << 6);
}

static void set_spi_16bit(void) {
    spi_wait_idle();
    SPI1_CR1 &= ~(1 << 6);
    SPI1_CR1 |=  (1 << 11);
    SPI1_CR1 |=  (1 << 6);
}

static void spi_tx_byte(uint8_t data) {
    while (!(SPI1_SR & (1 << 1)));
    *(volatile uint8_t *)&SPI1_DR = data;
    while (!(SPI1_SR & (1 << 1)));
}

static void spi_tx_cmd(uint8_t cmd) {
    DC_CMD();
    CS_LOW();
    spi_tx_byte(cmd);
    spi_wait_idle();
    CS_HIGH();
}

static void spi_tx_data(uint8_t data) {
    DC_DATA();
    CS_LOW();
    spi_tx_byte(data);
    spi_wait_idle();
    CS_HIGH();
}

/* --- Display Driver API --- */
void display_init(void) {
    RCC_AHB1ENR |= (1 << 0) | (1 << 1); 
    RCC_APB2ENR |= (1 << 12);           
    RCC_AHB1ENR |= (1 << 22);           

    // PA5 (SCK), PA7 (MOSI) -> AF5
    GPIOA_MODER &= ~((3 << (5 * 2)) | (3 << (7 * 2)));
    GPIOA_MODER |=  ((2 << (5 * 2)) | (2 << (7 * 2)));
    GPIOA_AFRL  &= ~((0xF << (5 * 4)) | (0xF << (7 * 4)));
    GPIOA_AFRL  |=  ((0x5 << (5 * 4)) | (0x5 << (7 * 4)));

    // PA4 (CS), PA3 (DC) -> Output
    GPIOA_MODER &= ~((3 << (4 * 2)) | (3 << (3 * 2)));
    GPIOA_MODER |=  ((1 << (4 * 2)) | (1 << (3 * 2)));

    // PB2 (RST) -> Output
    GPIOB_MODER &= ~(3 << (2 * 2));
    GPIOB_MODER |=  (1 << (2 * 2));

    
    // Set PA3, PA4, PA5, PA7 to Very High Speed(48MHz) (11)
    GPIOA_OSPEEDR |= (3 << (3 * 2)) | (3 << (4 * 2)) | (3 << (5 * 2)) | (3 << (7 * 2));
    // Set PB2 to Very High Speed
    GPIOB_OSPEEDR |= (3 << (2 * 2));


    // SPI1 Config (Master, /16 Baud = ~1MHz initially)
    //SPI1_CR1 = (1 << 2) | (1 << 9) | (1 << 8) | (3 << 3); 
    // SPI1 Config (Master, /2 Baud = Max Speed ~50MHz)
    // BR bits (5:3) set to 000 for fPCLK/2
    SPI1_CR1 = (1 << 2) | (1 << 9) | (1 << 8) | (0 << 3);
    SPI1_CR2 = (1 << 1); 
    SPI1_CR1 |= (1 << 6); 

    // DMA2 Stream 3 Config
    DMA2_S3CR = 0;
    while (DMA2_S3CR & 1); 
    DMA2_S3PAR = (uint32_t)&SPI1_DR;
    DMA2_S3CR = (3 << 25) | (1 << 16) | (1 << 13) | (1 << 11) | (1 << 10) | (1 << 6);

    CS_HIGH();
    RST_LOW();
    delay_ms(50);
    RST_HIGH();
    delay_ms(150);

    set_spi_8bit();
    spi_tx_cmd(0x01); delay_ms(150);
    spi_tx_cmd(0x11); delay_ms(500);
    spi_tx_cmd(0x3A); spi_tx_data(0x55);
    spi_tx_cmd(0x36); spi_tx_data(0x60); 
    //spi_tx_cmd(0x21); //INVON (Inverts colours) 
    spi_tx_cmd(0x20);   //INVOFF
    spi_tx_cmd(0x29); 
}


/* --- DMA Upscale 2x line Pipeline --- */
static inline void start_dma_transfer(uint16_t *buffer) {
    DMA2_S3CR &= ~1; 
    while (DMA2_S3CR & 1); 
    DMA2_LIFCR = (1 << 27) | (1 << 26) | (1 << 25) | (1 << 24); 
    DMA2_S3M0AR = (uint32_t)buffer;
    DMA2_S3NDTR = TFT_WIDTH;
    DMA2_S3CR |= 1;  
}

static inline void wait_dma_complete(void) {
    while (!(DMA2_LISR & (1 << 27))); 
}

static inline void convert_row_2x(uint8_t *src_row, uint16_t *dst_buf, uint16_t *palette) {
    for (int x = 0; x < RENDER_WIDTH; x++) {
        uint16_t color = palette[src_row[x]];
        dst_buf[x * 2]     = color;
        dst_buf[x * 2 + 1] = color;
    }
}

void display_push_frame(uint8_t *framebuffer, uint16_t *palette) {
    set_spi_8bit();
    spi_tx_cmd(0x2A); 
    spi_tx_data(0); spi_tx_data(0);
    spi_tx_data(319 >> 8); spi_tx_data(319 & 0xFF);

    spi_tx_cmd(0x2B); 
    spi_tx_data(0); spi_tx_data(0);
    spi_tx_data(239 >> 8); spi_tx_data(239 & 0xFF);

    spi_tx_cmd(0x2C); 
    DC_DATA();
    CS_LOW();
    set_spi_16bit(); 

    int active_buf = 0;
    convert_row_2x(&framebuffer[0], dma_line_buf[active_buf], palette);

    for (int row = 0; row < RENDER_HEIGHT; row++) {
        int idle_buf = active_buf ^ 1;

        // Sent line part 1
        start_dma_transfer(dma_line_buf[active_buf]);
        if (row < RENDER_HEIGHT - 1) {
            convert_row_2x(&framebuffer[(row + 1) * RENDER_WIDTH], dma_line_buf[idle_buf], palette);
        }
        wait_dma_complete();

        // Sent line part 2
        start_dma_transfer(dma_line_buf[active_buf]);
        wait_dma_complete();

        active_buf = idle_buf;
    }

    spi_wait_idle();
    CS_HIGH();
    set_spi_8bit(); 
}



/* --- Synchronous 1x line DMA Pipeline --- 
static inline void start_dma_transfer(uint16_t *buffer) {
    DMA2_S3CR &= ~1; 
    while (DMA2_S3CR & 1); 
    DMA2_LIFCR = (1 << 27) | (1 << 26) | (1 << 25) | (1 << 24); 
    DMA2_S3M0AR = (uint32_t)buffer;
    DMA2_S3NDTR = TFT_WIDTH;
    DMA2_S3CR |= 1;  
}

static inline void wait_dma_complete(void) {
    while (!(DMA2_LISR & (1 << 27))); 
}

static inline void convert_row_2x(uint8_t *src_row, uint16_t *dst_buf, uint16_t *palette) {
    for (int x = 0; x < RENDER_WIDTH; x++) {
        uint16_t color = palette[src_row[x]];
        dst_buf[x * 2]     = color;
        dst_buf[x * 2 + 1] = color;
    }
}

void display_push_frame(uint8_t *framebuffer, uint16_t *palette) {
    set_spi_8bit();
    spi_tx_cmd(0x2A); 
    spi_tx_data(0); spi_tx_data(0);
    spi_tx_data(319 >> 8); spi_tx_data(319 & 0xFF);

    spi_tx_cmd(0x2B); 
    spi_tx_data(0); spi_tx_data(0);
    spi_tx_data(239 >> 8); spi_tx_data(239 & 0xFF);

    spi_tx_cmd(0x2C); 
    DC_DATA();
    CS_LOW();
    set_spi_16bit(); 

    // Bulletproof Synchronous Loop
    for (int row = 0; row < RENDER_HEIGHT; row++) {
        // 1. CPU strictly fills the buffer
        convert_row_2x(&framebuffer[row * RENDER_WIDTH], dma_line_buf, palette);

        // 2. Start DMA for first vertical line
        start_dma_transfer(dma_line_buf);
        
        // 3. CPU Halts and waits for DMA to finish
        wait_dma_complete();

        // 4. Start DMA again for second vertical line (the 2x scale)
        start_dma_transfer(dma_line_buf);
        
        // 5. CPU Halts again
        wait_dma_complete();
    }

    spi_wait_idle();
    CS_HIGH();
    set_spi_8bit(); 
}
*/
