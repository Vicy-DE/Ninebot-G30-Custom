---
description: Stage relevant files and write a Conventional Commit (never push)
---

Commit the current task. Prerequisites: build passes, tests pass, CHANGE_LOG + PROJECT_DOC updated, test report saved.

1. **Stage only task-relevant files.** Exclude build outputs (`bootloader/build/`, `firmware/**/build/`),
   `*.elf/*.bin/*.hex/*.map/*.sfw`, `__pycache__/`, `.venv/`, signing keys.
2. **Message — Conventional Commits:**
   - type: `feat`/`fix`/`refactor`/`test`/`docs`/`chore`/`perf`/`reverse`
   - scope: `bootloader`/`ble`/`bms`/`nrf51`/`protocol`/`flasher`/`signing`/`analysis`/`docs`/`vesc`
   - summary: imperative, ≤72 chars, no trailing period. Body explains what + why.
3. `git commit -m "<summary>" -m "<body>"`.
4. **NEVER `git push`** (also blocked by `.claude/settings.json`). Local commit only. Keep commits focused.
