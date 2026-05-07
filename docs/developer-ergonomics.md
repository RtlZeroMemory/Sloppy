# Developer Ergonomics

Sloppy's product wedge is a deliberate backend developer loop: compile known application
shape, validate artifacts, run a bounded native app-host, and report clear diagnostics when
the current subset cannot support a behavior.

## Current Developer Loop

The supported source-input path is:

```powershell
sloppyc build examples/compiler-hello/app.js --out .sloppy
sloppy run --artifacts .sloppy --once GET /
sloppy run examples/compiler-hello/app.js --once GET /
```

`sloppy run <source.js>` compiles first and then enters the artifact runtime. Runtime
execution requires a V8-enabled build. Default non-V8 evidence proves the default native,
compiler, CTest, and scanner path only.

## App API Shape

The intended app-facing shape is a small app-host facade:

```js
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/", () => Results.text("Sloppy is alive"));

export default app;
```

The bootstrap stdlib also exposes builder, module, config, logging, service, result,
schema, provider, and core API facades. Some examples are API-shape fixtures rather than
runnable artifact apps; public docs and example READMEs must say which lane they belong to.

## Implemented Subset

Current implementation supports a narrow compiler/runtime subset:

- compiler extraction for supported one-file app sources;
- deterministic Plan/artifact/source-map emission;
- artifact validation and V8-gated artifact execution;
- bounded route dispatch and supported result descriptors;
- minimal route/query/request context in scoped runtime lanes;
- feature-gated core API facades where bridge support exists;
- metadata CLI commands such as routes, doctor, audit, and OpenAPI skeleton output.

## Limitations

- No public alpha docs launch.
- No production server claim.
- No Node/Bun/Deno/npm compatibility.
- No package-manager behavior.
- No full TypeScript type checking or arbitrary import graph resolution.
- No Framework v2 tutorial until Framework v2 lands.
- No benchmark or performance claim from smoke evidence.
- No provider readiness claim beyond the lanes that actually ran.

## Documentation Rules

Developer-facing docs should prefer current behavior, source docs, explicit limitations,
and runnable commands. Historical issue handles belong in project archives or issue
snapshots, not in user-facing examples or current product overview docs.

When a new ergonomic surface lands, update the source doc, public skeleton page or example
README, tests, and evidence report together.

## Source Docs

- `docs/compiler-supported-syntax.md`
- `docs/app-plan.md`
- `docs/execution-model.md`
- `docs/public/README.md`
- `stdlib/sloppy/README.md`
