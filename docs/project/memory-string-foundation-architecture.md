# Memory/String Foundation Architecture

Status: strategic design for ENGINE-21, with ENGINE-21.A/B/C/E/F primitive foundations
implemented.

ENGINE-21 makes memory and string handling a first-class engine foundation layer. It is not
a bag of helper utilities. It defines the ownership, lifetime, allocation, and conversion
rules that HTTP, V8, SQLite, diagnostics, Plan, CLI, and conformance work can depend on.
This implementation slice provides the core lifetime/allocation rules, view/copy/hash
helpers, bounded builders, bounded interning, and safety tests. V8/SQLite interop policy
remains tracked by ENGINE-21.D / #367, and subsystem adoption remains ENGINE-22.

Core rule:

> Include only what is defined. Configure only what is configured. Allocate only where
> ownership requires it.

## Goal

Provide top-notch memory and string primitives for Slop's C runtime kernel:

- fast enough for request and provider hot paths;
- safe against dangling views, unchecked growth, and hidden ownership transfer;
- easy enough that ordinary runtime code naturally uses the right primitive;
- scalable enough to support request/app/temp lifetimes, async completion, V8 conversion,
  SQLite results, diagnostics, HTTP buffers, and app-lifetime symbol tables without a
  later rewrite.

## Lifetime Model

| Lifetime | Owner | Freed or invalidated | May escape? | Intended use |
| --- | --- | --- | --- | --- |
| App/startup | App host/runtime startup state | App shutdown or startup abort cleanup | Yes, only as app-owned data | Plan graph, route table, provider metadata, bootstrap artifacts, module/cache metadata. |
| Request | Request scope | After sync completion, Promise settlement, cancellation, timeout, or response cleanup | No, unless copied to app/heap/resource ownership | Parsed request data, route/query/header views, body buffer, response conversion scratch, request diagnostics. |
| Temporary/scratch | Caller or scoped operation | Reset to mark or scratch scope end | No | Formatting, parser fragments, conversion staging, validation scratch. |
| Package/static asset | Artifact/package loader | Runtime/package lifetime | Yes, as immutable asset views or copied app-owned data | Bootstrap stdlib asset bytes, generated app artifact bytes, source maps where loaded. |
| Intern table | App host or package graph | App shutdown or package unload | Yes, as app/static-lifetime symbols only | Route names, method names, module names, capability names, provider names, stable Plan keys, and other repeated metadata identifiers. |
| V8 conversion | V8 bridge owner thread | End of bridge call or explicit arena/resource lifetime | No transient native view may be persisted in JS | Native view to V8 string, V8 string to native copied string, exception text, result descriptors. |
| SQLite result | Native provider call arena or request result arena | Arena reset/request cleanup | Only after explicit copy | Column names, row values, blobs, JS row materialization inputs. |
| Diagnostic output | Diagnostic arena or CLI output builder | Arena reset or command output completion | Yes only while diagnostic arena lives | Text diagnostics, JSON diagnostics, source frames, redacted output. |

Request cleanup interacts with async handlers by retaining request-owned memory until the
handler outcome is final: sync success, sync error, fulfilled Promise, rejected Promise,
pending timeout, cancellation, overflow/backpressure rejection, or shutdown cancellation.
Native work that can outlive the request stack must not borrow request or scratch memory.
It must copy data, own it through a resource table entry, or use a future explicitly owned
operation allocation.

Forbidden:

- returning a view into a scratch arena;
- keeping request-arena views after request cleanup starts;
- storing independently closable resources only in arenas;
- exposing native pointers or V8 handles to JavaScript;
- relying on C-string termination for `SlStr`/view data;
- hidden global mutable allocation state.

## Allocation Model

Arenas are for scoped lifetimes and bounded transient data. They are not a universal memory
solution.

Rules:

- Use stack storage for tiny fixed-size local work when size is bounded and not returned.
- Use a caller/request/app arena for scoped data whose lifetime matches the arena.
- Use resource tables or provider-owned objects for independently closable resources.
- Use heap/native provider allocation only at documented boundaries, such as V8 internals,
  SQLite/provider libraries, process/platform abstractions, or future allocator modules.
- Use checked size arithmetic for all allocation, growth, formatting, and copy sizes.
- Failed calls leave output parameters unchanged unless a public contract explicitly says
  the output is reset before work begins.
- Fixed caller-provided storage exhaustion returns `SL_STATUS_CAPACITY_EXCEEDED`.
- True heap/provider allocation failure returns `SL_STATUS_OUT_OF_MEMORY`.
- Overflow returns `SL_STATUS_OVERFLOW`.
- Growth policies must be bounded by caller-supplied limits. Default growth should not hide
  unbounded body/header/diagnostic/response allocation.
- Intern tables must have explicit capacity/growth limits, deterministic failure behavior,
  and a documented owner. Interning must reduce repeated metadata/string ownership, not
  create a hidden global allocator.
- Zero initialization is required for public structs before first init when stale state can
  cause reuse or cleanup bugs.
- Alignment follows `SlArena`: nonzero power-of-two alignment, checked padding, and no
  ambiguous zero-sized allocations.

