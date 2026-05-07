# Resource Module

## Purpose

The resource module gives native runtime resources stable handles without exposing raw
pointers to JavaScript or cross-boundary callers.

## Current Status

The implemented resource table uses fixed-capacity storage and generation-counted resource
IDs. Lookups validate table ownership, slot state, generation, expected type, and liveness
before returning native storage to runtime code.

## Invariants

- JavaScript must not receive native pointers.
- Stale handles fail deterministically.
- Cleanup must run at most once.
- Resource IDs are process-local runtime values, not durable storage identifiers.

## Deferred Work

Deferred work includes broader resource-table adoption, richer leak snapshots, and
additional provider/runtime resource types as their owning features mature.
