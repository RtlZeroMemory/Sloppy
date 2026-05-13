# C Standards

`docs/contributor/c-style.md` is the style overview. This document is the strict operational standard
for C in Sloppy.

## Goals

- Safe C.
- Readable C.
- Explicit ownership.
- Bounded behavior.
- Sanitizer/static-analysis friendly code.
- Cross-platform core.
- Maintainer-legible code.

## Language Baseline

Use C17 as the baseline. C23 is allowed only when isolated, guarded, documented, and
supported by active toolchains. `clang-cl` is first on Windows; clang/gcc follow later. Do
not depend on compiler-specific behavior in core unless it is abstracted.

## Representation and Layout Optimization

Sloppy is still pre-alpha, so we can change native contracts when a better layout removes
real cost. Use that freedom carefully. Layout work is useful when a type appears many times
in memory, moves through a hot path, or sits in a cache-sensitive batch. Examples include
HTTP/2 event arrays, route tables, request contexts, log fields, and engine result values.

The goal is to make those structures smaller or cheaper to scan without making them
surprising to read, debug, or port. Prefer ordinary C17 techniques:

- field reordering to remove padding;
- tagged unions for mutually exclusive payloads;
- integer flag masks or bit-fields for dense private boolean state, especially when callers
  do not need addresses for individual flags;
- `_Alignof(T)` when allocating typed storage;
- `_Static_assert` layout contracts in focused tests for hot structs;
- C++ standard-library representation helpers, such as `std::variant`, inside
  `src/engine/v8/*` when they avoid constructing inactive payloads.

Tagged unions need an unambiguous active-member rule. If an existing field already tells the
reader which union member is live, document that at the type definition. If it does not,
add a separate payload tag. For example, a semantic result kind such as “text” might not be
enough when text can be represented either as a raw `SlStr` or as an HTTP response body; in
that case the payload tag is part of the contract, not an implementation detail.

Every layout change must explain the practical reason for it. In the commit, PR, or nearby
comment, name the hot path or repeated storage and state the expected size/cache effect.
Preserve ownership and lifetime comments, because smaller structs do not excuse unclear
ownership. Cover behavior with normal tests. If the byte size is part of the value, add a
layout test guarded to the platform model being claimed, usually 64-bit.

Do not add clever representation changes just because they are possible. A good layout
change should still be easy for a contributor to inspect in a debugger and reason about
from the header.

## Branching on Hot Enums and Tags

Prefer `switch` when code dispatches on a stable enum, protocol tag, operation kind, or
small integer discriminator with three or more cases. This is not a blanket style rule:
it applies where the selector is already a single value and each branch represents a
distinct operation. Examples include frame types, event kinds, logging field kinds, result
payload tags, and async operation kinds.

Use `if` statements for predicates that are genuinely different tests, such as pointer
validation, status checks, string comparisons, feature gates, and V8 shape/type probes.
Forcing those into a switch usually makes the code less direct and does not give the
compiler a better dispatch shape.

A good switch should make the hot path easier to scan:

- group cases that share the same behavior;
- keep precondition checks before the switch when they apply to every case;
- include a `default` only when unknown values are valid to ignore or should map to a
  clear error;
- avoid fallthrough unless it is intentional, documented, and covered by tests.

Do not rewrite small two-branch checks just to use `switch`. The point is to clarify and
optimize real dispatch, not to satisfy a cosmetic pattern.

Forbidden for normal runtime hot paths:

- packed structs, `#pragma pack`, and compiler-specific packed attributes;
- NaN boxing;
- low-bit tagged pointers;
- pointer/integer punning that depends on allocator alignment;
- public ABI changes whose layout is not documented and tested.

Packed layouts are only acceptable at an external wire, disk, or FFI boundary, and even
there the code should parse or copy into a normal native type before field access. Directly
reading unaligned packed fields is too easy to get wrong across compilers and CPUs.

NaN packing and low-bit tagged pointers are also off by default. They can be fast in a
specialized value runtime, but they make debugging harder, interact poorly with sanitizers,
and depend on assumptions about floating-point payloads, pointer width, and allocator
alignment. If future measurements show one of those techniques is worth considering, start
with a design document that includes benchmark evidence, portability limits, sanitizer and
debugging implications, and a simpler fallback path. Do not land those techniques
opportunistically.

