# Memory model

Sloppy's C runtime treats ownership as part of every API. Every value
that crosses a function boundary has a documented owner and lifetime —
borrowed views, arena-owned data, scope-bound cleanups, or
generation-counted resource handles.

This page is the contract.

## Primitives

```text
include/sloppy/string.h           SlStr, SlBytes (borrowed views)
include/sloppy/owned_str.h        SlOwnedStr (inline-or-arena owned)
include/sloppy/arena.h            SlArena (bump allocator)
include/sloppy/builder.h          SlBuilder (bounded growable buffer)
include/sloppy/scope.h            SlScope (cleanup registry)
include/sloppy/resource.h         SlResource (generation-counted handles)
include/sloppy/checked_math.h     overflow-checked size/offset arithmetic
```

## Ownership shapes

| Shape                 | Storage             | Valid until                      | Typical use                                        |
| --------------------- | ------------------- | -------------------------------- | -------------------------------------------------- |
| `SlStr`, `SlBytes`    | the documented owner| owner-defined lifetime           | parser/render inputs that the caller must out-live |
| Arena copy            | caller-provided arena| arena reset/end                  | Plan metadata, diagnostics, per-request data       |
| `SlOwnedStr` (inline) | the struct itself   | struct lifetime                  | short owned strings; small-string optimization     |
| `SlOwnedStr` (arena)  | the backing arena   | arena lifetime                   | medium-size owned strings                          |
| Scope cleanup entry   | scope (request/app/resource) | scope ends, runs callback once | native resources, file/socket handles            |
| Resource ID           | resource table      | generation matches               | JS-visible native handles                          |
| Provider result copy  | request scope       | request scope ends               | rows, blobs, decoded values                        |
| V8 conversion copy    | the side that needs it | documented boundary             | JS strings/buffers crossing the bridge             |

The rule across all of these is: **the API tells you which one you're
getting**. A function that returns `SlStr` returns a borrowed view; a
function that takes `SlArena*` and returns `SlStr` returns an arena
copy with the lifetime of that arena.

## Borrowed views

`SlStr` and `SlBytes` are pointer + length pairs. They:

- are not NUL-terminated;
- do not own their storage;
- have no stable lifetime beyond the documented owner.

If you need to pass an `SlStr` to a NUL-terminated API (OS, libuv,
libpq, ODBC), use `sl_str_to_cstr_arena` or the equivalent C-string
boundary helper. That helper validates "no embedded NUL" before
allocating a terminated copy in an arena.

## Arenas

`SlArena` is a bump allocator. It's the workhorse:

- Per-app arena: born at app startup, dies at app shutdown.
- Per-request arena: born at request dispatch, dies at scope end.
- Per-operation scratch: short-lived, child of one of the above.

Arenas don't free individual allocations. They reset or end. Code that
returns data from an arena documents the arena it lives in.

`sl_arena_array_alloc` and friends use checked arithmetic
(`include/sloppy/checked_math.h`) for `count * sizeof(T)` and
`length + 1` patterns — overflow returns a status, never wraps.

## Builders

`SlBuilder` is a bounded growable buffer for serialization (JSON,
diagnostics, response bodies). It's append-only and self-validating:
appends fail with a status when the bound is exceeded.

Numeric appends and self-overlap checks live in `src/core/builder.c`.
The bound prevents accidental unbounded growth from untrusted inputs.

## Scopes and cleanup

`SlScope` is a cleanup registry. You register a callback + opaque
pointer; the scope guarantees the callback runs **exactly once**, in
reverse-registration order, when the scope ends.

```c
sl_scope_register_cleanup(scope, my_cleanup, user_data);
// ... later ...
sl_scope_dispose(scope);   // calls my_cleanup(user_data)
```

There are three relevant scopes:

- **App scope** — startup → shutdown. Owns singleton services and the
  app arena.
- **Request scope** — per request. Owns request arena, transient
  services, provider operation lifetimes for that request.
- **Resource scope** — per long-lived resource (background services,
  worker pools, provider connections). Disposed at app shutdown.

Late completions (e.g. driver returns after request cancellation)
register their cleanup against the *resource* scope, not the request
scope — the request scope is already dead. They never settle JS state.

## Resource table

Native resources surfaced to JS go through a resource table:

```c
SlResourceId id = sl_resource_register(table, kind, ptr, dispose_fn);
// JS sees `id`, not `ptr`.
sl_resource_with(table, id, kind, fn, ctx);  // generation-checked
```

The ID encodes a slot index plus a generation counter. Reusing a slot
bumps the generation; a stale ID looks up to a generation mismatch
and fails cleanly. JS never sees a pointer.

This is the mechanism that gives the "JS never receives raw native
pointers" invariant teeth.

## Failure modes

- **Allocation failure** — returns a status, never aborts. Builders and
  arenas surface the failure.
- **Overflow in size arithmetic** — checked helpers return a status.
- **Stale resource ID** — generation mismatch, returns a status.
- **Bad UTF-8 / embedded NUL** — string-boundary helpers reject up
  front.
- **Late completion after scope end** — cleanup-only path runs against
  the still-alive resource scope.

The recurring pattern: validate-or-fail at the boundary, never partial
success.

## V8 boundary

The bridge copies native data into V8-owned storage before returning to
JS, and copies JS data into Sloppy storage on the way in:

- Strings → V8 string allocator.
- Byte buffers → V8 `ArrayBuffer` with copied contents.
- Plan-derived metadata used by JS → static intrinsics that copy on
  read.

Zero-copy buffer ownership across the bridge is intentionally not
exposed. The trade is per-call cost vs. lifetime safety; we pick safety
until a measurement says otherwise.

## Common pitfalls

- Returning an `SlStr` whose backing memory will be freed before the
  caller reads it. Don't. Either copy into the caller's arena or
  document the owning scope clearly.
- Using `strlen`/`strcmp` on an `SlStr`. They aren't NUL-terminated.
  Use the `sl_str_*` helpers.
- Ignoring `sl_arena_array_alloc` failures. Arena allocation can fail;
  status returns mean "stop and propagate", not "warn".
- Registering a cleanup against the wrong scope. A per-request handle
  registered against the app scope leaks; a resource handle registered
  against a request scope dies too early.

## Tests

- **Arena unit tests** under `tests/unit/core/test_arena*.c` cover
  bounded allocation, reset, overflow.
- **Builder unit tests** verify append, self-overlap policy, bounds.
- **Scope tests** assert cleanup ordering and once-only invocation.
- **Resource table tests** cover generation mismatch and disposal.
- **Sanitizer lanes** (ASan/UBSan/LSan, opt-in) catch leaks and
  misuse.
- **Memory bounds fuzz seeds** under `tests/fuzz/` exercise checked
  arithmetic paths.

## See also

- [Async runtime](async-runtime.md) — owner threads, late completion
- [V8 bridge](v8-bridge.md) — what crosses the bridge
- [Provider runtime](provider-runtime.md) — provider value/result copying
