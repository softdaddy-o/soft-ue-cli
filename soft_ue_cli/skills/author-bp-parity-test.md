---
name: author-bp-parity-test
description: Scaffold a Blueprint-to-C++ parity test under tests/gameplay/parity/ using a committed input sweep and golden outputs. Use when a Blueprint implementation is the source of truth and the C++ port must match it.
version: 1.0.0
---

# author-bp-parity-test

Use this skill when the user wants to prove a C++ port matches the Blueprint behaviour for the same inputs.

## Target shape

- Output files:
  - `tests/gameplay/parity/<slug>.py`
  - `tests/gameplay/parity/<slug>.inputs.json`
  - `tests/gameplay/parity/<slug>.golden.json`
- Capture golden data from the Blueprint first, then replay against the C++ path.

## Gather before writing

1. Blueprint asset path or class path used as source of truth.
2. Function name.
3. Input sweep rows.
4. C++ target path or equivalent callable.
5. Float tolerance, if needed.

## Golden capture pattern

Use the CLI batch sweep mode to capture expected outputs:

```bash
soft-ue-cli call-function \
  --class-path /Game/Blueprints/BP_Example \
  --function-name EvaluateFoo \
  --use-cdo \
  --batch-json tests/gameplay/parity/foo.inputs.json \
  --output tests/gameplay/parity/foo.golden.json
```

## Test file pattern

```python
#!/usr/bin/env python3
"""<short purpose>"""

from __future__ import annotations

import json
from pathlib import Path

from soft_ue_bridge import call


def main() -> int:
    root = Path(__file__).resolve().parent
    inputs = json.loads((root / "<slug>.inputs.json").read_text(encoding="utf-8"))
    golden = json.loads((root / "<slug>.golden.json").read_text(encoding="utf-8"))

    actual = call(
        "call-function",
        {
            "class_path": "<C++ClassPath>",
            "function_name": "<FunctionName>",
            "use_cdo": True,
            "batch_json": str(root / "<slug>.inputs.json"),
        },
    )

    if actual != golden:
        raise AssertionError("parity mismatch between golden and C++ output")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

## Rules

- Do not capture the golden from a suspect Blueprint. If the BP might already be wrong, debug that first.
- Commit inputs, golden, and test file together.
- If exact equality is too strict, add a small float tolerance comparison instead of rewriting the golden.

## After writing

Tell the user which three files were created and offer to run the Python test via `run-test`.
