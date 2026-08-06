/**
 * @file startup_nrf51.cpp
 * @brief Minimal Cortex-M0 startup for the nRF51822 app slot.
 *
 * The app lives above the S110 SoftDevice, so the vector table sits at 0x00018000 and the
 * SoftDevice forwards interrupts to it. Keep the first two words (initial SP, reset vector)
 * intact — `tools/nrf51/nrf51_swd.py` and verify-safe both sanity-check them.
 */
#include <stdint.h>

extern "C" int main();

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

/* Freestanding C++ runtime bits. Static objects with (virtual) destructors would otherwise pull in
   __cxa_atexit/__dso_handle from libstdc++; the firmware never exits, so these are no-ops. */
extern "C" {
void* __dso_handle = nullptr;
int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
void __cxa_pure_virtual() { for (;;) { } }
}

/** @brief Copy .data, zero .bss, then hand over to main(). @sideeffects */
extern "C" void Reset_Handler()
{
    uint32_t* src = &_sidata;
    for (uint32_t* dst = &_sdata; dst < &_edata; ) {
        *dst++ = *src++;
    }
    for (uint32_t* dst = &_sbss; dst < &_ebss; ) {
        *dst++ = 0u;
    }
    (void)main();
    for (;;) { }
}

/** @brief Catch-all handler — the watchdog recovers us if we ever land here. */
extern "C" void Default_Handler()
{
    for (;;) { }
}

#define ALIAS(name) extern "C" void name() __attribute__((weak, alias("Default_Handler")))
ALIAS(NMI_Handler);
ALIAS(HardFault_Handler);
ALIAS(SVC_Handler);
ALIAS(PendSV_Handler);
ALIAS(SysTick_Handler);

/** @brief Cortex-M0 vector table; placed first in flash by the linker script. */
__attribute__((section(".isr_vector"), used))
void (* const g_vectors[])(void) = {
    reinterpret_cast<void (*)(void)>(&_estack),
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    0, 0, 0, 0, 0, 0, 0,
    SVC_Handler,
    0, 0,
    PendSV_Handler,
    SysTick_Handler,
};
