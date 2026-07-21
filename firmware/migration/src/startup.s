/**
 * @file startup.s
 * @brief Shared Cortex-M3 startup for the migration apps. The vector-table base
 *        is parameterised by the linker symbol `_vtor_base` (0x08001000 for the
 *        trampoline, 0x08004000 for the bl_updater), so one startup serves both.
 *        Copies .data (incl. .ramfunc) to RAM and zeroes .bss before main().
 */
    .syntax unified
    .cpu cortex-m3
    .thumb
    .equ SCB_VTOR, 0xE000ED08

    .section .isr_vector, "a", %progbits
    .global g_vectors
g_vectors:
    .word _estack
    .word Reset_Handler
    .word Default_Handler        /* NMI */
    .word Default_Handler        /* HardFault */
    .rept 12
    .word Default_Handler
    .endr
    .word Default_Handler        /* SysTick */
    .rept 43
    .word Default_Handler
    .endr

    .section .text
    .global Reset_Handler
    .type Reset_Handler, %function
    .thumb_func
Reset_Handler:
    ldr   r0, =_estack
    mov   sp, r0
    /* relocate vector table to this app's base */
    ldr   r0, =SCB_VTOR
    ldr   r1, =_vtor_base
    str   r1, [r0]
    /* zero .bss */
    ldr   r0, =_sbss
    ldr   r1, =_ebss
    movs  r2, #0
1:  cmp   r0, r1
    bge   2f
    str   r2, [r0], #4
    b     1b
2:  /* copy .data (incl .ramfunc) from flash to RAM */
    ldr   r0, =_sdata
    ldr   r1, =_edata
    ldr   r2, =_sidata
3:  cmp   r0, r1
    bge   4f
    ldr   r3, [r2], #4
    str   r3, [r0], #4
    b     3b
4:  bl    main
5:  b     5b

    .type Default_Handler, %function
    .weak Default_Handler
    .thumb_func
Default_Handler:
    b     .
    .end