ENGINE-21 should not introduce a complex allocator framework before it has real call sites.
The first allocator policy should be narrow: ownership documentation, raw-allocation
scanner enforcement, arena copy helpers, and carefully scoped owned string/buffer types.

## String Model

| Concept | Meaning | Ownership | NUL policy | UTF-8 policy | Equality/hash | Conversion policy |
| --- | --- | --- | --- | --- | --- | --- |
| Byte span | Non-owning bytes, not necessarily text. | Borrowed. | No terminator. | None. | Byte-wise. Hash helper may be added when used by route/header/plan tables. | Convert only through explicit encoding-aware API. |
| String view | Non-owning UTF-8-ish source slice, not necessarily NUL-terminated. | Borrowed. | No terminator required. | Treated as bytes unless caller validates UTF-8. | Byte-wise equality by default. | Boundary adapters add NUL or validate encoding when calling C/V8/provider APIs. |
| Owned string | Arena-owned or heap/operation-owned string bytes. | Owner documented by type/function. | NUL optional and explicit; view length excludes terminator when present. | Same as source unless validation requested. | Byte-wise; optional hash only with documented table use. | Returned as `SlStr` view plus lifetime contract. |
| Byte builder | Mutable growable byte output target. | Builder-owned over arena/caller buffer/owned allocation depending on init. | None. | None. | Not applicable. | Finalize to `SlBytes`. |
| String builder | Mutable growable text output target. | Builder-owned over arena/caller buffer/owned allocation depending on init. | Optional final NUL for boundary adapters. | Appends string views; byte appends require explicit API. | Not applicable. | Finalize to `SlStr`. |
| Interned string | Deduplicated app/static-lifetime string returned as a stable symbol/view pair. | Intern table owns bytes and symbol metadata. | No public assumption; optional NUL is explicit. | Same byte-level default; validation is opt-in per table/API. | Hash/equality required; pointer identity is an optimization, not the only correctness rule. | Convert into V8/SQLite/diagnostic strings through the same explicit boundary adapters as ordinary views. |

ENGINE-21 keeps `SlStr` and `SlBytes` small and borrowed. The implemented helper surface is
intentionally narrow: arena copy helpers, suffix helpers, deterministic hash helpers used by
the intern table, and explicit C-string boundary copies where a NUL terminator is required.
ASCII case-insensitive comparison remains a subsystem-specific follow-up when HTTP adoption
needs it.

String interning is in scope for ENGINE-21 because Plan graphs, route graphs, module names,
capability names, provider names, HTTP method tokens, and diagnostic code/name metadata can
otherwise accumulate repeated app-lifetime copies. The intended primitive is a bounded
app/static-lifetime intern table, not a process-global string pool. Interned strings may be
used for stable metadata lookup and identity acceleration, but public behavior must remain
byte-equality correct.

Interning rules:

- intern only stable metadata or explicitly app-owned strings;
- never intern request bodies, secrets, connection strings, arbitrary user payloads, or
  transient diagnostic text;
- store hash and length with the interned value;
- handle hash collisions by byte comparison;
- make table capacity, growth, and OOM behavior explicit;
- return a stable symbol or interned-string view whose lifetime is tied to the table owner;
- document whether an API requires an interned input or accepts any `SlStr`;
- provide tests for duplicate insertion, collision behavior where practical, table
  exhaustion, and lifetime invalidation at app/package cleanup.

## Builder Model

Required builder concepts:

- byte builder for response bytes, body buffers, binary diagnostics, and future blob paths;
- string builder for diagnostics, path assembly, source frames, CLI output, and generated
  small text;
- formatter helpers for size/integer/status-code/string/view writes without unsafe
  `sprintf`;
- response builder target that can write into caller buffers first and later into an owned
  response buffer without rewriting response logic;
- diagnostic builder target that replaces repeated ad hoc two-pass writer code only after
  tests preserve exact output.

JSON builder policy: include only the minimal escaping/emission target needed for
diagnostics and CLI output. Do not build a JSON DOM library in ENGINE-21.

Implemented builder surface:

- `SlByteBuilder` over fixed caller storage or arena storage with explicit max capacity;
- `SlStringBuilder` over the same storage model;
- append/reserve helpers for bytes, chars, strings, and small decimal integer formatting;
- `sl_string_builder_view_with_nul` for boundary adapters that require a terminator;
- failed append/reserve calls preserve the already-written prefix and the builder remains
  usable.

Builder failure behavior:

- all append/grow operations return `SlStatus`;
- growth checks use checked arithmetic;
- capacity exhaustion is deterministic;
- the builder remains valid after a failed append; views after failure expose the
  previously written prefix;
- output parameters remain unchanged on failure where project style expects that.

## Safety Rules

- No dangling views across app/request/temp/V8/SQLite/diagnostic lifetime boundaries.
- No hidden ownership transfer; every public API states borrowed, arena-owned,
  caller-owned, resource-table-owned, provider-owned, or engine-owned.
- No raw pointer exposure to JavaScript.
- No V8 handle leakage outside `src/engine/v8/*`.
- No connection string, password, token, access-token, API key, or provider handle leakage
  in diagnostics, JSON, CLI output, or issue examples.
