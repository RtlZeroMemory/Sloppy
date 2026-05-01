# Compiler Inference Issue Index

Status: source-of-truth issue map for COMPILER-30.

## Issues

| Kind | Issue | Title |
| --- | --- | --- |
| EPIC | #460 | COMPILER-30: Slop Application Compiler Inference Engine |
| Task | #461 | COMPILER-30.A: Compiler Module Architecture and Test Harness |
| Task | #462 | COMPILER-30.B: Supported Source Subset Parser and Import Resolver |
| Task | #463 | COMPILER-30.C: Symbol Binding and Slop DSL Recognition |
| Task | #464 | COMPILER-30.D: Route, Group, and Function Module Extraction |
| Task | #465 | COMPILER-30.E: Provider, Config, Schema, and Results Metadata Extraction |
| Task | #466 | COMPILER-30.F: Function Effect Summaries and Callgraph Inference |
| Task | #467 | COMPILER-30.G: Capability Inference and Escape Hatches |
| Task | #468 | COMPILER-30.H: Plan Completeness and Static Validation |
| Task | #469 | COMPILER-30.I: Strong Plan Emission and Compatibility Goldens |
| Task | #470 | COMPILER-30.J: Large Realistic Fixture Suite |

## Related Existing Issues

- #259 remains the parent source-input/compiler pipeline epic.
- #302 is the completed direct source-input run handoff and should not be duplicated.
- #318 remains the Strong Plan strategic consumer epic.
- #355 consumes COMPILER-30 graph output.
- #356 consumes COMPILER-30 validation/completeness.
- #357 consumes COMPILER-30 Plan completeness/explainability.
- #358 consumes response/body/schema/provider metadata from COMPILER-30.
- #359 coordinates Plan versioning with #469.

## Implementation Order

1. #461 COMPILER-30.A.
2. #462/#463 COMPILER-30.B/C.
3. #464 COMPILER-30.D.
4. #465 COMPILER-30.E.
5. #466/#467 COMPILER-30.F/G.
6. #468/#469 COMPILER-30.H/I.
7. #470 COMPILER-30.J.

Recommended first implementation PR: #461, because compiler module boundaries and fixture
harness shape should land before inference behavior spreads across the compiler.

## Dependency Map

```text
#461
  -> #462 import/source graph
  -> #463 symbol/DSL recognition
      -> #464 routes/groups/modules
      -> #465 provider/config/schema/results
          -> #466 callgraph/effects
          -> #467 capability inference
              -> #468 completeness/validation
              -> #469 strong Plan emission/version goldens
                  -> #470 broad realistic fixture suite
```

#355-#359 depend on COMPILER-30 output but should not implement compiler inference.

## Parallelism Guidance

- Do not parallelize #461.
- #462 and #463 may be grouped once #461 is merged.
- #464 and #465 may run in parallel only after the symbol/DSL contracts from #462/#463 are
  stable.
- #466 and #467 depend on #464/#465 and should be grouped or sequenced tightly.
- #468/#469 depend on #466/#467.
- #470 can grow alongside each PR, but the final broad suite should land after #468/#469.

## Must Not Run In Parallel

- Do not implement capability inference before provider/effect summaries are stable.
- Do not require manual `uses` metadata for normal Minimal API/function-module/repository/
  service patterns that can be statically resolved.
- Do not implement Plan completeness before route/provider/config/body/response metadata
  and effect summaries exist.
- Do not update Strong Plan consumers to rely on metadata before #469 defines emission and
  compatibility goldens.
- Do not migrate broad framework API shapes while compiler DSL recognition is still
  unstable.

## Non-goals For This Issue Set

- No runtime feature implementation.
- No framework API implementation.
- No doctor/audit/OpenAPI consumer implementation.
- No arbitrary TypeScript inference.
- No Node/npm/package-manager compatibility.
- No generated/build artifacts except intentional fixtures/goldens.
