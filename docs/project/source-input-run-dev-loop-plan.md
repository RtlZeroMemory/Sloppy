# Source-Input Run and Dev Loop Plan

Status: planning source of truth. Reuse #259/#302 and #316/#345-#349; do not create a
duplicate DEVLOOP EPIC unless the owner explicitly asks.

## Current Workflow

The current supported workflow is explicit and artifact-based:

```powershell
sloppyc build <source> --out <artifacts>
sloppy run --artifacts <artifacts>
```

This remains honest because `src/main.c` still rejects direct source input and instructs
users to use `--artifacts`.

## Desired Workflow

The desired post-Core workflow is a direct source-input command such as:

```powershell
sloppy run app.ts
sloppy run
```

The command should perform an explicit compiler handoff, produce or reuse artifacts, and
then run the same artifact path that is already proven.

The default no-argument form uses `sloppy.json` when present:

```json
{
  "entry": "app.ts",
  "outDir": ".sloppy",
  "environment": "Development"
}
```

## Build/Cache/Artifact Policy

- Cache artifacts under a deterministic tool-owned directory.
- Include source path, compiler version, relevant flags, and runtime mode in cache keys.
- Detect stale artifacts before execution.
- Keep generated artifacts out of source control.
- Preserve `sloppy run --artifacts` as the explicit debugging and conformance path.
- Prefer `.sloppy/cache/dev` or a configured artifact cache for source-input development
  artifacts.
- Source-input run should land before major example hardening or public-ish docs work.

## Diagnostics and Source Maps

- Compiler diagnostics must report source-input paths, not cache internals.
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
