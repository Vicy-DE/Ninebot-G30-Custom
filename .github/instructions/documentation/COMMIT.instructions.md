---
applyTo: "**/*.c,**/*.h,**/*.S,**/CMakeLists.txt,**/*.bat,**/*.ps1,**/*.py,**/*.sh"
---

# Commit — Instructions for Copilot

## When to execute

**Final step of every task — after all of the following are confirmed:**

1. Build passes (no errors, no warnings).
2. Tests pass (see [TEST_DOC](TEST_DOC.instructions.md)).
3. Change log updated (`Documentation/CHANGE_LOG.md`).
4. Project doc updated (`Documentation/PROJECT_DOC.md`).
5. Test report saved (`Documentation/Tests/<YYYY-MM-DD>_<feature>.md`).

**NEVER push the commit.** Stage and commit only.

---

## Step 1 — Stage all relevant files

Stage only files that belong to the current task.

```powershell
git add <file1> <file2> ...
```

Files to **always exclude** from staging:
- `bootloader/build/` (all build outputs)
- `firmware/decompiled/build/` (build outputs)
- `firmware/rebuild/build/` (build outputs)
- `*.elf`, `*.bin`, `*.hex`, `*.map`, `*.sfw`
- `*.pyc`, `__pycache__/`
- `.venv/`

---

## Step 2 — Write the commit message

Use the **Conventional Commits** format:

```
<type>(<scope>): <short imperative summary>

<body — explain the what and the why, not the how>

<footer — references, breaking changes>
```

### Type

| Type | When to use |
|---|---|
| `feat` | New feature or capability |
| `fix` | Bug fix |
| `refactor` | Code restructuring without behaviour change |
| `test` | Adding or updating tests |
| `docs` | Documentation only |
| `chore` | Build, tooling, config changes |
| `perf` | Performance improvement |
| `reverse` | Reverse engineering finding or documentation |

### Scope (use the affected module)

`bootloader` · `ble` · `bms` · `nrf51` · `protocol` · `flasher` · `signing` · `analysis` · `docs` · `vesc`

### Summary line rules
- Imperative mood, present tense: "add", "fix", "remove" — not "added" or "fixes".
- No trailing period.
- Max 72 characters.

---

## Step 3 — Commit

```powershell
git commit -m "<summary line>" -m "<body>"
```

---

## Rules

- **MUST** verify all prerequisites (build, tests, docs) before committing.
- **MUST** use Conventional Commits format.
- **MUST NOT** commit build artefacts or generated binaries.
- **MUST NOT** run `git push` at any point — local commit only.
- **SHOULD** keep each commit focused on a single logical change.
