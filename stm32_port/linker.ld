/* Entry Point */
ENTRY(Reset_Handler)

/* Memory Regions Configuration */
MEMORY
{
  FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 512K
  SRAM (xrw)      : ORIGIN = 0x20000000, LENGTH = 128K
}

/* Section Definitions */
SECTIONS
{
  /* Vector table and executable code inside Flash */
  .text :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Keep vector table from being optimized out */
    *(.text)
    *(.text*)
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
    _etext = .;         /* Global symbol marking end of raw code/rodata */
  } > FLASH

  /* Dedicated section for Asset Flash mapping */
  .textures :
  {
    . = ALIGN(4);
    *(.textures)
    *(.textures*)
    . = ALIGN(4);
  } > FLASH

  /* Used by startup to initialize data core variables in SRAM */
  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* Global symbol marking start of .data in RAM */
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;        /* Global symbol marking end of .data in RAM */
  } > SRAM AT > FLASH

  /* Uninitialized data section */
  .bss :
  {
    . = ALIGN(4);
    _sbss = .;         /* Global symbol marking start of .bss */
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;         /* Global symbol marking end of .bss */
  } > SRAM

  /* Define stack top pointer symbol at highest address of RAM boundary */
  _estack = ORIGIN(SRAM) + LENGTH(SRAM);

  /* Remove information from the compiler libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }
}
