/**
 * @file startup.s
 * @brief Cortex-M3 vector table and startup code for STM32F103 bootloader.
 *
 * The bootloader occupies 0x08000000 — 0x08002FFF (12 KB).
 * Vector table at 0x08000000, code follows immediately after.
 */

    .syntax unified
    .cpu cortex-m3
    .thumb

/* ── Stack configuration ───────────────────────────────────────────────── */
    .equ STACK_TOP, 0x20005000     /* Top of 20 KB SRAM */

/* ── Vector table ──────────────────────────────────────────────────────── */

    .section .isr_vector, "a", %progbits
    .type g_vectors, %object
    .size g_vectors, .-g_vectors

g_vectors:
    .word STACK_TOP                /* 0x00: Initial Stack Pointer */
    .word Reset_Handler            /* 0x04: Reset Handler */
    .word NMI_Handler              /* 0x08: NMI Handler */
    .word HardFault_Handler        /* 0x0C: Hard Fault Handler */
    .word MemManage_Handler        /* 0x10: MPU Fault Handler */
    .word BusFault_Handler         /* 0x14: Bus Fault Handler */
    .word UsageFault_Handler       /* 0x18: Usage Fault Handler */
    .word 0                        /* 0x1C: Reserved */
    .word 0                        /* 0x20: Reserved */
    .word 0                        /* 0x24: Reserved */
    .word 0                        /* 0x28: Reserved */
    .word SVC_Handler              /* 0x2C: SVCall Handler */
    .word 0                        /* 0x30: Debug Monitor */
    .word 0                        /* 0x34: Reserved */
    .word PendSV_Handler           /* 0x38: PendSV Handler */
    .word SysTick_Handler          /* 0x3C: SysTick Handler */

    /* IRQ 0-42 — all unused in bootloader, point to default */
    .rept 43
    .word Default_Handler
    .endr

/* ── Reset handler ─────────────────────────────────────────────────────── */

    .section .text
    .type Reset_Handler, %function
    .global Reset_Handler
    .thumb_func

Reset_Handler:
    /* Set stack pointer (redundant but safe) */
    ldr r0, =STACK_TOP
    msr msp, r0

    /* Zero .bss section */
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
bss_loop:
    cmp r0, r1
    bge bss_done
    str r2, [r0], #4
    b bss_loop
bss_done:

    /* Copy .data from flash to SRAM */
    ldr r0, =_sdata         /* Destination (SRAM) */
    ldr r1, =_edata         /* End of .data in SRAM */
    ldr r2, =_sidata        /* Source (Flash) */
data_loop:
    cmp r0, r1
    bge data_done
    ldr r3, [r2], #4
    str r3, [r0], #4
    b data_loop
data_done:

    /* Call main() */
    bl main

    /* If main returns, loop forever */
hang:
    b hang

/* ── Default fault/interrupt handlers ──────────────────────────────────── */

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

    .type MemManage_Handler, %function
    .weak MemManage_Handler
    .thumb_func
MemManage_Handler:
    b .

    .type BusFault_Handler, %function
    .weak BusFault_Handler
    .thumb_func
BusFault_Handler:
    b .

    .type UsageFault_Handler, %function
    .weak UsageFault_Handler
    .thumb_func
UsageFault_Handler:
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

    /* SysTick_Handler is defined in bootloader_main.c, not here */

    .type Default_Handler, %function
    .weak Default_Handler
    .thumb_func
Default_Handler:
    b .

    .end