## Naming

- Functions use the `sl_` prefix.
- Types use `SlTypeName`.
- Constants and macros use `SL_CONSTANT_NAME`.
- Avoid POSIX-reserved `*_t` public project types unless already standard/intentional.
- Use module prefixes where they imvalidate clarity.

## File/Module Rules

Every non-trivial C file should start with a file header comment covering purpose,
invariants, ownership model, platform assumptions, and test coverage pointer if relevant.

## Header Rules

Headers must be self-contained, use include guards or `#pragma once` consistently, avoid
heavy/platform headers, document ownership/lifetime, and avoid exposing internal structs
unless intended.

Public headers under `include/sloppy/` must not include V8, libuv, OS, or provider
dependency headers. Internal headers should live under the owning `src/<module>/` tree and
must not be used to bypass the documented core, platform, provider, engine, or CLI
ownership boundaries.

## Ownership Rules

- Borrowed pointer: valid only for the documented call or lifetime.
- Owned pointer: must be released by the documented owner.
- Arena-owned: released when the arena resets/destroys.
- Resource-table-owned: referenced by ID and generation, closed through the table.
- Engine-owned: owned by the engine adapter.
- Caller-owned: caller retains release responsibility.
- Callee-owned: callee retains release responsibility.
- Transfer semantics: ownership moves and must be documented.

Public and internal headers need ownership comments for ambiguous cases.

## Container Rules

Use the standard C container primitives for repeated collection mechanics in the C kernel:
fixed vectors for bounded append-only lists, ring queues for FIFO completion/event queues,
arena array allocation/copy for typed slices, and arena hash indexes for bounded lookup
tables. Do not reimplement checked array sizing, ring-buffer wraparound, or bucket-chain
bookkeeping in feature modules unless a documented invariant requires a specialized shape.

Containers must follow the same ownership rules as the memory they wrap. They do not own an
arena, do not free arena storage, and do not hide reset/generation invalidation. Typed
modules still own cleanup, resource generation, and public API semantics.

## Comments and Rationale

### Principle

Comments should explain what, why, and how when the code has context that is not obvious
from the local syntax.

Do not comment obvious syntax. Prefer clear names and simple code over comments that
compensate for confusing code.

### What Good Comments Explain

Good comments explain:

- ownership and lifetime;
- invariants;
- error-handling rules;
- bounds/safety reasoning;
- platform assumptions;
- engine/thread assumptions;
- allocator/arena assumptions;
- resource table assumptions;
- why a simpler-looking approach is unsafe;
- why a non-obvious tradeoff was chosen;
- public API contract;
- file/module purpose;
- TODOs only when tied to a tracked task or clear follow-up.

### Required Comment Locations

Require useful comments in these places.

File-level comments for non-trivial C files. They must include:

- what the module does;
- why it exists;
- important invariants;
- ownership/lifetime model;
- platform assumptions;
- related tests/docs.

Example:

```c
/*
 * src/core/arena.c
 *
 * Implements Sloppy's scoped arena allocator.
 *
 * Arenas are used for bounded lifetimes such as startup, scratch work, and
 * request-local temporary state. They are not used for independently closable
 * resources such as sockets, DB handles, or engine handles.
 *
 * Safety invariants:
 * - all allocation sizes are checked for overflow before alignment;
 * - returned memory is aligned to the requested power-of-two alignment;
 * - marks may only reset within the same arena generation;
 * - debug builds may poison reset memory to catch stale use.
 */
```

Public/internal header comments. They must document:

- ownership;
- lifetime;
- nullability;
- thread assumptions;
- whether output pointers are required;
- who frees or resets memory.

Example:

```c
// Copies `src` into `arena` and writes an arena-owned string view to `out`.
// `out->ptr` remains valid until the arena is reset or destroyed.
// `src` may be non-null-terminated.
SlStatus sl_str_copy(SlArena *arena, SlStr src, SlStr *out);
```

Non-obvious safety checks:

```c
// Align after checking `size + padding` for overflow. Without the checked add,
// a large size could wrap and produce a tiny allocation.
```

Ownership/lifetime boundaries:

