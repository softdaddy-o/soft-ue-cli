---
name: run-test
description: Run one or more committed gameplay Python tests under tests/gameplay/ and summarise pass or fail. Use after authoring a test or when the user wants to execute an existing gameplay regression file or directory.
version: 1.0.0
---

# run-test

Use this skill when the user wants to execute committed gameplay Python tests.

## Scope

- Supported targets:
  - `tests/gameplay/<file>.py`
  - `tests/gameplay/<directory>/`
  - `tests/gameplay/`
- Transport: `soft-ue-cli run-python-script --script-path`

This skill is for Python gameplay tests only. It does not run C++ Automation Spec tests.

## Single-file pattern

```bash
soft-ue-cli run-python-script --script-path tests/gameplay/anim/walk_stop_while_aiming.py
```

## Directory pattern

When a whole category should run, expand the directory into files and run each one in a stable order:

```bash
for %f in (tests\gameplay\anim\*.py) do soft-ue-cli run-python-script --script-path "%f"
```

Or use a Python helper to enumerate the tree if shell globbing is inconvenient.

## Summary format

Report results in a compact table:

| Test | Result | Note |
|---|---|---|
| `tests/gameplay/anim/foo.py` | PASS | exit code 0 |
| `tests/gameplay/anim/bar.py` | FAIL | assertion mismatch |

If any test fails, stop pretending it is flaky. Surface the failing file and recommend debugging or rewriting that test.

## Rules

- Do not silently retry failures.
- Do not mix gameplay Python tests with unrelated scripts.
- Keep the exact failing stdout or error message available for follow-up.

## After running

- If all tests pass, say so directly.
- If one fails, point to the failing path and suggest investigating the repro or assertion before rerunning.
