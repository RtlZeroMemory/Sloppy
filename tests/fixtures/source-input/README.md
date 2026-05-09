# Source-Input Fixtures

These fixtures are the manually reviewed source-input lane inventory.
Each case owns a small TypeScript app, `sloppy.json`, fixture metadata, and semantic expected
outputs for Plan, doctor, and diagnostics surfaces.

The fixtures are source inputs. Expected files must be written from the documented contract,
not regenerated from the compiler path being tested.
`tools/windows/test-source-input-fixtures.ps1` executes every `case.json`, validates the
semantic Plan fields, runs `doctor` when a Plan exists, checks expected diagnostics, and
asserts that redaction markers do not appear in process, Plan, or doctor output.

Lane status rules:

- `source-input` is separate from default non-V8 and V8-gated evidence.
- Positive fixtures declare the expected route, Plan fields, and doctor surface.
- Negative fixtures declare the intended failure diagnostic and must not be counted as a
  skipped optional lane.
- `requiresV8`, `requiresPlatform`, and `requiresDependency` describe requirements only; a
  missing optional requirement is `UNAVAILABLE`, not a pass.
