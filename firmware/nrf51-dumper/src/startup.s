/**
 * @file startup.s
 * @brief Cortex-M0 vector table and startup code for nRF51822 bootloader.
 *
 * The bootloader is placed at 0x0003C000 (top of flash, before settings page).
 * The Nordic MBR (at 0x00000000) reads UICR.BOOTLOADERADDR and jumps here.
 */

    .syntax unified
    .cpu cortex-m0
    .thumb

/* ── Stack top (16 KB SRAM for QFAA variant) ───────────────────────────── */
    .equ STACK_TOP, 0x20004000

/* ── Vector table ──────────────────────────────────────────────────────── */

    .section .isr_vector, "a", %progbits
    .type g_vectors, %object

g_vectors:
    .word STACK_TOP                /* 0x00: Initial Stack Pointer */
    .word Reset_Handler            /* 0x04: Reset Handler */
    .word NMI_Handler              /* 0x08: NMI */
    .word HardFault_Handler        /* 0x0C: Hard Fault */
    .word 0                        /* 0x10: Reserved (no MemManage on M0) */
    .word 0                        /* 0x14: Reserved (no BusFault on M0) */
    .word 0                        /* 0x18: Reserved (no UsageFault on M0) */
    .word 0                        /* 0x1C: Reserved */
    .word 0                        /* 0x20: Reserved */
    .word 0                        /* 0x24: Reserved */
    .word 0                        /* 0x28: Reserved */
    .word SVC_Handler              /* 0x2C: SVCall */
    .word 0                        /* 0x30: Reserved */
    .word 0                        /* 0x34: Reserved */
    .word PendSV_Handler           /* 0x38: PendSV */
    .word SysTick_Handler          /* 0x3C: SysTick */

    /* nRF51822 peripheral IRQs (0-25) — unused in bootloader */
    .rept 26
    .word Default_Handler
    .endr

    .size g_vectors, .-g_vectors

/* ── Reset handler ─────────────────────────────────────────────────────── */

    .section .text
    .type Reset_Handler, %function
    .global Reset_Handler
    .thumb_func

Reset_Handler:
    /* Set stack pointer */
    ldr r0, =STACK_TOP
    msr msp, r0

    /* Zero .bss */
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
bss_loop:
    cmp r0, r1
    bge bss_done
    str r2, [r0]
    adds r0, r0, #4
    b bss_loop
bss_done:

    /* Copy .data from flash to SRAM */
    ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =_sidata
data_loop:
    cmp r0, r1
    bge data_done
    ldr r3, [r2]
    str r3, [r0]
    adds r0, r0, #4
    adds r2, r2, #4
    b data_loop
data_done:

    /* Call main */
    bl main

hang:
    b hang

/* ── Default handlers ──────────────────────────────────────────────────── */

    .type NMI_Handler, %function
    .weak NMI_Handler
    .thumb_func
NMI_Handler:
    b .

    .type HardFault_Handler, %function
    .weak HardFault_Handler
    .thumb_func
HardFault_Handler:
    b .

    .type SVC_Handler, %function
    .weak SVC_Handler
    .thumb_func
SVC_Handler:
    b .

    .type PendSV_Handler, %function
    .weak PendSV_Handler
    .thumb_func
PendSV_Handler:
    b .

    /* SysTick_Handler is defined in bootloader_main.c */

    .type Default_Handler, %function
    .weak Default_Handler
    .thumb_func
Default_Handler:
    b .

    .end
