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
| Routes | Handler identity, route pattern, source location, and artifact link. |
| Methods | Supported HTTP methods and dispatch policy. |
| Body policy | Body size/media limits, parse policy, and binding expectations. |
| Query/route params | Names, coercion policy, validation hooks, and diagnostic spans. |
| Providers | Provider usage, service tokens, resource policy, and bridge requirements. |
| Capabilities | Required permissions and explanation paths for denial diagnostics. |
| Response shapes | Result metadata, status/header/body expectations, and OpenAPI hints. |
| Diagnostics/source maps | Source mapping and stable diagnostic references. |
| Examples/OpenAPI metadata | Documentation and schema seed data when explicitly supported. |

## Uses

- Startup validation.
- Doctor/audit commands.
- Plan-driven OpenAPI.
- Request binding validation.
- Capability enforcement explanation.
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

