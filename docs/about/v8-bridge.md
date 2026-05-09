# The V8 bridge

Sloppy's runtime kernel is C. JavaScript handlers run inside V8. The two
talk through a deliberately narrow C++ bridge that lives entirely under
`src/engine/v8/`.

That isolation is unusual — most JavaScript runtimes embed V8 directly
into the runtime code, with V8 types flowing through every layer. Sloppy
goes the other way.

## The boundary

A few hard rules:

- V8 types (`v8::Local`, `v8::Isolate`, `v8::Context`, …) never appear
  outside `src/engine/v8/`.
- The C ABI between the kernel and the bridge passes only Sloppy-owned
  types: status codes, diagnostics, byte slices, plan metadata.
- JavaScript never receives a raw native pointer. Every native resource
  the bridge exposes to JS is wrapped in a capability-checked handle.
- One owner thread enters each isolate. Wrong-thread access fails before
  V8 sees it.
- Native strings and result bytes are copied out of the bridge before
  returning to C — V8 handles never escape.
- Promise/microtask drains are bounded and owner-thread only. Pending or
  rejected promises produce a deterministic failure rather than fake
  success.

## Why isolate

**You can build, parse, validate, and inspect Sloppy without V8.** The
default CI builds and most CLI commands (`build`, `routes`,
`capabilities`, `audit`, `openapi`, `doctor`) compile against a noop
engine. V8 enters the picture only when handlers actually run.

**The runtime kernel stays portable.** Replacing V8 with another engine
would be invasive in most JavaScript runtimes — you'd find V8 types
everywhere. In Sloppy it's a bridge swap.

**Memory safety boundaries are visible.** Native code that crosses into
JavaScript or back is concentrated in one place. Reviewers can audit it
without pulling on threads through the rest of the kernel.

## What that costs

**Calls across the bridge are not free.** Every JS-visible value the
bridge returns is copied; every native value the JS handler hands back is
parsed and validated. For most HTTP backends that's invisible — the
bottleneck is downstream I/O, not bridge cost. If you're building a hot
loop that round-trips through V8 millions of times, you'll feel it.

**Some V8 features aren't surfaced.** Public Sloppy APIs don't expose
typed arrays as zero-copy `ArrayBuffer`s tied to native memory, for
example. Adding a feature to the bridge is intentional work, not free
exposure.

## What V8 is and isn't doing

V8 in Sloppy:

- Runs your handler bundle.
- Owns the JavaScript heap and microtask queue.
- Receives request context, returns result descriptors.

V8 isn't:

- Resolving modules. The compiler emits a single `app.js` bundle; V8
  evaluates it as a unit.
- Talking to the network or filesystem. Those happen on the C side; the
  bridge gives JS a small, capability-checked surface.
- Running outside the owner thread. Workers exist, but they each have
  their own isolate.

This is why a Sloppy app feels different from a Node app even though both
"run JavaScript on V8" — the surrounding shape is its own.
