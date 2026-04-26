---
name: author-anim-state-test
description: Scaffold an animation-state regression test as a Python file under tests/gameplay/anim/. Use when the regression is visible in inspect-anim-instance output such as current_state, previous_state, montage past_end, or blend weights.
version: 1.0.0
---

# author-anim-state-test

Use this skill when the user needs a committed regression test for animation behaviour.

## Target shape

- Output path: `tests/gameplay/anim/<slug>.py`
- Primary signal: `inspect-anim-instance`
- Typical assertions:
  - state machine `current_state`
  - state machine `previous_state`
  - montage `past_end`
  - named blend weights

## Gather before writing

1. Which actor or anim instance to inspect.
2. Which input or setup sequence drives the bug.
3. What frame window matters.
4. What exact anim-state condition should hold or should stop holding.

## Template

```python
#!/usr/bin/env python3
"""<short purpose>"""

from __future__ import annotations

from soft_ue_bridge import call


def main() -> int:
    actor_tag = "TestCharacter"

    call("pie-session", {"action": "start", "map": "<optional-map>"})
    call("spawn-actor", {"actor_class": "<ActorClass>", "label": actor_tag})
    call("trigger-input", {"action": "key", "key": "<Key>"})
    call("pie-tick", {"frames": 30})

    trace = []
    for _ in range(20):
        call("pie-tick", {"frames": 1})
        trace.append(call("inspect-anim-instance", {"actor_tag": actor_tag}))

    matched = [
        frame
        for frame in trace
        if frame["state_machines"][0]["current_state"] == "<ExpectedState>"
    ]
    if not matched:
        raise AssertionError("expected at least one frame in <ExpectedState>")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

## Notes

- Prefer a short rolling trace over a single snapshot when state persistence matters.
- Treat `slots` as optional and unstable unless the project has explicitly standardised on them.
- Use stable actor tags so `inspect-anim-instance` keeps resolving the same target.

## After writing

Tell the user the new file path and offer `run-test tests/gameplay/anim/<slug>.py`.
