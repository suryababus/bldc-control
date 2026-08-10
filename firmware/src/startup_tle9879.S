/*
 * Minimal Cortex-M3 startup for TLE9879-2QXA40 (GNU assembler)
 */
  .syntax unified
  .cpu cortex-m3
  .thumb

  .global g_pfnVectors
  .global Default_Handler
  .global Reset_Handler

  .section .isr_vector,"a",%progbits
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
  /* External IRQs 0..15 */
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .size g_pfnVectors, .-g_pfnVectors

  .section .text.Reset_Handler,"ax",%progbits
  .weak Reset_Handler
  .thumb_func
  .type Reset_Handler, %function
Reset_Handler:
  ldr r0, =_sdata
  ldr r1, =_edata
  ldr r2, =_sidata
  movs r3, #0
  b 2f
1:
  ldr r4, [r2, r3]
  str r4, [r0, r3]
  adds r3, r3, #4
2:
  adds r4, r0, r3
  cmp r4, r1
  bcc 1b

  ldr r2, =_sbss
  ldr r4, =_ebss
  movs r3, #0
  b 4f
3:
  str r3, [r2]
  adds r2, r2, #4
4:
  cmp r2, r4
  bcc 3b

  bl SystemInit
  bl main
  b .

  .size Reset_Handler, .-Reset_Handler

  .section .text.Default_Handler,"ax",%progbits
  .thumb_func
  .type Default_Handler, %function
Default_Handler:
  b Default_Handler
  .size Default_Handler, .-Default_Handler

  .macro def_irq handler
  .weak \handler
  .thumb_set \handler, Default_Handler
  .endm

  def_irq NMI_Handler
  def_irq HardFault_Handler
  def_irq MemManage_Handler
  def_irq BusFault_Handler
  def_irq UsageFault_Handler
  def_irq SVC_Handler
  def_irq DebugMon_Handler
  def_irq PendSV_Handler
  def_irq SysTick_Handler
