# Source-Input Run and Dev Loop Plan

Status: ENGINE-02.E implementation source of truth. Reuse #259 for parent compiler/source
input context and #316 for parent CLI/dev-loop context. #302/#346 cover the current
source-input handoff; #345/#349 remain open for artifact inspection and watch/dev-loop
decisions. Do not create a duplicate DEVLOOP EPIC unless the owner explicitly asks.

## Current Workflow

The explicit artifact workflow remains supported:

```powershell
sloppy build
sloppy build <source>
sloppyc build <source> --out <artifacts>
sloppy run --artifacts <artifacts>
```

Direct source input now compiles first and then runs that same artifact path.
FRAMEWORK-01.B also passes the selected environment and selected CLI runtime overrides
through that compiler handoff so source-input runs see the same application config model
as explicit `sloppyc build`.

## Desired Workflow

The direct source-input command is:

```powershell
sloppy run app.js
sloppy run app.ts
sloppy run src/main.ts
sloppy run
```

The command performs an explicit compiler handoff, produces artifacts, validates the Plan,
bundle, and source map, then runs the same artifact path that is already proven. `.ts`
source input is accepted for the compiler-owned Framework subset documented in
`docs/compiler-supported-syntax.md`; unsupported TypeScript shapes still fail through
`sloppyc` diagnostics.

The default no-argument form uses `sloppy.json` when present:

```json
{
  "entry": "src/main.ts",
  "outDir": ".sloppy",
  "environment": "Development"
}
```

`sloppy.json` remains project build/run config. Project-mode application configuration
lives in `appsettings.json` and `appsettings.{Environment}.json` next to `sloppy.json`;
positional source-input configuration stays next to the source entry. The
environment from `sloppy.json` selects the overlay unless `sloppy run --environment <name>`
or `sloppy build --environment <name>` is supplied, in which case the CLI value wins.
`--artifacts <dir>` remains an explicit
artifact/debug path and does not require `sloppy.json` or appsettings files.

## Build/Cache/Artifact Policy

- Positional source input writes to `.sloppy/cache/dev/source-input` and rebuilds every run
  in ENGINE-02.E.
- `sloppy.json` writes to `outDir`, defaulting to `.sloppy`, and rebuilds every run in
  ENGINE-02.E.
- `sloppy build` emits and validates artifacts without entering V8; runtime execution is
  still owned by `sloppy run` or `sloppy run --artifacts`.
- Complete cache reuse remains deferred. Future cache keys must include source and
  supported import hashes, compiler/runtime/stdlib identity, target platform/engine,
  environment, and relevant feature/options.
- Fail closed when cache validation is uncertain.
- Keep generated artifacts out of source control.
- Preserve `sloppy run --artifacts` as the explicit debugging and conformance path.
- Users can safely delete `.sloppy/` and `.sloppy/cache/`; they are generated outputs.

## Diagnostics and Source Maps

- Compiler diagnostics must report source-input command-boundary failures clearly.
- Runtime diagnostics should consume source maps once the existing diagnostics issues land.
- Artifact-path diagnostics and source-input diagnostics must be reported as separate
  evidence lanes until both are proven.

## Watch/Dev Loop Later

Watch mode, richer artifact inspection, and hot-reload behavior remain separate decisions
under the existing CLI/dev-loop issue family (#316/#345/#349). The completed source-input
slice intentionally prefers direct build-and-run behavior over a hidden watch loop.

## Package Outside-Checkout Relationship

Source-input smoke should extend the package outside-checkout evidence only after direct
source-input behavior works locally. Package smoke must keep non-V8 and V8-gated evidence
separate.

## Non-Goals

- No package manager.
- No TypeScript typechecker.
- No npm resolution.
- No Node compatibility.
- No hot reload unless later scoped.
- No public alpha docs from source-input alone.
- No full TypeScript typechecker in the first source-input handoff.
## ENGINE-14 Update

Source-input run now accepts the compiler-owned module subset: static relative function
modules, `"sloppy"`, and `"sloppy/providers/sqlite"`. It still rebuilds into the same
`.sloppy/cache/dev/source-input` artifact directory and executes through the existing
artifact path. Cache reuse, watch mode, Node/npm resolution, package-manager behavior, and
full TypeScript typechecking remain deferred.
