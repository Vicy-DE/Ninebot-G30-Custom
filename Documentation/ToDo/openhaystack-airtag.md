# ToDo — OpenHaystack AirTag Emulation

**Created:** 2026-03-31
**Requirement:** Req #15 ([requirements.md](../Requirements/requirements.md#15-openhaystack-airtag-emulation--apple-findmy))
**Board:** nRF51822
**Status:** Not Started
**Deployment Phase:** 4 (nRF51822 firmware — after STM32 is stable)

---

## Tasks

### Phase 4a — Key Provisioning (PC-side, offline)

- [ ] Research OpenHaystack key derivation format (ECDH P-224 key pair generation)
  - [ ] Clone/review `seemoo-lab/openhaystack` tooling
  - [ ] Understand `generateKeys.py` or equivalent to produce rolling public key set
  - [ ] Confirm key format: 28-byte compressed P-224 public key
- [ ] Write `tools/signing/generate_haystack_keys.py`
  - [ ] Inputs: private seed (or generate new ECDH keypair), number of keys (min 96)
  - [ ] Output: C header `haystack_keys.h` with `uint8_t HAYSTACK_KEYS[N_KEYS][28]`
  - [ ] Output: `keys.json` for association with owner Apple ID via OpenHaystack app
- [ ] Verify key format matches FindMy advertisement spec (see Req 15.1)

### Phase 4b — nRF51822 Firmware: FindMy Advertising

- [ ] Add FindMy advertisement builder to `firmware/decompiled/nrf51822/`
  - [ ] `haystack_adv.h` / `haystack_adv.c`: build `ADV_NONCONN_IND` payload from active key
  - [ ] Payload format: `0xFF 0x4C 0x00 0x12 0x19 <status> <key[6..27]> <hint>` (28 bytes total)
- [ ] Implement key rolling timer
  - [ ] Use RTC1 (available while SoftDevice is active) with 900-second period
  - [ ] Persist current key index in UICR Customer Reserved bytes or a dedicated 1 KB flash page
  - [ ] Increment index on each 900-second tick (wrap at N_KEYS)
- [ ] Configure SoftDevice advertising in broadcaster role
  - [ ] `BLE_GAP_ADV_TYPE_ADV_NONCONN_IND`, interval configurable (default 5,000 ms)
  - [ ] No scan response, no directed advertising

### Phase 4c — nRF51822 Firmware: Mode Switching

- [ ] Define and implement UART command handler for mode switch bytes
  - [ ] `0xAA` → `nrf51_enter_haystack_mode()`: disconnect BLE, stop NUS, start FindMy adv
  - [ ] `0xAB` → `nrf51_exit_haystack_mode()`: stop FindMy adv, re-init NUS, resume normal adv
- [ ] Add mode state variable `nrf51_mode_t current_mode` (NORMAL / HAYSTACK)
- [ ] Ensure mode switch is safe if called while a VESC App BLE connection is active (graceful disconnect)

### Phase 4d — STM32 Firmware: Sleep/Wake Commands

- [ ] Add `nrf51_request_sleep()` call in STM32 power management code (before entering STOP mode)
  - [ ] Send `0xAA` over UART to nRF51
  - [ ] Wait 50 ms for nRF51 to acknowledge / transition (or just fire-and-forget)
- [ ] Add `nrf51_request_wake()` call on STM32 wake-up (EXTI or RTC alarm)
  - [ ] Send `0xAB` over UART to nRF51
  - [ ] Wait for nRF51 Normal mode before resuming VESC App traffic

### Phase 4e — Optional: FindMy in Normal Mode (Req 15.6)

- [ ] Evaluate SoftDevice advertising schedule for interleaving FindMy with Ninebot BLE
  - [ ] If timing allows, add FindMy `ADV_NONCONN_IND` as second advertising set
  - [ ] Ensure it does not degrade VESC App NUS throughput
  - [ ] Mark as optional — skip if scheduling complexity is too high

### Standard Tasks

- [ ] Build `firmware/decompiled/nrf51822/` target — fix all errors
- [ ] Flash to nRF51822 via STM32 relay (XMODEM) — Phase 4 method
- [ ] Verify via UART monitor:
  - [ ] FindMy advertisements visible in BLE scanner (e.g., nRF Connect) while in HAYSTACK mode
  - [ ] Mode switch `0xAA` / `0xAB` commands work correctly
  - [ ] Key rolls after 900 s (or accelerate via test flag)
  - [ ] VESC App BLE recovers after `0xAB` wake command
- [ ] Register keys in OpenHaystack macOS app or `Proxy` app and confirm Find My detection
- [ ] Run test script `Target/test_haystack_adv.py` (to be created)
- [ ] Update `Documentation/CHANGE_LOG.md`
- [ ] Update `Documentation/PROJECT_DOC.md`
- [ ] Save test report `Documentation/Tests/haystack_airtag_test_YYYY-MM-DD.md`
- [ ] Commit (never push)

---

## Notes

### OpenHaystack Reference
- Project: https://github.com/seemoo-lab/openhaystack
- Compatible devices include nRF51822 (confirmed by OpenHaystack firmware examples)
- Keys must be registered in the macOS OpenHaystack app or compatible fork before the scooter will appear on Find My

### Advertisement Power Budget
At 5,000 ms advertising interval with Nordic SoftDevice DCDC converter active:
- TX event (~1.5 ms @ 0 dBm): ~5.3 mA for ~1.5 ms
- Sleep between events: ~0.6 µA
- Estimated average: **~4–6 µA** — negligible against overall scooter sleep current (~2–7 mA)

### Flash Key Storage Layout (nRF51822 app region)
```
0x00018000  SoftDevice boundary
...
0x0002FC00  haystack_keys[96][28]  = 2,688 bytes (one page = 1,024 bytes, 3 pages)
0x00030000  Staging buffer (XMODEM update, 48 KB)
0x0003C000  Bootloader (16 KB)
0x0003FC00  Bootloader settings
```
Keys fit in 3 × 1 KB flash pages well below the staging buffer start.

### Key Index Persistence
The period counter (key index) must survive power loss. Options:
1. **UICR Customer registers** (0x10001080–0x100010FC, 32 × 32-bit words) — simple, but limited write cycles
2. **Dedicated 1 KB flash page** at a fixed address — use wear-levelling (write incrementally, erase when full)
Option 2 is preferred for longevity.

### Deployment Phase
This is Phase 4 work. STM32 must be stable (Phase 3 verified) before starting nRF51 development. The STM32-side mode command (`0xAA`/`0xAB`) can be stubbed in Phase 1/2/3 app firmware.

### Apple FindMy Network Compatibility
OpenHaystack emulates the proprietary AirTag advertising format. This relies on undocumented Apple protocols reverse-engineered by seemoo-lab. Apple may change the FindMy format at any time. Operation on the Find My network is subject to Apple's terms of service.
