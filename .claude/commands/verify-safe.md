---
description: Gate every firmware change — verify it's safe (no brick), the update path is preserved, and secure boot is sound
---

Run the firmware safety gate after **every firmware change** (and before any flash):

```
python tools/verify_firmware_safe.py        # use --fast to skip the arm bootloader builds
```

It must end with **`VERDICT: SAFE to flash, UPDATE path preserved, SECURE BOOT sound.`**
The gate chains:

1. **SAFE (no brick)** — `dashboard_sim`: the firmware never writes the bootloader (`0x08000000–0x08000FFF`)
   or option-byte/RDP flash, keeps a valid vector table, and the 5000 ms watchdog resets **and recovers**.
2. **UPDATE PATH PRESERVED** — the app image is bootloader-loadable (valid vector @ `0x08001000`) and never
   touches the bootloader region, so the stock IAP / bootloader can always reflash it (you can't get locked
   out); the secure bootloader still builds + verifies.
3. **REGRESSION** — full `ctest` host suite (protocol, bridge, BLE session, …) stays green.
4. **SECURE BOOT** — `tools/verify_secureboot.py`: a genuinely-signed image is accepted, every tampering
   (firmware byte, signature byte, wrong key, wrong magic, wrong target) is rejected, and both bootloader
   targets (STM32 + nRF51) build.

If any guarantee fails: **do not flash**. Fix the cause, don't bypass the gate. Then re-run, `/document`,
and `/commit`. (`verify_secureboot.py` needs the `cryptography` Python package: `pip install cryptography`.)
