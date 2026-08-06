/**
 * @file main.c
 * @brief nRF51 bootloader installer — writes the custom bootloader to 0x0003C000.
 *
 * Port of the deleted STM32 `firmware/migration16` (installer @0x08008000 that wrote a bootloader
 * to 0x08004000). That whole 4K->16K migration existed for a dashboard STM32 that does not exist
 * — see boards/ble-dashboard/MCU_IDENTIFICATION.md. On the real chip the layout is fixed by the
 * hardware, so the job is much simpler:
 *
 *     0x00000000  MBR + S110 SoftDevice        (region 0 — untouchable, MPU-enforced)
 *     0x00018000  application slot             <- THIS app + the bootloader image appended to it
 *     0x0003C000  bootloader  (UICR.BOOTLOADERADDR, already set by the factory)
 *
 * ## Why this exists when SWD also works
 *
 * SWD is the better path (`tools/nrf51/nrf51_swd.py flash`) — it is verifiable and reversible.
 * This app is the fallback for when the SWD pads are not accessible: it lets the custom
 * bootloader be installed over the bus alone.
 *
 * ## Why the copy loop must run from RAM
 *
 * nRF51 RM ch.6: *"The CPU is halted while the NVMC is writing to the NVM"* / *"...performs the
 * erase operation"*. Flash-resident code can therefore program *other* pages fine — and this app
 * lives at 0x18000 while writing 0x3C000, so strictly it would not need a RAM copy.
 *
 * It runs from RAM anyway, for one reason: **the window must be as short and as
 * self-contained as possible.** Once the bootloader region is erased the board has no bootloader
 * until the write completes; keeping the loop in SRAM removes any dependence on flash reads
 * (literal pools, veneers, an accidental call) during that window.
 *
 * ## Image layout expected
 *
 * The bootloader image is appended to this app by `pack.py`, so a single blob is flashed into the
 * app slot. The trailer lets the installer find and check it:
 *
 *     [ installer code ][ bootloader image ][ trailer(16B) ]
 *     trailer: magic "NBBL" | length | crc32 | ~length
 */

#include <stdint.h>

#define REG(a)                  (*(volatile uint32_t *)(a))

#define NVMC_BASE               0x4001E000UL
#define NVMC_READY              REG(NVMC_BASE + 0x400)
#define NVMC_CONFIG             REG(NVMC_BASE + 0x504)
#define NVMC_ERASEPAGE          REG(NVMC_BASE + 0x508)
#define NVMC_REN                0UL
#define NVMC_WEN                1UL
#define NVMC_EEN                2UL

#define GPIO_BASE               0x50000000UL
#define GPIO_PIN_CNF(n)         REG(GPIO_BASE + 0x700UL + 4UL * (n))
#define UART0_BASE              0x40002000UL
#define UART_TASKS_STARTTX      REG(UART0_BASE + 0x008)
#define UART_EVENTS_TXDRDY      REG(UART0_BASE + 0x11C)
#define UART_ENABLE             REG(UART0_BASE + 0x500)
#define UART_PSELTXD            REG(UART0_BASE + 0x50C)
#define UART_PSELRXD            REG(UART0_BASE + 0x514)
#define UART_TXD                REG(UART0_BASE + 0x51C)
#define UART_BAUDRATE           REG(UART0_BASE + 0x524)
#define UART_CONFIG             REG(UART0_BASE + 0x56C)
#define BAUD_115200             0x01D7E000UL
#define PIN_BUS_A               15
#define PIN_BUS_B               20

#define SCB_AIRCR               REG(0xE000ED0CUL)
#define AIRCR_SYSRESET          ((0x05FAUL << 16) | (1UL << 2))

#define APP_START               0x00018000UL
#define BL_START                0x0003C000UL
#define BL_SIZE                 0x4000UL       /* 16 KB */
#define PAGE_SIZE               0x400UL        /* 1 KB */
#define APP_SLOT_END            BL_START

#define TRAILER_MAGIC           0x4C42424EUL   /* "NBBL" little-endian */

void SysTick_Handler(void);
void SysTick_Handler(void) { }

/* ── Minimal UART for progress reporting ──────────────────────────────────── */

static void uart_init(void)
{
    GPIO_PIN_CNF(PIN_BUS_A) = 3U;              /* output */
    GPIO_PIN_CNF(PIN_BUS_B) = 4U;              /* input  */
    UART_PSELTXD = PIN_BUS_A;
    UART_PSELRXD = PIN_BUS_B;
    UART_BAUDRATE = BAUD_115200;
    UART_CONFIG = 0;
    UART_ENABLE = 4U;
    UART_TASKS_STARTTX = 1;
}

static void puts_(const char *s)
{
    while (*s) {
        UART_EVENTS_TXDRDY = 0;
        UART_TXD = (uint8_t)*s++;
        while (UART_EVENTS_TXDRDY == 0U) { }
    }
    UART_EVENTS_TXDRDY = 0;
}

