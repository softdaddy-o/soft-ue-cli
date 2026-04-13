---
name: author-invariant-test
description: Scaffold a single-property invariant test as a Python file under tests/gameplay/invariants/. Use when the whole assertion is one property value after a specific setup.
version: 1.0.0
---

# author-invariant-test

Use this skill when the regression can be pinned by reading one property after setup.

## Target shape

- Output path: `tests/gameplay/invariants/<slug>.py`
- Pattern: setup, read one property, assert one expected value

## Gather before writing

1. Actor identity.
2. Setup steps.
3. Property path.
4. Expected value.

If the setup or observation grows beyond a simple single-property check, redirect to `author-regression-test`.

## Template

```python
#!/usr/bin/env python3
"""<short purpose>"""

from __future__ import annotations

from soft_ue_bridge import call


def main() -> int:
    actor_name = "<ActorLabel>"

    call("pie-session", {"action": "start", "map": "<optional-map>"})
    call("spawn-actor", {"actor_class": "<ActorClass>", "label": actor_name})
    call("set-property", {"actor_name": actor_name, "property_name": "<Prop>", "value": "<SetupValue>"})

    result = call("get-property", {"actor_name": actor_name, "property_name": "<ObservedProp>"})
    if str(result) != "<ExpectedValue>":
        raise AssertionError(f"expected <ExpectedValue>, got {result}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

## Rules

- Keep it to one observed property.
- Prefer exact expected values unless the property is inherently approximate.
- Use deterministic setup and avoid unnecessary world ticking.

## After writing

Tell the user the new file path and offer to run it via `run-test`.
