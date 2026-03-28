---
applyTo: "**/*.c,**/*.h,**/*.S,**/CMakeLists.txt,**/*.bat,**/*.ps1,**/*.py,**/*.sh"
---

# Coding Conventions — Scripts

## RULE: Script Locations

Scripts are organized by purpose:

| Directory | Purpose | Examples |
|-----------|---------|----------|
| `tools/flasher/` | Firmware flashing and UART communication | `ninebot_flasher.py`, `xmodem_send.py` |
| `tools/signing/` | Firmware signing and key management | `sign_firmware.py`, `generate_keys.py` |
| `tools/analysis/` | Firmware analysis and reverse engineering | `disassemble_firmware.py`, `analyze_bootloader.py` |
| `Target/` | Hardware test/debug scripts | `test_ble_boot.py`, `capture_protocol.py` |

**MUST:** Place new scripts in the appropriate directory based on their purpose.

### Exceptions (do NOT move)

| Path pattern | Reason |
|---|---|
| `bootloader/**` | Embedded source — not scripts |
| `firmware/**` | Firmware source — not scripts |
| `lib/**` | Library source — not scripts |

---

## RULE: Use Script-Relative Paths

When a script needs to reference files relative to the project root, use the script's own location as the anchor — **never** `os.getcwd()` or `$PWD`.

### Python

```python
import os
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.join(SCRIPT_DIR, "..", "..")
```

### PowerShell

```powershell
$ScriptDir = Split-Path $MyInvocation.MyCommand.Path -Parent
$ProjectRoot = Split-Path (Split-Path $ScriptDir -Parent) -Parent
```

---

## RULE: Naming Convention

- Use `snake_case` for Python scripts: `ninebot_flasher.py`, `xmodem_send.py`
- Use `kebab-case` for shell scripts: `build-all.ps1`
- Exception: legacy scripts may keep their established names

---

## RULE: Never Create Temporary Scripts in the Workspace Root

- Use the terminal directly for one-off commands
- If a diagnostic script is needed, place it in `tools/analysis/` with a `diag_` prefix

---

## tools/ Inventory

| Script | Directory | Language | Purpose |
|--------|-----------|----------|---------|
| `ninebot_flasher.py` | flasher | Python | Flash via Ninebot IAP protocol |
| `xmodem_send.py` | flasher | Python | Flash via XMODEM-CRC |
| `initial_flash.py` | flasher | Python | First-time SWD flash helper |
| `update_bootloader.py` | flasher | Python | Bootloader self-update |
| `sign_firmware.py` | signing | Python | Sign firmware with ECDSA-P256 |
| `generate_keys.py` | signing | Python | Generate ECDSA key pair |
| `verify_firmware.py` | signing | Python | Verify .sfw signature |
| `disassemble_firmware.py` | analysis | Python | Disassemble .bin files |
| `analyze_bootloader.py` | analysis | Python | Analyze bootloader binary |
| `deep_disassemble.py` | analysis | Python | Deep analysis of firmware |

## Target/ Inventory

| Script | Language | Purpose |
|--------|----------|---------|
| (to be created) | Python | Hardware integration tests |
