---
name: author-regression-test
description: Scaffold a general-purpose committed gameplay regression test as a Python file under tests/gameplay/regression/. Use when the test does not fit a more specialised anim, parity, or invariant shape.
version: 1.0.0
---

# author-regression-test

Use this skill when the user has a stable repro sequence and wants to preserve it as a committed Python gameplay test.

## Target shape

- Output path: `tests/gameplay/regression/<slug>.py`
- Runner: `soft-ue-cli run-python-script --script-path`
- Style: setup, drive the world, observe, assert, exit non-zero on failure

## Gather before writing

1. Test name and slug.
2. Required setup: map, actor class, tags, initial properties.
3. Input or call sequence.
4. The observation to assert.

Keep the file narrow. If the assertion collapses to one property value, redirect to `author-invariant-test`.

## Template

```python
#!/usr/bin/env python3
"""<short purpose>"""

from __future__ import annotations

from soft_ue_bridge import call


def main() -> int:
    # Setup
    call("pie-session", {"action": "start", "map": "<optional-map>"})
    actor = call("spawn-actor", {"actor_class": "<ActorClass>", "label": "<Label>"})
    call("set-property", {"actor_name": "<Label>", "property_name": "<Prop>", "value": "<Value>"})

    # Drive repro
    call("trigger-input", {"action": "key", "key": "<Key>"})
    call("pie-tick", {"frames": 30})

    # Observe
    value = call("get-property", {"actor_name": "<Label>", "property_name": "<ObservedProp>"})

    # Assert
    if "<expected-fragment>" not in str(value):
        raise AssertionError(f"expected <expected-fragment>, got {value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

## Authoring rules

- Prefer bridge primitives over manual editor steps.
- Prefer stable actor labels or tags so later calls remain deterministic.
- Use `pie-tick` instead of real-time sleeps.
- Keep assertions concrete and readable.

## After writing

Tell the user where the file was written and offer to run it via `run-test`.
