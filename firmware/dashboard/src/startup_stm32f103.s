/**
 * @file startup_stm32f103.s
 * @brief Cortex-M3 vector table + reset for the dashboard app @ 0x08001000.
 *
 * The app runs behind the stock 4 KB bootloader, so the reset handler relocates
 * the vector table (SCB->VTOR = 0x08001000) before enabling SysTick — otherwise
 * SysTick_Handler would be fetched from the bootloader's table at 0x00000000.
 */
    .syntax unified
    .cpu cortex-m3
    .thumb

    .equ STACK_TOP, 0x20005000      /* top of 20 KB SRAM */
    .equ SCB_VTOR,  0xE000ED08
    .equ APP_BASE,  0x08001000

    .section .isr_vector, "a", %progbits
    .global g_vectors
g_vectors:
    .word STACK_TOP
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
    .word 0
    .word 0
    .word PendSV_Handler
    .word SysTick_Handler
    .rept 43                          /* IRQ0..42 — unused (polled firmware) */
    .word Default_Handler
    .endr

    .section .text
    .type Reset_Handler, %function
    .global Reset_Handler
    .thumb_func
Reset_Handler:
    ldr   r0, =STACK_TOP
    msr   msp, r0

    /* Relocate the vector table to the app base */
    ldr   r0, =SCB_VTOR
    ldr   r1, =APP_BASE
    str   r1, [r0]

    /* Zero .bss */
    ldr   r0, =_sbss
    ldr   r1, =_ebss
    movs  r2, #0
1:  cmp   r0, r1
    bge   2f
    str   r2, [r0], #4
    b     1b
2:
    /* Copy .data from flash to SRAM */
    ldr   r0, =_sdata
    ldr   r1, =_edata
    ldr   r2, =_sidata
3:  cmp   r0, r1
    bge   4f
    ldr   r3, [r2], #4
    str   r3, [r0], #4
    b     3b
4:
    bl    main
5:  b     5b

    .type Default_Handler, %function
    .weak NMI_Handler
    .weak HardFault_Handler
    .weak MemManage_Handler
    .weak BusFault_Handler
    .weak UsageFault_Handler
    .weak SVC_Handler
    .weak PendSV_Handler
    .weak SysTick_Handler        /* strong override in dash_hal.cpp; weak default for the dumper */
    .thumb_func
NMI_Handler:
HardFault_Handler:
MemManage_Handler:
BusFault_Handler:
UsageFault_Handler:
SVC_Handler:
PendSV_Handler:
SysTick_Handler:
Default_Handler:
    b     .

    .end
