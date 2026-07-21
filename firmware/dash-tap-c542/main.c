/**
 * @file main.c
 * @brief NUCLEO-C542RC dashboard-plug tap + USB bridge (reference firmware).
 *
 * Purpose: tap the two signal wires of the dashboard's internal plug (one to the
 * **BT** chip = nRF51822, one to the **dashboard** MCU = STM32F103) on the Arduino
 * header pins **A0 = PA0** and **A2 = PA4**, FIND OUT which wire is which by passive
 * sniffing, then BRIDGE the chosen wire to the PC over the ST-LINK USB Virtual COM
 * Port — so the existing host tools (dump_bootloader.py, ninebot_flasher.py,
 * nbu_send.py) run straight through it.
 *
 * The classification brain is `wire_finder.c` (pure logic, host-tested in
 * sim/test_wire_finder.cpp). This file is the **board glue** and uses the STM32Cube
 * HAL — build it as a CubeIDE project against the **STM32CubeC5** package for the
 * NUCLEO-C542RC (STM32C542RCT6, Cortex-M33). Items that depend on the STM32C5
 * datasheet/CubeMX are marked  <<< CONFIRM IN CUBEMX >>>.
 *
 * ── Pin map (NUCLEO-C542RC, verified from the board devicetree) ──
 *   A0 = PA0   tap on plug wire #1
 *   A2 = PA4   tap on plug wire #2
 *   GND        common ground to the dashboard (REQUIRED)
 * Both lines are 3.3 V TTL, 115200 8N1, Ninebot `5A A5` framing.
 *
 * ── How the two taps become USARTs ──
 * To classify passively, each tap is an RX. PA0/PA4 must be routed to a USART_RX
 * alternate function (pick the instances in CubeMX — it knows the STM32C542 AF map;
 * pin/AF tables for this part were not yet web-published at authoring time).
 * For a one-wire half-duplex line, drive it with the same USART in HDSEL mode when
 * bridging; for a two-wire (full-duplex) plug, RX is enough to read each direction.
 */
#include "wire_finder.h"
#include <string.h>
#include <stdio.h>

/* The HAL header name differs per family; CubeIDE generates it. */
#include "stm32c5xx_hal.h"          /* <<< CONFIRM IN CUBEMX >>> (generated) */

/* ── Peripheral handles (configured by CubeMX-generated init) ───────────────
 * LINE0 = PA0 (A0), LINE1 = PA4 (A2), VCP = ST-LINK virtual COM port USART. */
extern UART_HandleTypeDef hxUartLine0;   /* USART on PA0  <<< CONFIRM IN CUBEMX >>> */
extern UART_HandleTypeDef hxUartLine1;   /* USART on PA4  <<< CONFIRM IN CUBEMX >>> */
extern UART_HandleTypeDef hxUartVcp;     /* ST-LINK VCP   <<< CONFIRM IN CUBEMX >>> */

#define LINE_BAUD     115200u
#define FIND_WINDOW_MS 4000u             /* passive sniff time before reporting */

/* Single-byte RX ring per line, filled from the RX interrupt. */
static volatile uint8_t  s_rx[WF_LINES];
static volatile uint8_t  s_rx_ready[WF_LINES];
static UART_HandleTypeDef *const s_line_uart[WF_LINES] = { &hxUartLine0, &hxUartLine1 };

static void vcp_print(const char *s) {
    HAL_UART_Transmit(&hxUartVcp, (uint8_t *)s, (uint16_t)strlen(s), HAL_MAX_DELAY);
}

/* HAL RX-complete: stash the byte, mark ready, re-arm. (Called for each line UART.) */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h) {
    for (int i = 0; i < WF_LINES; i++) {
        if (h == s_line_uart[i]) {
            s_rx_ready[i] = 1;
            HAL_UART_Receive_IT(s_line_uart[i], (uint8_t *)&s_rx[i], 1);
            return;
        }
    }
}

/* ── FIND: passively sniff both taps, classify, print a verdict ──────────── */
static int find_active_line(wire_finder_t *wf) {
    wf_reset(wf);
    for (int i = 0; i < WF_LINES; i++)
        HAL_UART_Receive_IT(s_line_uart[i], (uint8_t *)&s_rx[i], 1);

    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < FIND_WINDOW_MS) {
        for (int i = 0; i < WF_LINES; i++) {
            if (s_rx_ready[i]) { s_rx_ready[i] = 0; wf_feed(wf, i, s_rx[i]); }
        }
    }

    wf_line_t st[WF_LINES];
    wf_classify(wf, st);

    char line[96];
    const char *name[WF_LINES] = { "A0/PA0", "A2/PA4" };
    int best = -1; uint32_t best_frames = 0;
    for (int i = 0; i < WF_LINES; i++) {
        snprintf(line, sizeof line,
                 "[FIND] %s: %lu bytes, %lu frames (%lu bad) — %s\r\n",
                 name[i], (unsigned long)st[i].bytes,
                 (unsigned long)st[i].frames_ok, (unsigned long)st[i].frames_bad,
                 wf_role(&st[i]));
        vcp_print(line);
        if (st[i].frames_ok > best_frames) { best_frames = st[i].frames_ok; best = i; }
    }
    if (best < 0)
        vcp_print("[FIND] no Ninebot traffic on either tap — check GND, baud, power, "
                  "and that the dashboard is awake.\r\n");
    else {
        snprintf(line, sizeof line, "[FIND] bridging %s (most Ninebot frames).\r\n",
                 name[best]);
        vcp_print(line);
    }
    return best;
}

/* ── BRIDGE: transparently relay the chosen tap <-> VCP ─────────────────────
 * Full-duplex plug: read the tap, forward to PC; read PC, forward to the tap.
 * (For a one-wire half-duplex line, configure that UART in HDSEL so its TX/RX
 *  share the pin; the relay loop is identical.) */
static void bridge(int line) {
    UART_HandleTypeDef *tap = s_line_uart[line];
    uint8_t b;
    s_rx_ready[line] = 0;
    HAL_UART_Receive_IT(tap, (uint8_t *)&s_rx[line], 1);
    for (;;) {
        if (s_rx_ready[line]) {                       /* dashboard -> PC */
            s_rx_ready[line] = 0;
            HAL_UART_Transmit(&hxUartVcp, (uint8_t *)&s_rx[line], 1, HAL_MAX_DELAY);
        }
        if (HAL_UART_Receive(&hxUartVcp, &b, 1, 0) == HAL_OK)  /* PC -> dashboard */
            HAL_UART_Transmit(tap, &b, 1, HAL_MAX_DELAY);
    }
}

/* CubeMX generates SystemClock_Config + MX_*_UART_Init + MX_GPIO_Init. */
extern void SystemClock_Config(void);
extern void MX_GPIO_Init(void);
extern void MX_LINE0_UART_Init(void);   /* PA0 @115200, RX (HDSEL if one-wire) */
extern void MX_LINE1_UART_Init(void);   /* PA4 @115200, RX (HDSEL if one-wire) */
extern void MX_VCP_UART_Init(void);     /* ST-LINK VCP @115200 */

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_LINE0_UART_Init();
    MX_LINE1_UART_Init();
    MX_VCP_UART_Init();

    vcp_print("\r\n[C542-TAP] dashboard-plug tap ready (A0=PA0, A2=PA4 @115200).\r\n");

    wire_finder_t wf;
    int line = find_active_line(&wf);
    if (line < 0) line = 0;            /* nothing found: still expose A0 as a bridge */
    bridge(line);                      /* never returns */
}
