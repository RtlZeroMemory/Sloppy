# Memory Model Internals

## Where It Lives

- `include/sloppy/arena.h`
- `include/sloppy/bytes.h`
- `include/sloppy/string.h`
- `src/core/arena.c`
- `src/core/bytes.c`
- `src/core/string.c`

## Model

Core allocation uses Sloppy-owned primitives. Arena marks, borrowed string
views, byte slices, and checked arithmetic are part of the safety boundary.

## Invariants

- Mark rollback must be reliable in release builds.
- Size and offset arithmetic must be checked.
- Borrowed views must not outlive their owner.