```c
// The returned buffer is owned by the resource table entry. JS receives only
// the resource id; it must never observe this pointer directly.
```

Platform/engine boundaries:

```c
// This file is the only place allowed to include <windows.h>. Core runtime
// code must call the sl_os_* abstraction instead.
```

Threading/async assumptions:

```c
// Completion callbacks are posted back to the owning JS event-loop thread.
// Worker threads must not enter the V8 isolate directly.
```

Intentional limitations:

```c
// This parser is intentionally non-recursive. Route patterns are small, but
// avoiding recursion keeps malformed input from consuming stack.
```

### TODO/FIXME Comments

TODOs must include:

- reason;
- follow-up issue/task if known;
- whether it blocks correctness or is a deferred enhancement.

Good:

```c
// TODO(<tracked-issue>): replace this synchronous completion path when the native
// completion queue exists. This test double must not be used
// by HTTP request handling.
```

Bad:

```c
// TODO fix later
```

### What Not To Comment

Do not add comments that merely restate code:

- "set x to zero";
- "loop through items";
- "return ok";
- "allocate memory" when the function name already says that;
- comments generated for every line;
- vague comments like "handle error" without explaining why/how.

### Comment Density

Comments should be dense where invariants are dense. Simple helpers may have no internal
comments if names and tests make behavior obvious. Do not force comments on every function.
Do require comments on public APIs and non-obvious internals.

### Comment Accuracy

Stale comments are bugs. If behavior changes, comments must change in the same PR. Do not
leave stale rationale.

### Rationale Comments vs Cleanup

If code needs many comments to explain accidental complexity, simplify the code instead.
Comments should explain necessary complexity, not excuse unnecessary complexity.

### Examples

Bad:

```c
// Check if ptr is null.
if (ptr == NULL) {
    return SL_ERR_INVALID_ARG;
}
```

Better if context matters:

```c
// Public APIs reject NULL output pointers instead of asserting because callers
// may be outside Sloppy's trusted internal boundary.
if (out == NULL) {
    return SL_STATUS_INVALID_ARG;
}
```

Bad:

```c
// Copy bytes.
memcpy(dst, src, len);
```

Good:

```c
// `dst` was allocated with exactly `src.len + 1` bytes so the boundary adapter
// can append a NUL terminator for the C API without changing the SlStr length.
memcpy(dst, src.ptr, src.len);
```

## Simplicity and Anti-Overengineering

Simplicity is a safety feature. Sloppy prefers direct, explicit C over premature
abstraction.

### What We Want

- clear data structures;
- clear ownership;
- direct control flow;
- explicit error handling;
- small helper functions with obvious value;
- local reasoning;
- abstractions that enforce real boundaries;
- abstractions that remove validaten duplication;
- abstractions backed by tests;
- boring code that is easy to review.

### What We Avoid

- premature generic containers;
- unnecessary vtables;
- unnecessary factories/builders/providers;
- callback-heavy designs when direct calls work;
- global registries before real plugin/provider need;
- macro systems for simple logic;
- excessive type wrapping;
- "frameworks inside the runtime";
- deeply nested control flow;
- clever branchless code without measured need;
- hiding simple checks behind abstract helpers;
- speculative extension points;
- configurable everything;
- writing a reusable subsystem before the second real use case exists.

### Abstraction Rule

Do not introduce an abstraction unless at least one is true:

1. It enforces an architectural boundary documented in `docs/` or ADRs.
2. It eliminates real duplication that already exists.
3. It encodes a safety invariant that tests can verify.
4. It isolates platform/engine/runtime-specific behavior.
5. It simplifies the caller more than it complicates the callee.

If none of these is true, write the simple code.

### Two-Use Rule

Prefer waiting for two real use cases before generalizing, unless the abstraction is a known
architectural boundary such as platform, engine, allocator, resource table, or provider
interface.

### Small API Rule

APIs should start narrow. Adding public/internal API is easier than removing it later. Do
not expose options, flags, callbacks, or hooks "just in case."

### Local Reasoning Rule

Each function must make ownership, failure modes, and side effects clear without cross-file
indirection.

### Explicit Beats Generic

Use specific structs/functions first.

Good:

