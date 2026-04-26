---
name: author-invariant-test
description: Scaffold a committed C++ Automation Spec for a single-property invariant after setup.
version: 3.0.0
---

# author-invariant-test

Use this skill when the regression can be pinned by reading one property after setup.

## Target shape

- Output path: `Source/<Project>Tests/Private/Invariants/TEST_<Slug>.cpp`
- Pattern: setup, read one property, assert one expected value
- Final artifact: C++ Automation Spec
- Exploration source: the property and expected value already identified during CLI/Python investigation

## Gather before writing

1. Actor identity.
2. Setup steps.
3. Property path.
4. Expected value.
5. How the current session verified that this property is the right signal.
6. Whether `docs/testing/test-infrastructure.md` already defines the source path, map, and naming conventions for this invariant test category.

If the setup or observation grows beyond a simple single-property check, redirect to `author-regression-test`.

## Output pattern

Generate a C++ Automation Spec scaffold that:

- sets up the world or actor
- reads the property directly in C++
- asserts the expected value with a clear test message

## Template

```cpp
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

BEGIN_DEFINE_SPEC(F<SpecName>,
    "<Project>.Invariants.<SpecName>",
    EAutomationTestFlags::ProductFilter | EAutomationTestFlags::EditorContext)
END_DEFINE_SPEC(F<SpecName>)

void F<SpecName>::Define()
{
    Describe("<scenario>", [this]()
    {
        It("preserves the invariant", [this]()
        {
            // Apply setup.
            // Read the target property.
            TestEqual(TEXT("<property assertion>"), <ActualValue>, <ExpectedValue>);
        });
    });
}
```

## Rules

- Keep it to one observed property.
- Prefer exact expected values unless the property is inherently approximate.
- Use the property signal already validated during exploration instead of re-discovering it in the generated scaffold.
- Do not emit a Python test as the main result.
- For UE 5.7-compatible scaffolds, prefer `EAutomationTestFlags::EditorContext` over `ApplicationContextMask`.
- If setup code needs a PIE world, resolve it with a null-guarded `GEditor->GetPIEWorldContext()` lookup rather than emitting `GetWorld()` directly from the spec.
- If `docs/testing/test-infrastructure.md` exists, use its conventions to choose the map, source path, and spec prefix.
- If those conventions are missing, say so and suggest `plan-test-infrastructure` rather than inventing shared invariant-test layout rules.

## After writing

Tell the user the new `.cpp` path and show the Automation command needed to run the generated invariant test.
