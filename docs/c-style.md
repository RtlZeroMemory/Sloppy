# C Style

## Purpose

This document defines the C rules for Sloppy runtime code. It is meant to be enforceable in
review and quality gates.

For strict operational rules, scanner expectations, and phase acceptance criteria, see
`docs/c-standards.md`. This file is the style overview; `docs/c-standards.md` is the
enforcement-oriented standard.

Simplicity is part of the style. Sloppy prefers boring, direct C with visible ownership and
error paths over generic systems, speculative extension points, macro DSLs, and
abstraction-heavy ceremony. See the Simplicity and Anti-Overengineering section in
`docs/c-standards.md`.

Comments should explain rationale, ownership, lifetime, invariants, and boundary
assumptions, not obvious syntax. See `docs/c-standards.md#comments-and-rationale`.

## Scope

This document covers:

- C17 baseline;
- `clang-cl` specifics;
- warning policy;
- naming conventions;
- cleanup style;
- ownership documentation;
- forbidden APIs and patterns;
- platform isolation;
- static analysis;
- file/module comments;
- acceptance criteria for C code entering the repo.

## Non-Goals

This document does not define the JavaScript, TypeScript, or Rust style guides.

## Current Phase

Core runtime, HTTP, provider, diagnostics, memory, Plan, and platform modules now contain
real foundation code. These rules are review and CI requirements for every C runtime
change.

## Future Phase

As modules grow, the rules may become stricter, but feature code should not weaken them.

## Language Baseline

Sloppy runtime C uses C17 as the portability baseline. C23 features may be used only when:

- isolated;
- guarded;
- documented;
- supported by the active toolchains;
- justified by a real benefit.

## clang-cl And Windows

Windows with `clang-cl` and `lld-link` is the first-class developer path. Code must compile
cleanly with MSVC-style command-line behavior and Clang diagnostics.

Do not assume GCC/Clang Unix defaults in core code. Do not assume Windows-only APIs either.

## Warning Policy

Clang/GCC-like builds should use:

- `-Wall`;
- `-Wextra`;
- `-Wpedantic`;
- `-Wconversion`;
- `-Wsign-conversion`;
- `-Wshadow`;
- `-Wstrict-prototypes`;
- `-Wmissing-prototypes`.

`clang-cl` builds use `/W4`, `/WX` in CI, and practical Clang warning flags.

## Naming Conventions

- public symbols: `sl_` prefix;
- public types: `SlTypeName`;
- constants/macros: `SL_CONSTANT_NAME`;
- functions: `sl_function_name`;
- private helpers: `static` and module-prefixed when useful;
- public headers: under `include/sloppy/`;
- internal files: under `src/<module>/`.

## File And Module Comments

Every module should start with a file-level comment covering:

- purpose;
- invariants;
- ownership model;
- thread/lifetime assumptions;
- platform restrictions if any.

Every public header must document public ownership and lifetime rules.

## Ownership Documentation

Public APIs must document:

- whether inputs may be null;
- whether strings are borrowed views or owned buffers;
- who closes/frees/releases resources;
- when returned views become invalid;
- whether async use is allowed.

No implicit ownership transfer.

## Cleanup Style

Use `goto cleanup` for multi-resource functions.

Pattern:

```c
SlStatus status = sl_status_ok();

resource = acquire();
if (!resource) {
    status = sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    goto cleanup;
}

cleanup:
    release(resource);
    return status;
```

Cleanup blocks release only initialized resources.

## Forbidden APIs And Patterns

- raw `malloc`/`free` outside allocator modules;
- unchecked integer arithmetic for sizes;
- variable-length arrays;
- unbounded parser recursion;
- implicit ownership transfer;
- global mutable runtime state except controlled process initialization;
- magic numbers in parser/protocol/allocation logic;
- unchecked `snprintf`, `memcpy`, `memmove`;
- naked internal `char*` string APIs;
- `strlen`-driven internal logic outside boundary adapters;
- ad hoc string/byte append loops, local mini-builders, or manual buffer construction when
  `SlStringBuilder`, `SlByteBuilder`, arena copy helpers, or existing string/byte helpers
  fit;
- JS raw pointer exposure;
- V8 type leakage outside `src/engine/v8/`;
- OS-specific includes outside `src/platform/*`.

## Platform Isolation

Core runtime modules must not include OS-specific headers and must not call OS APIs
directly. No scattered `#ifdef _WIN32` branches should appear in runtime logic.

Platform checks belong in platform abstraction or tiny detection headers only.
`include/sloppy/platform.h` may define platform/compiler detection macros, but it must not
become a dumping ground for behavior.

Prefer capability and platform abstraction APIs over platform macros in core modules.

## Static Analysis Rules

`.clang-tidy` is part of the project contract. It starts with analyzer, bugprone,
portability, and high-signal bug checks. The enforceable advanced-analysis baseline is the
repo-wide `sloppy_memory_analysis` target over configured native sources, unit tests, fuzz
seed-replay targets, and benchmark harnesses in the current compile database. Generic
style-noise checks stay out unless a scoped task proves they catch a Sloppy invariant.

New warnings should be fixed or explicitly documented. CI treats warnings as failures for
configured gates. `NOLINT` suppressions require `sloppy-analysis-suppress: #issue reason;
remove when condition`.

## Public API Shape

Public C APIs should prefer:

- explicit `SlStatus`;
- output parameters for owned results;
- `SlStr`/`SlBytes` views;
- typed handles;
- no raw platform types;
- no engine-specific types.

Start APIs narrow. Do not add flags, callbacks, hooks, or provider-style interfaces before
there is a documented boundary, a tested safety invariant, or more than one real use case.

## Testing Requirements

Every new C module needs:

- unit tests for success and failure paths;
- ownership/lifetime tests where applicable;
- invalid argument tests;
- sanitizer-friendly behavior;
- static analysis clean result.

## Quality Gates

- `clang-format`;
- `clang-tidy`;
- clang-tidy/Clang Static Analyzer evidence for non-doc analysis-relevant changes;
- CMake build;
- CTest;
- warnings-as-errors in CI;
- platform-boundary scanner;
- no generated artifacts staged.

## Acceptance Criteria For C Code Entering Repo

C code is acceptable when:

- it is C17-compatible;
- public symbols follow naming rules;
- ownership is documented;
- cleanup is deterministic;
- tests cover meaningful behavior;
- static analysis gates pass;
- no OS or V8 leakage appears outside allowed directories;
- no raw allocator calls appear outside allocator modules.

## Open Questions

- Exact comment template for public headers.
- Whether to require include-what-you-use later.
