# Documentation Policy

## Purpose

Sloppy has two documentation audiences:

- implementers and agents working on Sloppy internals;
- developers using Sloppy to build applications.

Both must remain accurate. Internal docs explain how Sloppy is built. Public docs explain
how Sloppy is used. A change is not complete when code passes but the relevant docs now
mislead the next reader.

## Documentation Layers

### A. Architecture/spec docs

Architecture and spec docs are the source of truth for implementation:

- `docs/architecture.md`;
- `docs/execution-model.md`;
- `docs/concurrency.md`;
- `docs/platform-abstraction.md`;
- `docs/memory.md`;
- `docs/diagnostics.md`;
- `docs/app-plan.md`;
- `docs/compiler.md`;
- `docs/modularity.md`;
- `docs/data-providers.md`.

### B. User/developer-facing docs

User-facing docs live under `docs/public/*`.

Their purpose is to teach people how to use Sloppy. During the foundation phase, they are
planned docs and must clearly say when examples are not implemented yet.

### C. Module docs

Module docs live under `docs/modules/*`, with `src/<module>/README.md` added where useful.

Their purpose is to document actual implemented module behavior, invariants, APIs, tests,
diagnostics, and limitations.

### D. ADRs

ADRs live under `adr/*`.

Their purpose is to record long-lived architecture decisions, rejected alternatives, and
consequences.

### E. Execution plans

Execution plans live under `docs/exec-plans/*`.

Their purpose is to track complex implementation work, progress, and decisions that should
not live only in chat history.

## Rule: Code, Tests, Docs Move Together

A PR that changes behavior, APIs, module boundaries, diagnostics, architecture, CLI
behavior, test expectations, or public examples must update relevant docs.

Examples:

- changing `SlStr` semantics updates `docs/modules/core/README.md` and `docs/memory.md`;
- adding `SlArena` updates `docs/modules/memory/README.md` and `docs/memory.md`;
- changing diagnostic format updates `docs/diagnostics.md` and
  `docs/public/diagnostics.md`;
- adding a CLI command updates `docs/public/cli.md`;
- changing app API updates `docs/public/app-model.md` and
  `docs/developer-ergonomics.md`.
- changing public JS API or examples updates the relevant public/internal docs and must
  distinguish implemented behavior from future runtime/compiler behavior.
- changing compiler extraction or diagnostics docs must distinguish supported syntax from
  rejected syntax.
- Rust/JS changes must update docs and examples together when public behavior, examples,
  or fixture intent changes.
- standards changes must update `AGENTS.md` references and relevant skill docs.

## Rule: No Stale Documentation

If docs contradict implementation or tests, the PR must fix the contradiction or explicitly
document a migration/deprecation. Do not leave stale docs with TODO-only excuses.

## User-Facing Documentation Requirements

Every public feature must eventually include:

- overview;
- quick example;
- API shape;
- behavior;
- error/diagnostic behavior;
- limitations;
- links to deeper architecture docs if relevant.

## Module Documentation Requirements

Every implemented module should document:

- purpose;
- public/internal API;
- ownership/lifetime rules;
- invariants;
- non-goals;
- key data structures;
- error behavior;
- diagnostics;
- tests;
- examples if applicable;
- known limitations;
- source docs/ADRs.

## Documentation Checklist for PRs

- Does this PR change behavior?
- Does this PR add/change public API?
- Does this PR change diagnostics?
- Does this PR change test intent?
- Does this PR change architecture or boundaries?
- Does this PR require user-facing docs?
- Does this PR require module docs?
- Does this PR require ADR update?
- Does this PR touch JS/TS public API or examples and therefore need
  `docs/js-ts-standards.md` plus updated examples/docs?
- Does this PR touch compiler/tooling and therefore need `docs/rust-standards.md` plus
  updated compiler docs or fixtures?

## Docs Freshness Gate

Future/checkable rules:

- public docs must not mention implemented commands/APIs incorrectly;
- module docs must exist for implemented modules;
- docs links must not be broken where checkable;
- PR template requires a docs decision.

The lightweight checker starts with structure and required headings. Stronger semantic
freshness checks should be added only when they are reliable.

## Acceptance Criteria

For this pass:

- `docs/documentation-policy.md` exists;
- `docs/public/` skeleton exists;
- `docs/modules/` skeleton exists;
- `AGENTS.md`, `CONTRIBUTING.md`, and the PR template reference docs freshness;
- testing strategy references docs-as-intent.
