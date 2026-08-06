# nRF51 flash / IAP — what runs from flash, what must run from RAM

Sourced from the **nRF51 Series Reference Manual, ch. 6 "Non-Volatile Memory Controller (NVMC)"**.
Quotes below are verbatim.

> ⚠️ The two files in `boards/ble-dashboard/datasheets/` named `*.pdf` are **not datasheets** — both
> are saved HTML error pages (`<!DOCTYPE html>…`). They cannot be used as a reference. The real
> reference manual was consulted online for this document.

---

## 1. The key question: can flash-resident code program flash?

**Yes — for *other* pages.** The NVMC stalls the core instead of faulting:

> *"The CPU is halted while the NVMC is writing to the NVM."*
> *"The CPU is halted while the NVMC performs the erase operation."*

So when the bootloader at `0x0003C000` erases/writes the application at `0x00018000`, the CPU simply
pauses for the operation and resumes — its own instructions are untouched. **No RAM copy is needed for
normal application updates.**

**No — for the pages you are executing from.** After an erase *"all bits in the page are set to '1'"*,
so the code that issued the erase no longer exists and the CPU resumes into blank flash. Updating the
**bootloader itself** therefore requires the erase/program loop to execute from **SRAM**.

| Operation | Who erases/writes | Runs from | RAM needed |
|---|---|---|---|
| App update (`0x18000`) | bootloader @`0x3C000` | **flash** — fine | none |
| Settings/boot record (`0x3FC00`) | bootloader @`0x3C000` | flash — different page | none |
| **Bootloader self-update (`0x3C000`)** | bootloader itself | **RAM (mandatory)** | ~124 B |

## 2. What is actually in RAM in this project

`bootloader/nrf51/src/nrf51_selfupdate.c` puts exactly one function, `ram_install()`, in a `.ramfunc`
section. The linker places it at the start of `.data`, so the existing startup copy loop moves it into
SRAM; nothing else changes.

Verified in the linked image:

```
0003d8d8 t __ram_install_veneer     <- flash: long-branch veneer into RAM
0003df28 A _sidata                  <- flash: load address of the RAM image
20002000 t ram_install              <- RAM: the erase/program/reset loop  (124 B)
2000207c T _edata
```

`ram_install()` must not touch flash while the bootloader region is erased, so it:
- takes its source pointer and length as **parameters**,
- polls `NVMC.READY` inline instead of calling a helper in flash,
- ends with `AIRCR = VECTKEY|SYSRESETREQ`, and never returns.

The caller disables interrupts first (`cpsid i`) — an interrupt vectoring into erased flash would be
fatal. Note the linker reports *"a LOAD segment with RWX permissions"*: that is inherent to executing
from RAM and is expected here.

## 3. Other NVMC rules the driver must obey

| Rule (verbatim) | Consequence / where handled |
|---|---|
| *"Only word aligned writes are allowed. Byte or half word aligned writes will result in a hard fault."* | `nrf_flash_write()` rejects a misaligned address instead of faulting, and `nrf_flash_stream_*()` buffers the 0-3 leftover bytes of each NBU block so the address never drifts off a word boundary |
| *"The NVMC is only able to write bits in the NVM that are erased, that is, set to '1'."* | pages are erased before programming; the update flag is *cleared in place* (`0xDEAD1234 → 0`, bits only go 1→0) so the boot record on the same page survives |
| *"The user must make sure that writing and erasing is not enabled at the same time…"* | `NVMC.CONFIG` is always **assigned** (`REN`/`WEN`/`EEN`), never OR-ed, and returned to `REN` afterwards |
| *"After erasing a NVM page all bits in the page are set to '1'."* | erase is verified by reading back `0xFFFFFFFF` |
| `ERASEPCR0` is restricted by the MPU to code running in region 0 | the bootloader lives in region 1, so it **physically cannot** erase the SoftDevice (region 0 = below `UICR.CLENR0 = 0x18000`) — a free hardware safety net, on top of the software address guards |

Timing: `tPAGEERASE` ≈ **22.3 ms** per 1 KB page, `tWRITE` ≈ **46.3 µs** per word (product spec).
Erasing the 144 KB app region therefore takes ≈ 3.2 s — the watchdog is fed throughout.

## 4. The IAP / update flow

```
   PC ──NBU frames over the Ninebot bus (115200 8N1, half-duplex P0.15/P0.20)──> bootloader
        │
        ├─ erase app region                     (flash-resident code, CPU stalls per page)
        ├─ stream .sfw body -> 0x18000          (word-aligned streaming writer)
        ├─ validate header + CRC-32 + ECDSA-P256 signature
        ├─ anti-rollback: reject fw_version < installed (unless FORCE_UPDATE)
        │
        ├─ target 0x03 (app) ─> save boot record (header+version) -> reset
        └─ target 0x04 (bootloader) ─> ram_install() from SRAM -> reset
```

Nothing is trusted before the signature check: a bootloader image is staged in the *application*
region and only copied over the live bootloader after it verifies.

## 5. Residual risk

The window between "erase bootloader" and "program complete" in `ram_install()` is not
power-fail-safe — a power loss there leaves no bootloader, and recovery needs SWD
([`WIRING_NRF51_SWD.md`](WIRING_NRF51_SWD.md)). This is unavoidable on a part with a single
bootloader slot; keep the scooter powered during a bootloader update, and prefer SWD for that step.
Application updates do **not** have this problem: the bootloader survives, so a failed app update
just re-enters update mode.
