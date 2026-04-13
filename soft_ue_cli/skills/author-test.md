---
name: author-test
description: Choose the right committed gameplay test type, then route to the matching authoring sub-skill. Use when the user wants to write a new gameplay regression test but has not yet picked the best test shape.
version: 1.0.0
---

# author-test

Use this skill when the user wants to create a new committed gameplay test and needs help choosing the right shape first.

This is a routing skill. It does not scaffold files itself. Its job is to classify the request, recommend the best test type, and then hand off to one of the authoring sub-skills below.

## Do not use for

- Running existing tests. Use `run-test`.
- General bug investigation without a stable repro yet.
- UE Functional Test maps or C++ Automation Spec authoring. Those are not covered by this CLI skill set yet.

## Supported test types

| Skill | Best fit | Output location |
|---|---|---|
| `author-regression-test` | General reproducible sequence plus one or more observations | `tests/gameplay/regression/` |
| `author-anim-state-test` | Anim state machine, montage, or blend-weight regressions | `tests/gameplay/anim/` |
| `author-bp-parity-test` | BP to C++ parity checks using golden inputs and outputs | `tests/gameplay/parity/` |
| `author-invariant-test` | Single property invariant after setup | `tests/gameplay/invariants/` |

## Routing checklist

Ask only the minimum needed to classify the request:

1. What exact behaviour should stay true?
2. What setup is required before the assertion?
3. Is the signal an anim-state trace, a BP/C++ parity comparison, a single property value, or a broader runtime observation?

## Recommendation rules

- If the signal lives in `inspect-anim-instance` output, choose `author-anim-state-test`.
- If the goal is proving a Blueprint port matches C++ for the same inputs, choose `author-bp-parity-test`.
- If the assertion is a single property read after setup, choose `author-invariant-test`.
- Otherwise choose `author-regression-test`.

## Response pattern

State the recommendation explicitly before handing off:

> Based on your repro, the best fit is `author-anim-state-test` because the assertion lives in animation-state data across a tick window. If you want, I will use that sub-skill to scaffold the committed test.

If the user agrees, retrieve the chosen sub-skill and follow it. After the sub-skill finishes and the file exists, offer to run it immediately via `run-test`.
