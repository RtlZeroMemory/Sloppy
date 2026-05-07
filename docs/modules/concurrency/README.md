# Concurrency Module

## Purpose

The concurrency module defines Sloppy's owner-thread, async completion, cancellation,
deadline, worker/offload, and runtime boundary rules.

## Current Status

The runtime has loop/completion primitives, bounded completion ownership, terminal-state
guards, cancellation/deadline mapping, owner-thread V8 rules, blocking/offload policy, and
provider executor contracts. Some worker/offload paths are still foundations rather than
full production schedulers.

## Invariants

- Only the owning engine thread may enter a V8 isolate unless an engine bridge documents a
  different ownership rule.
- Cross-thread completions must own or retain their data.
- Failed completion admission does not transfer ownership.
- Late completions after terminal state are cleanup-only.
- Blocking work must run only in domains that allow it.

## Evidence

Concurrency tests should avoid timing-dependent sleeps where deterministic hooks or bounded
loops can prove the contract. Stress and torture lanes are separate from default evidence.