/* ── CRC-32 (same polynomial as the bootloader's crc32.c) ─────────────────── */

static uint32_t crc32_buf(const uint8_t *d, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    for (i = 0; i < len; i++) {
        uint32_t j;
        crc ^= d[i];
        for (j = 0; j < 8U; j++) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
    }
    return ~crc;
}

/* ── The RAM-resident erase+program+reset window ──────────────────────────── */

/**
 * @brief Install the bootloader and reset. Runs from SRAM; never returns.
 * @param src   word-aligned source in the app slot
 * @param words image length in 32-bit words
 * @sideeffects erases and rewrites 0x3C000..0x40000, then resets the chip
 */
__attribute__((section(".ramfunc"), noinline, used))
static void ram_install(const uint32_t *src, uint32_t words)
{
    uint32_t addr;
    uint32_t i;

    NVMC_CONFIG = NVMC_EEN;
    while (NVMC_READY == 0U) { }
    for (addr = BL_START; addr < BL_START + BL_SIZE; addr += PAGE_SIZE) {
        NVMC_ERASEPAGE = addr;
        while (NVMC_READY == 0U) { }
    }

    NVMC_CONFIG = NVMC_WEN;
    while (NVMC_READY == 0U) { }
    for (i = 0; i < words; i++) {
        REG(BL_START + i * 4U) = src[i];       /* word-aligned only (RM 6.1.1) */
        while (NVMC_READY == 0U) { }
    }

    NVMC_CONFIG = NVMC_REN;
    while (NVMC_READY == 0U) { }

    SCB_AIRCR = AIRCR_SYSRESET;
    for (;;) { }
}

/* ── Trailer lookup ───────────────────────────────────────────────────────── */

typedef struct {
    uint32_t magic;
    uint32_t length;
    uint32_t crc32;
    uint32_t not_length;
} trailer_t;

/**
 * @brief Scan the app slot for the packed bootloader trailer.
 * @param out_addr receives the image start address
 * @param out_len  receives the image length
 * @return 1 when a consistent, CRC-correct image was found
 */
static int find_image(uint32_t *out_addr, uint32_t *out_len)
{
    uint32_t p;

    /* The trailer is word-aligned somewhere in the app slot; walk backwards so we find the
     * last (most recently packed) one first. */
    for (p = APP_SLOT_END - sizeof(trailer_t); p > APP_START; p -= 4U) {
        const trailer_t *t = (const trailer_t *)p;
        if (t->magic != TRAILER_MAGIC) {
            continue;
        }
        if ((t->length ^ 0xFFFFFFFFUL) != t->not_length) {
            continue;                          /* length/complement disagree */
        }
        if (t->length == 0U || t->length > BL_SIZE || (t->length & 3U) != 0U) {
            continue;
        }
        {
            const uint32_t start = p - t->length;
            if (start < APP_START) {
                continue;
            }
            if (crc32_buf((const uint8_t *)start, t->length) != t->crc32) {
                continue;                      /* corrupt payload */
            }
            *out_addr = start;
            *out_len = t->length;
            return 1;
        }
    }
    return 0;
}

/** @brief Does the image look like a bootloader that will actually run at BL_START? */
static int image_plausible(uint32_t addr)
{
    const uint32_t sp = REG(addr);
    const uint32_t rv = REG(addr + 4U);
    if (sp < 0x20000000UL || sp > 0x20004000UL) {
        return 0;                              /* initial SP must be in SRAM */
    }
    if ((rv & 1U) == 0U) {
        return 0;                              /* reset vector must be thumb */
    }
    if ((rv & ~1U) < BL_START || (rv & ~1U) >= (BL_START + BL_SIZE)) {
        return 0;                              /* must point inside the bootloader slot */
    }
    return 1;
}

int main(void)
{
    uint32_t addr = 0;
    uint32_t len = 0;

    uart_init();
    puts_("\r\n[BL-INST] nRF51 bootloader installer\r\n");

    if (!find_image(&addr, &len)) {
        puts_("[BL-INST] ERROR: no valid packed bootloader found (bad magic/CRC)\r\n");
        for (;;) { }
    }
    if (!image_plausible(addr)) {
        puts_("[BL-INST] ERROR: image vector table not valid for 0x3C000 - refusing\r\n");
        for (;;) { }
    }

    puts_("[BL-INST] image OK, installing to 0x3C000 from RAM...\r\n");

    /* Let the UART drain: the reset at the end of ram_install() is immediate. */
    {
        volatile uint32_t d = 400000U;
        while (d--) { }
    }

    __asm volatile ("cpsid i");                /* no IRQ may vector into erased flash */
    ram_install((const uint32_t *)addr, len / 4U);

    for (;;) { }                               /* unreachable */
}
