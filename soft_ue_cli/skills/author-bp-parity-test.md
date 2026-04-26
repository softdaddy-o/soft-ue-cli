---
name: author-bp-parity-test
description: Scaffold a committed C++ Automation Spec for Blueprint-to-C++ parity checks using captured inputs and expected outputs.
version: 3.0.0
---

# author-bp-parity-test

Use this skill when the user wants to prove a C++ port matches Blueprint behavior for the same inputs.

## Target shape

- Output files:
  - `Source/<Project>Tests/Private/Parity/TEST_<Slug>.cpp`
  - optional committed fixtures such as `<slug>.inputs.json` and `<slug>.golden.json`
- Final artifact: C++ Automation Spec
- Exploration source: CLI/Python sessions used to identify call shapes, inputs, and expected outputs

## Gather before writing

1. Blueprint asset path or class path used as source of truth.
2. Function name.
3. Input sweep rows.
4. C++ target path or equivalent callable.
5. Float tolerance, if needed.
6. How the current session validated the parity signal.
7. Whether `docs/testing/test-infrastructure.md` already defines the destination source path and naming conventions for parity tests.

## Golden capture

Capture or confirm the golden data during exploration first. CLI capture is acceptable here as exploration tooling when it is the fastest way to confirm the expected outputs.

Possible sources:

- ad hoc CLI capture during investigation
- a temporary Python exploration script
- pre-existing committed fixture data

The committed output from this skill is still a C++ Automation Spec, not a Python parity test.

## Output pattern

Generate a C++ Automation Spec scaffold that:

- loads or embeds the input rows
- loads or embeds the expected outputs
- invokes the C++ target for each row
- compares actual vs expected, with tolerance handling if needed
- reports row-level failures clearly

## Template

```cpp
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(F<SpecName>,
    "<Project>.Parity.<SpecName>",
    EAutomationTestFlags::ProductFilter | EAutomationTestFlags::EditorContext)
END_DEFINE_SPEC(F<SpecName>)

void F<SpecName>::Define()
{
    Describe("<parity scenario>", [this]()
    {
        It("matches the expected outputs", [this]()
        {
            // Load fixture data or define it inline.
            // Invoke the C++ target for each row.
            // Compare results against the captured golden values.
            TestTrue(TEXT("<parity assertion>"), true);
        });
    });
}
```

## Rules

- Do not emit a Python parity test as the main result.
- Keep the committed artifact in C++ even if the golden was discovered with CLI/Python tooling.
- If exact equality is too strict, make the tolerance explicit in the generated scaffold.
- Make fixture ownership clear: what is committed, what was just exploratory.
- For UE 5.7-compatible scaffolds, prefer `EAutomationTestFlags::EditorContext` over `ApplicationContextMask`.
- If the generated parity test needs world access, do not emit a bare `GetWorld()` call from the spec body. Resolve the PIE world through `GEditor->GetPIEWorldContext()` with a null guard.
- If `docs/testing/test-infrastructure.md` exists, use its directory and naming conventions for parity tests.
- If conventions are missing, say so and suggest `plan-test-infrastructure` rather than inventing project-wide parity layout rules.

## After writing

Tell the user which committed files were created and show the Automation command needed to run the generated parity test.
