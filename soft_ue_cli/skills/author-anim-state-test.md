---
name: author-anim-state-test
description: Scaffold a committed C++ Automation Spec for animation-state regressions. Use when the signal lives in anim state machines, montages, selected databases, or blend weights.
version: 3.0.0
---

# author-anim-state-test

Use this skill when the user needs a committed C++ regression test for animation behavior.

## Target shape

- Output path: `Source/<Project>Tests/Private/Anim/TEST_<Slug>.cpp`
- Primary signal: animation state machines, montages, selected databases, blend weights, or related anim properties
- Exploration source: CLI/Python probing that identified the key anim signal
- Final artifact: C++ Automation Spec

## Gather before writing

1. Which actor or anim instance to inspect.
2. Which input or setup sequence drives the bug.
3. What frame or time window matters.
4. What exact anim-state condition should hold or should stop holding.
5. Which property, state name, montage, or database the current session already proved is the best signal.
6. Whether `docs/testing/test-infrastructure.md` already defines the map, source path, and spec prefix for anim/locomotion tests.

## Output pattern

Generate a C++ Automation Spec scaffold with:

- map setup in `BeforeEach`
- latent steps for character switching, aiming, walking, stopping, or settling
- explicit lookup of the animation object or property under test
- `TestTrue` / `TestFalse` / `TestEqual` assertions against the discovered signal

## Template

```cpp
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

BEGIN_DEFINE_SPEC(F<SpecName>,
    "<Project>.Anim.<SpecName>",
    EAutomationTestFlags::ProductFilter | EAutomationTestFlags::EditorContext)
    UObject* TargetAnimObject = nullptr;
END_DEFINE_SPEC(F<SpecName>)

void F<SpecName>::Define()
{
    Describe("<scenario>", [this]()
    {
        BeforeEach([this]()
        {
            AutomationOpenMap(TEXT("<MapPath>"));
            ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(1.0f));
        });

        It("<expected animation behavior>", [this]()
        {
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]() -> bool
            {
                // Recreate the repro discovered during exploration.
                return true;
            }));

            ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(<WaitSeconds>));

            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]() -> bool
            {
                // Read the anim signal discovered during exploration.
                TestFalse(TEXT("<forbidden anim condition>"), false);
                return true;
            }));
        });
    });
}
```

## Notes

- Prefer the exact property or signal already validated during exploration, for example `CurrentSelectedDatabase`, current state name, or a blend weight.
- Make the latent waits explicit and tied to the repro steps.
- If the exploration needed repeated runtime inspection with CLI tools, use that result to choose the final C++ assertion rather than trying to reproduce the exploration tooling itself.
- For UE 5.7-compatible scaffolds, prefer `EAutomationTestFlags::EditorContext` over `ApplicationContextMask`.
- If the generated test needs a runtime world, use a null-guarded `GEditor->GetPIEWorldContext()` lookup rather than emitting `GetWorld()` directly from the spec.
- If `docs/testing/test-infrastructure.md` exists, use its conventions to select the map, output directory, and spec prefix.
- If those conventions do not exist yet, say so and suggest `plan-test-infrastructure` instead of inventing a shared anim test map on the fly.

## After writing

Tell the user the new `.cpp` path and show the Automation command needed to run the generated test.
