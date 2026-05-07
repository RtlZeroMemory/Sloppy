# Documentation Policy

Documentation is part of Sloppy's correctness story. A change is not complete when code
passes but the next reader is sent to stale behavior, old task history, or unsupported
product claims.

## Audiences

- Current architecture and contributor docs explain how Sloppy is built and reviewed.
- Public/user-facing docs explain how Sloppy is used, but remain pre-alpha skeletons until
  the public alpha gate promotes them.
- Historical/archive docs preserve evidence and planning context without becoming current
  source of truth.

## Documentation Layers

### Current Source Docs

Current source docs describe architecture, invariants, implemented behavior, limitations,
and evidence boundaries:

- `docs/architecture.md`
- `docs/execution-model.md`
- `docs/concurrency.md`
- `docs/platform-abstraction.md`
- `docs/memory.md`
- `docs/diagnostics.md`
- `docs/security-permissions.md`
- `docs/app-plan.md`
- `docs/compiler.md`
- `docs/compiler-supported-syntax.md`
- `docs/modularity.md`
- `docs/data-providers.md`
- `docs/developer-ergonomics.md`
- `docs/testing-strategy.md`
- `docs/quality-gates.md`
- current contract docs under `docs/project/`

`docs/project/README.md` maps current contracts, issue snapshots, and archives.

### Public Docs

Public docs live under `docs/public/*`. During pre-alpha, they must be explicit skeletons
or current-subset pages. They must not expose internal issue choreography, imply public
alpha launch, or present broad framework/package/release behavior before it exists.

### Module Docs

Module docs live under `docs/modules/*`, with `src/<module>/README.md` added where useful.
They document implemented module behavior, invariants, APIs, tests, diagnostics, and
limitations.

### Contributor And Agent Docs

Contributor docs include `CONTRIBUTING.md`, `AGENTS.md`, `docs/agent-harness.md`,
`docs/review-playbook.md`, `docs/skills/*`, and prompt templates under
`docs/project/prompts/`. They may mention agent workflow when it helps implementation, but
they should focus on outcomes: source docs, bounded context, implementation contract,
evidence lanes, review criteria, and honest claims.

### ADRs And Execution Plans

ADRs record long-lived architecture decisions and rejected alternatives. Execution plans
track complex implementation work and move to `docs/exec-plans/completed/` when done.

### Archives

Archive docs under `docs/project/archive/**` and completed execution plans may retain old
task names, issue handles, and construction-era wording. Current docs must not point to
archive files as live status.

## Code, Tests, Docs Move Together

A PR that changes behavior, APIs, module boundaries, diagnostics, architecture, CLI
behavior, test expectations, or public examples must update relevant docs. Examples:

- changing string or arena semantics updates module docs and memory docs;
- changing diagnostic format updates diagnostics docs and goldens;
- adding a CLI command updates public CLI docs and any relevant source docs;
- changing public JS API or examples updates JS/TS standards, examples, and public/internal
  docs with implemented-vs-deferred wording;
- changing compiler extraction updates compiler docs, syntax docs, fixtures, and tests;
- standards changes update `AGENTS.md`, `CONTRIBUTING.md`, and relevant skills.

## Pre-Alpha Current-Code Policy

Sloppy is pre-alpha. When a current contract changes, source docs should describe the new
contract directly. Do not keep replaced/current parallel docs, numbered successor docs, or
transitional wording unless the task is specifically about a release-stability promise.
Deleting replaced code and rewriting stale docs is acceptable when tests and source docs
move with the change.

## No Stale Documentation

If docs contradict implementation or tests, rewrite the current source docs to match the
implemented contract. Do not leave stale docs with TODO-only excuses.

Current docs should describe current invariants. Historical docs should be archived or
clearly labeled. Issue indexes should not replace GitHub live issue state.

## No Unsupported Claims

Current/public docs must not claim:

- public alpha release or readiness;
- production readiness or operational hardening;
- performance results or benchmark conclusions;
- Node/Bun/Deno/npm behavior;
- package/release readiness;
- provider, V8, HTTP, TLS, or framework behavior beyond the evidence lane that proves it.

Negative limitation wording is encouraged when it prevents overclaiming.

## PR Documentation Checklist

- Does this PR change behavior, public API, diagnostics, test intent, architecture, or
  boundaries?
- Which source docs govern the change?
- Do user-facing docs, module docs, architecture docs, ADRs, skills, or examples need
  updates?
- Do tests verify documented intent rather than current output?
- Are optional lanes and limitations reported honestly?
- Are stale construction breadcrumbs moved to archive or rewritten as current invariants?

## Docs Freshness Gate

Static docs checks should be high-signal. They should distinguish current/public docs from
archive, planning, skills, tests, and issue snapshots. Secret-looking values should remain
broadly forbidden across docs, examples, tests, and goldens, except approved fake markers.

Add semantic checks only when they are reliable enough to reduce reviewer memory without
creating noisy false positives.
