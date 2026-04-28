# 0006: Memory Model

## Status

Accepted.

## Context

The runtime will process untrusted inputs, async lifetimes, native resources, and JS engine
objects. Plain null-terminated strings and ad hoc allocation rules are not enough.

## Decision

Sloppy will define custom string and buffer primitives such as `SlStr`, `SlBytes`, and
`SlBuf`. It will use arenas deliberately for permanent, startup/build, request, and scratch
lifetimes. Independently closed resources use resource tables with generation counters.

JavaScript never receives raw C pointers. Ownership transfer must be explicit.

## Consequences

The runtime can avoid hidden `strlen` costs, stale pointer hazards, and ambiguous ownership.
More upfront primitives are required before feature implementation.

## Alternatives Considered

- Use raw C strings internally: rejected because it creates length, encoding, and ownership
  ambiguity.
- Arena everything: rejected because async and independently closed resources need different
  lifetimes.

## Follow-up Tasks

- Implement `SlStr`, `SlBytes`, `SlBuf`, checked math, and allocator interfaces in Phase 1.
- Add tests for ownership, overflow, and invalid arguments.
- Define resource ID layout before JS-visible native handles exist.
- Add debug leak/resource tracking before async features expand.
