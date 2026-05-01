# Strong Plan Strategic Layer Plan

Status: planning source of truth. Reuse #318/#355-#359; do not create duplicate PLAN
issues.

## Strategic Role

Plan is Slop's strategic differentiator: the engine should understand the app shape before
serving it. Strong Plan work turns generated metadata into a typed graph that can validate,
diagnose, audit, document, and eventually optimize supported apps.

## Typed Plan Graph

| Graph node | Purpose |
| --- | --- |
| App entry | Entrypoint, environment, source-input/cache relationship, and artifact roots. |
| Modules | Function-module graph, route/provider contributions, and source locations. |
| Routes | Handler identity, route pattern, source location, and artifact link. |
| Methods | Supported HTTP methods and dispatch policy. |
| Body policy | Body size/media limits, parse policy, and binding expectations. |
| Query/route/header/body params | Names, coercion policy, validation hooks, and diagnostic spans. |
| Providers | Provider usage, service tokens, resource policy, and bridge requirements. |
| Capabilities | Inferred/generated permissions, source locations, and explanation paths for denial diagnostics. |
| Config keys | Provider-bound and Slop-owned runtime config needed by the app. |
| Response shapes | Result metadata, status/header/body expectations, and OpenAPI hints. |
| Diagnostics/source maps | Source mapping and stable diagnostic references. |
| Examples/OpenAPI metadata | Documentation and schema seed data when explicitly supported. |

## DSL Recognition Boundary

Strong Plan work recognizes the Slop app DSL, not arbitrary JavaScript static analysis.
Target recognition includes `Sloppy.create()`, `app.use(...)`, `app.useModule(...)`,
`app.group(...)`, `app.get/post/put/patch/delete(...)`, `ctx.body.json(schema)`,
`db.query/queryOne/exec/transaction`, `Results.*`, and `schema.*`.

If route, provider, capability, body, config, or response behavior is not Plan-visible, it
is not part of the optimized/safe Slop framework path. Dynamic patterns must fail with a
helpful diagnostic or require explicit metadata; no silent unsound inference.

## Uses

- Startup validation.
- Doctor/audit commands.
- Plan-driven OpenAPI.
- Request binding validation.
- Capability enforcement explanation.
- Config and provider policy explanation.
- Future native JSON fast-path candidate identification.
- Future multi-isolate or route-partitioning research.

## Issue Plan

- #318 owns the Strong Plan strategic layer EPIC.
- #355 covers typed Plan graph model.
- #356 covers route/body/provider/capability/response metadata.
- #357 covers Plan validation and startup diagnostics.
- #358 covers doctor/audit/OpenAPI hooks.
- #359 covers future fast-path candidate registry without implementation.

## Non-Goals

- Do not implement multi-isolate execution now.
- Do not implement native JSON fast paths now.
- Do not make benchmark claims.
- Do not make OpenAPI/public-alpha claims before framework metadata and examples are honest.
- Do not attempt full arbitrary JS/TS analysis.
## ENGINE-14 Plan Input

ENGINE-14 feeds Strong Plan work with compiler-emitted module route attribution,
multi-source source-map metadata, and provider/capability entries from function modules.
The native typed module graph remains future Strong Plan work; this PR keeps runtime
startup on the existing Plan route/provider/capability sections.

## FRAMEWORK-01.B Plan Input

FRAMEWORK-01.B feeds Strong Plan with compiler-emitted configuration metadata:
environment name, effective config keys, source layers, redacted values, and provider
binding prefixes. The native `SlPlan` struct does not yet model this as a typed graph.
#355-#359 should promote this metadata into the Strong Plan graph when provider/config
diagnostics, doctor, audit, and OpenAPI consume it.
