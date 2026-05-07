# Memory Module

## Purpose

The memory module provides the concrete primitives behind `docs/memory.md`: borrowed
views, arenas, bounded builders, interned metadata, resource handles, cleanup scopes, and
async ownership support.

## Current Status

Memory primitives are active across Plan parsing, diagnostics, HTTP, route matching,
provider metadata, app-host startup, and selected V8/provider bridge paths. Remaining
adoption work must preserve documented ownership and evidence lanes.

## Invariants

- Borrowed views do not imply ownership or NUL termination.
- Arena-owned outputs must document their owner.
- Delayed or cross-thread work must own or retain data.
- Resource IDs use generation checks.
- Cleanup callbacks run at most once.

## Related Docs

- `docs/memory.md`
- `docs/concurrency.md`
- `docs/testing-strategy.md`
