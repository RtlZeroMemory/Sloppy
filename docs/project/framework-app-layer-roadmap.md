# Framework/App Layer Roadmap

Status: planning source of truth for #432 and FRAMEWORK-01 tasks. This is not an
implementation claim.

## Why Now

Core MVP proved the scoped engine path: compiler artifacts, V8 execution, MVP HTTP
transport, capability-gated SQLite proof, conformance lanes, and package smoke. The next
wave can now focus on developer ergonomics without pretending the engine proof is the
framework.

## Framework Layer Meaning

The framework layer is Slop's app-authoring surface over the engine. It should turn engine
primitives into a predictable backend application model while preserving the runtime
boundaries documented in `docs/project/engine-framework-contract.md`.

## Target Developer Experience

| Area | Target |
| --- | --- |
| App routing | Clear route registration and route metadata that can feed Plan validation. |
| Context/request helpers | Stable request context with params, query, headers, body, config, services, logger, and diagnostics. |
| Binding | Route, query, header, and body binding with explicit coercion and failure policy. |
| Validation | Safe validation diagnostics and safe error responses; no raw internals in user responses. |
| Results model | Intentional response helpers for JSON/text/status/header behavior. |
| Configuration | App config model with environment and CLI binding policy. |
| Services/DI | Simple service registration/lifetime rules that map to app/request resource ownership. |
| Diagnostics | Stable app/framework diagnostic codes, source-map integration where available, redaction by default. |
| OpenAPI/doctor later | Driven by Strong Plan metadata, not hand-maintained framework side channels. |

## Issue Plan

- #432 owns the framework/app-layer wave.
- #435 locks the architecture and public surface contract.
- #436 covers configuration and environment/CLI binding.
- #437 covers request binding.
- #438 covers validation and safe error responses.
- #439 covers Results/response model completion.
- #440 covers examples hardening after the runtime path is honest.

## Non-Goals

- No Node compatibility.
- No Express compatibility.
- No public alpha docs yet.
- No ORM or migrations.
- No production HTTP claims.
- No benchmark or performance claims.
- No PostgreSQL/SQL Server JS bridge implementation in this wave.