- No unchecked integer overflow in allocation, builder growth, length calculation, hash
  table sizing, or formatting.
- No implicit NUL-terminated assumptions for `SlStr` or byte spans.
- Failed calls leave outputs unchanged unless the API documents an early reset.
- No raw `malloc`/`free` outside allocator/provider/platform boundaries once allocator
  policy is implemented and scanner-enforced.

## V8 Interop Strategy

Native to V8:

- Convert `SlStr`/string view to V8 string only on the V8 owner thread.
- Use explicit length when constructing V8 strings; do not rely on NUL termination.
- Treat invalid conversion as a deterministic bridge failure or thrown JS exception.
- Do not let V8 persist a pointer into native transient memory.

V8 to native:

- Convert V8 strings into request/operation/diagnostic arena-owned strings when C code
  needs the value after the immediate conversion expression.
- `std::string` may remain a private C++ bridge staging type, but ENGINE-21 must document
  when it is allowed and ENGINE-22 should remove duplicated conversion helpers.
- Exception messages, stack summaries, result bodies, and SQLite parameters must cross the
  boundary with explicit copied ownership.

Encoding:

- V8 conversions are UTF-8 by construction where V8 APIs produce UTF-8 bytes.
- Slop core string views remain byte-length views unless a specific API validates UTF-8.

## SQLite Interop Strategy

SQLite text/blob ownership:

- SQLite `text` and future `blob` values are valid only according to SQLite statement
  lifetime rules and must be copied before statement step/finalize invalidates them.
- Query results exposed to C or JS should be arena/request-owned unless the API explicitly
  returns an independently owned result object.
- Statement/transaction strings and parameter text/blob values must outlive the provider
  call. If provider work becomes async/offloaded, parameters must be copied into operation
  ownership before submission.
- JS row materialization must copy from native result arenas into V8 objects on the owner
  thread; no V8 object should reference native row memory after conversion.

Prepared statement policy remains an ENGINE-17 decision. ENGINE-21 only defines memory
lifetime requirements so prepared statements cannot accidentally borrow short-lived SQL or
parameter memory.

## HTTP Strategy

- Request line, path, query, and header views may be zero-copy only when they point into a
  request-owned buffer that survives through handler/result conversion.
- Complete-buffer parser copies are acceptable for the current skeleton. A streaming
  backend should preserve views into request-owned read buffers only while those buffers
  remain stable.
- Header storage must have count and byte limits.
- Body buffers are request-owned, bounded, cancellation-aware, and never unbounded by
  default.
- Response builders own output until write completion. They may write into caller-provided
  buffers for dev/test paths and later into owned buffers for real backend paths.
- Parser limits and buffer reuse should prefer bounded reuse over repeated allocation in
  request hot paths.

Zero-copy is allowed only where the lifetime is simpler than copying. Safety beats clever
avoidance of copies.

## Diagnostics Strategy

- Source-frame builder, JSON diagnostic builder, redaction, and CLI diagnostic output
  should use one shared builder/formatter model after ENGINE-21.C lands.
- Diagnostic output must be stable and golden-testable.
- Redaction helpers must avoid repeated ad hoc `snprintf` or manual buffer chains.
- Low-level memory helpers return status without recursively allocating diagnostics.
- Higher-level diagnostics may allocate into diagnostic arenas with exact ownership.
- Diagnostic code/name metadata may use an app/static intern table, but formatted
  diagnostic output must stay builder-owned and must not rely on interning request-specific
  or secret-containing text.

## ENGINE-21 Task Shape

- TASK ENGINE-21.A: Lifetime and Allocation Model.
- TASK ENGINE-21.B: String and Byte View Primitives.
- TASK ENGINE-21.C: String Builder, Byte Builder, and Formatting Utilities.
- TASK ENGINE-21.D: V8 and SQLite String Interop Policies.
- TASK ENGINE-21.E: Memory Safety and Stress Tests.
- TASK ENGINE-21.F: String Interning and Symbol Table Foundation.

Implementation status:

- ENGINE-21.A/B/C/E/F primitive work is implemented in `include/sloppy/string.h`,
  `include/sloppy/bytes.h`, `include/sloppy/builder.h`, `include/sloppy/intern.h`,
  `src/core/string.c`, `src/core/bytes.c`, `src/core/builder.c`, `src/core/intern.c`, and
  focused unit tests.
- ENGINE-21.D remains a follow-up for V8/native and SQLite text/blob helper policy.
- ENGINE-22 remains the adoption layer for HTTP, diagnostics/CLI, Plan/artifacts, V8,
  SQLite, and cleanup/regression guards.

## Non-goals

- General-purpose STL clone.
- Complex allocator framework.
- Lock-free allocator.
- Unbounded or process-global string intern pool.
- Full Unicode library, normalization, collation, regex, or display-width engine.
- JSON DOM library unless separately scoped.
- ORM or migration layer.
- Node Buffer compatibility.
- Package-manager or npm behavior.
- Broad runtime/compiler/provider refactor in ENGINE-21 itself.
