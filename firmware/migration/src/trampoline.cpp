/**
 * @file trampoline.cpp
 * @brief App1 — booted by the OLD 4 KB bootloader at 0x08001000; immediately
 *        jumps to 0x08004000 (the new-format app / the bl_updater). This is what
 *        lets the old bootloader "start the new format" at the 16 KB offset.
 */
#include "migration.h"
#include "stm32f1xx.h"

static void cookie(uint32_t status) {
    volatile uint32_t *c = (volatile uint32_t *)MIG_COOKIE_ADDR;
    c[0] = MIG_COOKIE_MAGIC;
    c[1] = status;
}

extern "C" int main() {
    cookie(MIG_STATUS_TRAMPOLINE);

    uint32_t sp = *(volatile uint32_t *)(MIG_APP_BASE);
    uint32_t pc = *(volatile uint32_t *)(MIG_APP_BASE + 4);

    __asm volatile("cpsid i");          // IRQs off across the handoff
    SCB_VTOR = MIG_APP_BASE;            // target sets its own VTOR too
    __asm volatile(
        "msr msp, %0 \n"
        "bx  %1      \n"
        :: "r"(sp), "r"(pc));
    for (;;) {}
}
