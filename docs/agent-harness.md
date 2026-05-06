# Agent Harness Engineering

## Purpose

Sloppy uses Codex to move quickly without turning the repository into prompt archaeology.
Humans steer. Agents execute. The repository must contain the knowledge, plans, checks, and
feedback loops needed to work safely without relying on chat memory.

## Principles

- Humans steer, agents execute.
- Repository-local knowledge is the system of record.
- `AGENTS.md` is a map, not an encyclopedia.
- Execution plans are first-class artifacts.
- Mechanical checks beat reminders.
- Boundaries enable speed.
- Human taste becomes docs/checks/tools.
- Repeated review findings become durable repo knowledge.
- Technical debt needs continuous garbage collection.
- Prefer boring, legible code over clever code.

## Repository Knowledge Model

Anything not in repo-local docs, tools, plans, or ADRs effectively does not exist to agents.
Decisions made in chat must be promoted into docs or ADRs before they are expected to guide
future implementation. Docs should cross-link the source-of-truth pages they depend on.
When a doc guides implementation, it should be story-ready: concrete enough to identify
files, non-goals, tests, and acceptance criteria.

## AGENTS.md Policy

`AGENTS.md` stays short, stable, and map-like. Update it when navigation or the operating
contract changes. Do not use it as a dumping ground for every rule; detailed standards live
under `docs/`.

## Execution Plans

Active plans live in `docs/exec-plans/active`. Completed plans move to
`docs/exec-plans/completed`. Plans contain progress logs and decision logs, and agents
update them while working. Use them for complex multi-step tasks. Small changes may use a
lightweight in-prompt plan. Execution plans prevent chat-only decisions.

## Bounded-Context PR Model

Prefer coherent module or foundation slices. Avoid tiny one-line PRs forever and avoid
kitchen-sink PRs that mix unrelated work. Each PR should map to a roadmap epic/task and
should have a clear validation story.

## Review Loop

- Dev agent: implements the bounded slice.
- Spec reviewer: checks prompt, roadmap, docs, ADRs, scope, and acceptance criteria.
- Safety reviewer: checks C safety, resource ownership, platform boundaries, and tests.
- Build/tooling reviewer: checks scripts, CMake, CI, and generated artifact hygiene.
- Fixer agent: addresses actionable findings only.
- Final verifier: confirms original acceptance criteria and blocking feedback.

Blocking feedback prevents merge. Non-blocking feedback can become follow-up work when it
does not violate architecture, safety, quality gates, or required acceptance criteria.

## Mechanical Enforcement

Current and planned checks include the platform-boundary scanner, C standards scanner,
future V8 leakage checks, future allocator/resource checks, generated artifact hygiene, and
CI gates. Checks should print clear failures and point back to docs.

## Entropy and Garbage Collection

Agent code tends to replicate nearby patterns. Bad patterns must be corrected early before
they become the template. Repeated issues should create docs, checks, or tools. Track
deferred cleanup in `docs/tech-debt-tracker.md` and quality movement in
`docs/quality-score.md`.

## When to Add a Check

Add a check for a repeated review comment, boundary violation, unsafe C pattern, generated
artifact accidentally included, or docs drift that can be detected mechanically.

Repeated overengineering review comments should become C standards rules, examples, scanner
checks when mechanical, or tech-debt entries. Do not let "safe-looking" abstraction-heavy
agent output become the local style by repetition.

Repeated comment-quality findings should become examples in `docs/c-standards.md`.
Comments are part of agent legibility; they should preserve context, not create noise.

## When to Add a Doc

Add or update docs for a new architecture decision, repeated confusion, or an implementation
story that needs a source of truth.

## When to Add an ADR

Add an ADR for a long-lived architecture choice, when rejected alternatives matter, or when
the policy is cross-cutting.

## Required Agent Behaviors

Agents must read `AGENTS.md`, read relevant source docs, keep scope bounded, run checks,
report honestly, and update the tech debt tracker when deferring work.

On the primary Windows Codex machine, agents must also treat the compatible local V8 SDK as
available through the repository scripts. Runtime, app-host, compiler, bootstrap,
provider, and configuration implementation PRs should include the separate V8-enabled
`windows-relwithdebinfo` configure/build/test lane from `AGENTS.md` and
`docs/quality-gates.md`. If `tools/windows/resolve-v8-sdk.ps1` cannot resolve the SDK on
that machine, report the failed resolution as an environment blocker rather than silently
downgrading V8 evidence.

## Anti-patterns

- Giant `AGENTS.md` encyclopedia.
- Undocumented architectural decisions.
- Prompt-only rules not encoded in the repo.
- Kitchen-sink PRs.
- Implementation without tests/specs.
- Platform leakage.
- V8 leakage.
- Raw pointer exposure to JS.
- Clever C without comments/tests.
- "Works on my machine" tooling.
- Stale docs ignored by agents.

## GitHub Ceremony Workflow

Project ceremony flows from repository docs to EPICs, bounded tasks, PR prompts, review prompts, and merge verification.

1. Source docs and ADRs define the architecture.
2. `docs/project/epics/` converts roadmap outcomes into GitHub-ready EPICs.
3. `docs/project/tasks/` converts EPICs into bounded-context PR chunks.
4. `docs/project/prompts/` turns tasks into dev, reviewer, fixer, and final-verifier prompts.
5. GitHub scripts under `tools/github/` mirror labels, milestones, and issues without replacing the docs.
6. Human final review decides merge readiness and records deferred cleanup in the tech debt tracker.
