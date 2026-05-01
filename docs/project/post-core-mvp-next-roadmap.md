# Post-Core MVP Next Roadmap Proposal

Status: owner-approved planning wave created on 2026-05-01. The issue mapping source of
truth is `docs/project/post-core-next-wave-issue-map.md`.

## 1. Core MVP Status

| Area | Status | Notes |
| --- | --- | --- |
| Compiler -> Plan/artifacts | complete/proven | Supported subset emits deterministic artifacts and source maps. |
| V8 runtime execution | complete/proven for scoped path | V8-gated artifact execution, handlers, request context, direct async microtask settlement, and SQLite bridge paths exist. |
| HTTP backend semantics | complete/proven for MVP | Parser/body/response/dispatch/shutdown/admission semantics exist for bounded non-production paths. |
| Libuv localhost transport | complete/proven for scoped MVP path; boundary debt remains | One request per connection path is proven; boundary audit still tracks libuv API/dev-path leakage as mixed debt. |
| SQLite users API proof | complete/proven | Source-built users API runs over localhost TCP through V8 and capability-gated SQLite. |
| Provider execution/offload | partial | Native executor exists; current SQLite bridge is not yet routed through it. |
| Capability enforcement | complete/proven for integrated paths | SQLite bridge and provider executor enforce before provider work; filesystem/network remain skeletons. |
| Conformance/package evidence | complete/proven for current lanes | ENGINE-19 evidence lanes and package smoke exist; optional V8/package/live lanes must stay separate. |
| Source-input run | partial/proven | `sloppy run <source.js>` and `sloppy run` with `sloppy.json` compile through `sloppyc`, validate artifacts, and reuse `--artifacts`; TypeScript/module graphs/cache reuse remain deferred. |
| Framework configuration | partial/proven | FRAMEWORK-01.B adds appsettings overlays, environment/CLI/env binding, typed access, `bind`, SQLite provider convention binding, and redacted Plan metadata. Reload/secrets/custom providers and doctor/OpenAPI consumption remain deferred. |
| Public alpha | blocked | Needs canonical docs, executable examples, source-input decision, ergonomics, and package/platform story. |

## 2. Next Tracks

### Track A - Framework/App Layer

Created/reused issues:

- #432 FRAMEWORK-01 plus #435-#440 for framework architecture, configuration,
  request binding, validation, Results, and examples hardening;
- `docs/project/framework-api-shape.md` locks Minimal API first, function modules first,
  generated/inferred capabilities by default, layered Plan-visible config, Plan-first DSL
  extraction, explicit request binding, and explicit `Results.*` descriptors;
- #259/#302 and #316/#345-#349 for source-input run/dev loop;
- #318/#355-#359 for Plan-driven OpenAPI/doctor foundations.

### Track B - HTTP Post-MVP Transport

Created issues:

- #433 HTTP-25 plus #441-#446 for keep-alive, idle/max request limits, sequential
  request lifecycle reset, chunked request decoding, streaming response writer, and
  stress/conformance.

Later only: TLS/reverse proxy awareness, WebSockets, HTTP/2, and HTTP/3 research.

### Track C - Strong Plan Strategic Layer

Reused issues:

- #318 Strong Plan strategic layer;
- #355 typed Plan graph model;
- #356 route/body/provider/capability/response-shape metadata;
- #357 Plan validation and startup diagnostics;
- #358 Plan doctor/audit CLI and OpenAPI hooks;
- #359 future fast-path candidate registry, no implementation.

### Track D - Provider Expansion Later

Possible EPICs:

- route SQLite bridge work through provider executors before scalable provider claims;
- PostgreSQL JS bridge;
- SQL Server JS bridge;
- provider-specific cancellation;
- pooling strategy;
- live provider conformance.

## 3. Public Alpha Blockers

- Docs are cleaned and canonical.
- Examples are honest and executable.
- Conformance evidence is stable and split by default, V8-gated, transport, SQLite,
  package, live-provider, stress, and benchmark lanes.
- Source-input run decision is made.
- Framework ergonomics do not require normal app authors to hand-write capability blocks.
- Platform/package story is clear.
- No production HTTP, public performance, Node/npm, or package-manager claims are implied.

## 4. Recommended Next EPIC Creation Order

1. Source-of-truth/framework app-layer design docs/issues.
2. Source-input run/dev loop if needed.
3. Request binding/validation/config.
4. Plan-driven OpenAPI/doctor.
5. HTTP keep-alive/streaming.
6. Provider expansion later.

The concrete issue map is `docs/project/post-core-next-wave-issue-map.md`.