```c
SlStatus sl_str_copy(SlArena *arena, SlStr src, SlStr *out);
```

Suspicious:

```c
SlStatus sl_object_clone_with_policy(
    SlCloneContext *ctx,
    SlAnyRef input,
    SlClonePolicy *policy,
    SlAnyRef *out
);
```

### Macros

Macros are allowed for:

- source locations;
- assertions;
- compile-time constants;
- tiny type-safe-ish helpers where C has no better option.

Macros are not allowed for:

- hiding control flow;
- generating large APIs;
- replacing ordinary functions;
- clever DSLs;
- generic programming unless explicitly justified.

### Function Length and Complexity

Do not enforce arbitrary tiny functions, but watch for:

- too many nested levels;
- too many responsibilities;
- too many parameters;
- repeated cleanup logic;
- hidden global effects.

Prefer splitting when it imvalidates clarity, not to satisfy artificial line counts.

### Error Paths

Do not abstract error handling so much that the cleanup path becomes invisible. `goto
cleanup` is acceptable and preferred for multi-resource C functions.

### Performance and Simplicity

Do not add "performance abstractions" without benchmark or clear hot-path reason. Simple
code with obvious bounds checks is preferred until profiling says otherwise.

### Comments

Comment invariants and non-obvious safety choices. Do not comment obvious line-by-line
behavior. Do not replace clear code with vague comments.

### Examples

Bad overengineered:

```c
typedef struct SlStringFactory {
    SlAllocatorVTable *allocator;
    SlStringPolicy policy;
} SlStringFactory;

SlStatus sl_string_factory_create(
    SlStringFactory *factory,
    SlStringInput input,
    SlStringOutput *out
);
```

Good:

```c
SlStatus sl_str_copy(SlArena *arena, SlStr src, SlStr *out);
```

Bad speculative registry:

```c
sl_registry_register_handler(runtime->global_registry, "checked_add", handler);
```

Good direct helper:

```c
bool sl_checked_add_size(size_t a, size_t b, size_t *out);
```

### Review Questions

- Could this be simpler?
- Is this abstraction required by a documented boundary?
- Is there more than one real use case?
- Does this make callers simpler?
- Does this hide ownership or error behavior?
- Would a direct function be clearer?
- Are we building a framework for future imaginary needs?

## Error Handling

Use `SlStatus`/`SlStatusCode` style. Diagnostics are separate from status codes. Meaningful
operations must not fail silently and must not use null-only failure. Cleanup paths must be
tested.

## Cleanup Pattern

Use `goto cleanup` for functions with multiple resources.

```c
SlStatus sl_example_make(SlThing **out) {
    SlStatus status = sl_status_ok();
    SlThing *thing = NULL;

    thing = sl_alloc_thing();
    if (thing == NULL) {
        status = sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
        goto cleanup;
    }

    *out = thing;
    thing = NULL;

cleanup:
    sl_free_thing(thing);
    return status;
}
```

## Strings and Bytes

`SlStr` is pointer plus length and is not necessarily null-terminated. `SlBytes` is the
byte-slice equivalent. Do not rely on internal `strlen`; use boundary adapters only for C,
OS, and library APIs. C-string boundaries must validate no embedded NUL before producing
terminated storage. Add explicit UTF-8 validation where needed later.

## Memory, String, Buffer, And Builder Operations

Use Sloppy's existing high-level primitives for all string, byte, buffer, arena, and
builder operations.

Required:

- use `SlStr` and `SlBytes` views;
- use `SlOwnedStr` and `SlOwnedBytes` where ownership markers exist;
- use `SlArena` and arena copy helpers;
- use `SlStringBuilder` and `SlByteBuilder` for bounded construction;
- use existing checked size, capacity, and growth helpers;
- use existing string/byte equality, hash, and copy helpers;
- use scalar byte-search helpers instead of open-coded delimiter scans where the shared
  primitive fits;
- use existing diagnostics rendering and redaction helpers;
- preserve explicit pointer-plus-length semantics;
- preserve embedded-NUL and binary correctness.

Optional SIMD backends belong in dedicated backend files and must call through canonical
length-based primitive APIs. The scalar implementation remains the reference behavior and
fallback. Intrinsics must not introduce OS API dependencies, hidden global dispatch state,
or a second public compatibility path.

