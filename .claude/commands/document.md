---
description: Update CHANGE_LOG.md and PROJECT_DOC.md for the current change
---

Document the verified change. Do BOTH:

**1. `Documentation/CHANGE_LOG.md`** — prepend a new entry (newest first):
```markdown
## [YYYY-MM-DD] <short title>
### What was changed
- <file/component> — <concrete change>
### Why it was changed
<reason>
### What it does / expected behaviour
<observable effect>
### Verified
- Build: OK/FAIL · Flash: OK/FAIL (IAP/XMODEM/SWD) · UART: OK/FAIL (<obs>) · Functional: OK/FAIL
```
Use past tense for "what was changed", present tense for "what it does". One line per modified file. Never skip, even for trivial changes.

**2. `Documentation/PROJECT_DOC.md`** — update "Last updated", section 4 (Key Modules) if files/responsibilities changed, section 8 (Known Limitations), and add a one-line section 9 (Revision History) entry. Link to CHANGE_LOG rather than duplicating.
