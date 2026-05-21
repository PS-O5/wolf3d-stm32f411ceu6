.syntax unified
.cpu cortex-m4
.fpu fpv4-sp-d16
.thumb

.global g_pfnVectors
.global Reset_Handler

/* Symbols exported from linker script */
.word _sdata
.word _edata
.word _sbss
.word _ebss
.word _sidata
.word _estack

.section .text.Reset_Handler
.type Reset_Handler, %function
Reset_Handler:
  /* 1. Set the Stack Pointer */
  ldr r0, =_estack
  mov sp, r0

  /* 2. Copy the data section from FLASH to SRAM */
  ldr r0, =_sdata
  ldr r1, =_edata
  ldr r2, =_sidata
  movs r3, #0
  b LoopCopyData

CopyDataInit:
  ldr r4, [r2, r3]
  str r4, [r0, r3]
  adds r3, r3, #4

LoopCopyData:
  adds r4, r0, r3
  cmp r4, r1
  bcc CopyDataInit

  /* 3. Clear the bss segment */
  ldr r0, =_sbss
  ldr r1, =_ebss
  movs r2, #0
  b LoopFillZeroBss

FillZeroBss:
  str r2, [r0]
  adds r0, r0, #4

LoopFillZeroBss:
  cmp r0, r1
  bcc FillZeroBss

  /* 4. Branch to main loop execution context */
  bl main
  bx lr
.size Reset_Handler, .-Reset_Handler

/* Vector Table Initialization Layout */
.section .isr_vector, "a", %progbits
.type g_pfnVectors, %object

g_pfnVectors:
  .word _estack
  .word Reset_Handler
  .word NMI_Handler
  .word HardFault_Handler
  .word MemManage_Handler
  .word BusFault_Handler
  .word UsageFault_Handler
  .word 0
  .word 0
  .word 0
  .word 0
  .word SVC_Handler
  .word DebugMon_Handler
  .word 0
  .word PendSV_Handler
  .word SysTick_Handler

  /* External Interrupts (Stub placeholders) */
  .word WWDG_IRQHandler
  .word PVD_IRQHandler
  /* ... Remaining vectors can be referenced or left open if unused ... */

.size g_pfnVectors, .-g_pfnVectors


/* Define weak aliases for exception vectors to point to a infinite loop default */
.macro def_irq_handler handler_name
  .weak \handler_name
  .thumb_set \handler_name, Default_Handler
.endm

def_irq_handler NMI_Handler
def_irq_handler HardFault_Handler
def_irq_handler MemManage_Handler
def_irq_handler BusFault_Handler
def_irq_handler UsageFault_Handler
def_irq_handler SVC_Handler
def_irq_handler DebugMon_Handler
def_irq_handler PendSV_Handler
def_irq_handler WWDG_IRQHandler
def_irq_handler PVD_IRQHandler

/* REMOVE SysTick_Handler from the macro list so it doesn't get weakly overridden! */
.global SysTick_Handler

.section .text.Default_Handler, "ax", %progbits
Default_Handler:
  b Default_Handler
.size Default_Handler, .-Default_Handler