Forbidden:

- do not hand-roll ad hoc string append loops;
- do not manually track string offsets when `SlStringBuilder` or `SlByteBuilder` fits;
- do not use `sprintf`, `snprintf`, `strcat`, `strcpy`, or `strlen`-based construction for
  internal buffers unless the code is a validated boundary helper;
- do not assume NUL termination for `SlStr` or `SlBytes`;
- do not allocate hidden heap buffers for convenience;
- do not create new local mini-builders;
- do not duplicate existing memory/string helpers.

If a required primitive is missing:

- stop and document the missing primitive;
- add the smallest reusable helper in the existing memory/string module only when clearly
  necessary;
- add tests for that helper;
- do not solve it with one-off local buffer manipulation.

Apvalidated boundary helpers must be narrow, documented, and easy for the standards scanner to
recognize. Raw NUL-append string copies are not C-string validation; OS/env/config/app-host
boundaries should use the canonical C-string copy helper. New exceptions should be rarer
than new reusable primitives.

### C Stdlib Memory/String Policy

The C standards scanner is the fast first line of defense. These categories are enforced
for implementation code under `include/` and `src/`, with always-unsafe string APIs also
banned in tests and benchmarks.

Keep scanner rules broad and reviewable. A good rule catches a class of unsafe code across
normal implementation paths, such as unsafe C strings, raw allocation outside allocator
modules, plain Win32 DLL loading, or POSIX directory opens that can follow symlinks. Do not
add one-off checks for a single line of code when a focused unit test or clearer helper
would be more honest.

Always banned:

- `gets`;
- `strcpy`, `strncpy`, `strcat`;
- `sprintf`, `vsprintf`;
- `strdup`.

Primitive-only or explicit-boundary APIs:

- `strlen` is allowed in `src/core/string.c` for the canonical C-string adapter. Other
  C-string boundary code must use `SlStr` adapters instead of local length scans.
- `memcpy`, `memmove`, `memcmp`, and `memset` are errors outside canonical string/byte
  primitives recognized by the scanner. Add a reusable Sloppy primitive instead of an
  inline exception.
- `snprintf` is not allowed in Sloppy-owned implementation code. Floating-point text uses
  the Ryu-backed `sl_string_format_f32` / `sl_string_format_f64` helpers, and integer text
  uses the canonical `sl_string_format_*` helpers.
- `memset` must not be used for secret wiping. Structure zero-initialization at platform
  or dependency boundaries must use a narrow local helper or canonical primitive.

Raw allocation APIs:

- `malloc`, `free`, `realloc`, and `calloc` are errors outside allocator modules. New code
  should use arenas, resource tables, or a scoped allocator primitive instead.

Static-analysis suppressions:

- `NOLINT`, `NOLINTNEXTLINE`, `NOLINTBEGIN`, and `NOLINTEND` require
  `sloppy-analysis-suppress: #issue reason; remove when condition`.
- Prefer fixing the analyzer finding over suppressing it. Suppressions are narrow,
  reviewed debt, not a baseline dumping ground.

Void casts and ignored return values:

- Do not use `(void)` casts to silence ignored function return values.
- Plain `(void)parameter` suppression is allowed for fixed-signature callback parameters or
  platform branches that intentionally do not use an argument.
- If the return value matters for truncation, formatting, or write failure, check it and
  propagate or report the error. Output-file `fclose` can report final buffered write
  failures and must be checked when command success depends on durable output. If failure is
  intentionally non-actionable in a test, cleanup, or best-effort diagnostic path, call the
  function directly without a void cast.
- New implementation code should not use `snprintf` at all; use Sloppy string builders or
  canonical formatting helpers instead.

## Integer and Size Safety

Use checked add/mul for allocation sizes. Prefer checked array-size helpers for
`count * sizeof(T)` allocations and checked three-term additions for repeated-size totals.
Avoid unchecked casts from signed to unsigned and truncating conversions without helpers.
Avoid magic constants. Use `size_t` for sizes and `uint*_t` when exact width matters.

