# Source-Input Run and Dev Loop Plan

Status: ENGINE-02.E implementation source of truth. Reuse #259/#302 and #316/#345-#349;
do not create a duplicate DEVLOOP EPIC unless the owner explicitly asks.

## Current Workflow

The explicit artifact workflow remains supported:

```powershell
sloppyc build <source> --out <artifacts>
sloppy run --artifacts <artifacts>
```

Direct source input now compiles first and then runs that same artifact path.

## Desired Workflow

The direct source-input command is:

```powershell
sloppy run app.js
sloppy run
```

The command performs an explicit compiler handoff, produces artifacts, validates the Plan,
bundle, and source map, then runs the same artifact path that is already proven. `.ts` is
still rejected by the current compiler; the CLI hands it to `sloppyc` so the compiler
diagnostic remains authoritative.

The default no-argument form uses `sloppy.json` when present:

```json
{
  "entry": "app.js",
  "outDir": ".sloppy",
  "environment": "Development"
}
```

## Build/Cache/Artifact Policy

- Positional source input writes to `.sloppy/cache/dev/source-input` and rebuilds every run
  in ENGINE-02.E.
- `sloppy.json` writes to `outDir`, defaulting to `.sloppy`, and rebuilds every run in
  ENGINE-02.E.
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

Watch mode, rebuild policy, and hot-reload behavior are separate decisions under the
existing CLI/dev-loop issue family (#316/#345-#349). The first source-input slice should
prefer direct build-and-run behavior over a hidden watch loop.

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
