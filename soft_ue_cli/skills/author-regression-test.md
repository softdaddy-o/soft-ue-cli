---
name: author-regression-test
description: Scaffold a committed C++ Automation Spec for a general gameplay regression. Use when the repro is broader than a single invariant or a pure animation-state check.
version: 3.0.0
---

# author-regression-test

Use this skill when the user has a stable repro sequence and wants to preserve it as a committed C++ gameplay regression test.

## Target shape

- Output path: `Source/<Project>Tests/Private/Regression/TEST_<Slug>.cpp`
- Test style: `BEGIN_DEFINE_SPEC` / `Describe` / `It` with latent setup and assertions
- Dependencies: project-native test module only
- Exploration inputs: CLI, bridge, and Python findings from the current session

## Gather before writing

1. Test name and slug.
2. Required setup: map, actor class, tags, initial properties.
3. Input or call sequence.
4. The observation to assert.
5. Which signals or properties were already validated during CLI/Python exploration.
6. Whether `docs/testing/test-infrastructure.md` already defines the map, output directory, and spec prefix for this category.

Keep the file narrow. If the assertion collapses to one property value, redirect to `author-invariant-test`.

## Output pattern

Generate a C++ Automation Spec scaffold that includes:

- includes for the relevant gameplay classes and automation headers
- `BEGIN_DEFINE_SPEC(...)`
- map loading or world setup in `BeforeEach`
- latent waits or helper commands where needed
- one focused assertion block in `It(...)`
- minimal cleanup in `AfterEach`

## Template

```cpp
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

BEGIN_DEFINE_SPEC(F<SpecName>,
    "<Project>.<Category>.<SpecName>",
    EAutomationTestFlags::ProductFilter | EAutomationTestFlags::EditorContext)
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

        It("<expected behavior>", [this]()
        {
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]() -> bool
            {
                // Apply the setup or repro steps discovered during exploration.
                return true;
            }));

            ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(<WaitSeconds>));

            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]() -> bool
            {
                // Read the key signal found during exploration and assert it.
                TestTrue(TEXT("<assertion>"), true);
                return true;
            }));
        });
    });
}
```

## Rules

- Treat CLI/Python work as exploration input, not the committed artifact.
- Do not emit Python test files as the main result.
- Prefer explicit latent commands over vague comments about waiting.
- Carry over the exact runtime signal learned during exploration into the C++ assertion.
- If the exploration relied on bridge-only helper behavior, translate the behavior into project-native C++ calls rather than keeping the bridge dependency.
- For UE 5.7-compatible scaffolds, prefer `EAutomationTestFlags::EditorContext` over `ApplicationContextMask`.
- If the generated spec needs a PIE world pointer, do not emit a bare `GetWorld()` call from the spec body. Use a null-guarded `GEditor->GetPIEWorldContext()` lookup instead.
- If `docs/testing/test-infrastructure.md` exists, use its map, directory, and naming conventions instead of inventing new ones.
- If project-wide conventions are missing, say so explicitly and suggest `plan-test-infrastructure` rather than silently making them up.

## After writing

Tell the user where the `.cpp` file was written and show the Automation command needed to build and run that test.