When a dependency API accepts a narrower size type, check the value against that target
type before casting. This applies to ODBC `SQLLEN`, libpq parameter lengths, V8 string
lengths, Win32 `DWORD`, POSIX `int`, and any other API where Sloppy stores the value as a
`size_t` or wider integer first. The guard should live next to the cast so reviewers do not
have to reconstruct the range proof from distant control flow.

## Memory Allocation

Raw `malloc`/`free` are forbidden outside allocator modules once those modules exist. Use
arena allocation for scoped lifetimes and pools/resource tables for independently closable
resources. Do not abuse arenas for independently closable resources. Avoid hot-path
allocation unless intentional.

## Platform Isolation

No `windows.h`, `unistd.h`, `pthread.h`, `epoll`, `kqueue`, or similar OS headers outside
`src/platform/*`. No scattered `#ifdef _WIN32` in core logic. Platform behavior sits behind
Sloppy-owned APIs.

Platform security-sensitive APIs need the same boundary discipline:

- Windows dynamic-library loading must go through Sloppy's platform helper and use
  `LoadLibraryExW` with explicit search flags. Do not call `LoadLibraryW` directly.
- POSIX platform code that opens directory handles for traversal or recursive delete must
  include `O_NOFOLLOW`, or document and test a narrow exception.
- Windows recursive traversal must check `FILE_ATTRIBUTE_REPARSE_POINT` before descending
  into a directory-like entry.

## V8 Isolation

No `v8::*` outside `src/engine/v8/*`. The C runtime sees only an engine-neutral C ABI. Core
code must not include V8 headers.

## JS/Native Safety

JavaScript never holds raw C pointers. Use resource IDs plus generation counters, and
validate kind, generation, and lifetime on every operation.

## Concurrency

No shared mutable global state without documented synchronization. Document thread
ownership. Atomics require comments explaining memory ordering. Do not access an engine
across threads unless the engine adapter owns that rule.

One V8 isolate has one owner JS thread. Native worker-pool threads must not enter that
isolate or call JS handlers. Cross-thread engine communication uses queues/completion
messages posted back to the owning JS event loop. Data crossing threads must have documented
ownership and lifetime. See `docs/internals/async-runtime.md`.

## Assertions

Use assertions for internal invariants. Do not use assertions as user input validation.
Release behavior must remain safe. `SL_ENABLE_ASSERTS` is Sloppy's project assertion toggle;
when it is enabled, assertions must remain active even if a toolchain or build mode defines
`NDEBUG`.

## Forbidden Patterns

- Raw `malloc`/`free` outside allocator modules.
- `strcpy`, `strcat`, `sprintf`, `vsprintf`, `gets`.
- Packed structs or `#pragma pack` in runtime representation.
- NaN boxing or low-bit tagged pointers without an accepted design document.
- `(void)` casts around ignored function calls.
- Unchecked `memcpy`, `memmove`, `snprintf`.
- `strlen` on untrusted/non-boundary strings.
- Variable length arrays.
- Unbounded recursion in parsers.
- Platform headers in core.
- V8 headers outside the bridge.
- Exposing raw native pointers to JS.
- Magic numbers.
- Hidden global mutable state.
- Swallowing errors.
- TODO-only implementation with fake success.

## Required Tests by Module Type

- Parser: fuzz/golden tests.
- Allocator: boundary, overflow, and alignment tests.
- Resource table: stale ID, wrong kind, and leak tests.
- Platform code: platform-specific tests.
- Diagnostics: golden tests.
- Compiler artifacts: golden tests.

## Review Checklist

- Ownership and lifetime are documented.
- The solution is direct and avoids speculative abstraction.
- Any abstraction is required by a documented boundary, validaten duplication, or tested safety
  invariant.
- Bounds and overflow behavior are tested.
- Cleanup releases every acquired resource.
- Errors are surfaced through status/diagnostics.
- Core code has no platform or V8 leakage.
- JS-facing native state uses resource IDs, not raw pointers.
- Tests cover success and failure paths.
- Code is boring enough for the next maintainer to maintain.

## Acceptance Criteria for Phase 1 C Primitives

- `SlStatus`.
- `SlSourceLoc`.
- `SlStr`/`SlBytes`.
- Checked math.
- Assertions.
- Unit tests.
- Formatting/lint passing.
- No forbidden platform headers.
- No raw allocator misuse.
