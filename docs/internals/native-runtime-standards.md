# Native Runtime Standards

Sloppy's native runtime is pre-alpha, but its C and C++ code still has to be
boring to review. This page defines the standards for code under
`include/sloppy/`, `src/core/`, `src/platform/`, `src/data/`, `src/engine/`,
`src/cli/`, native tests, fuzz targets, and benchmarks.

## Module Boundaries

- `src/core/` owns portable runtime logic. It must not include OS, libuv, V8,
  database-driver, or provider-driver headers.
- `src/platform/` owns OS and libuv integration behind Sloppy-shaped APIs.
  Platform handles must not leak into public headers, core structs, or
  JavaScript-visible values.
- `src/engine/engine.c` owns the engine-neutral C boundary. V8 APIs, handles,
  and C++ implementation details stay under `src/engine/v8/`.
- `src/data/` owns provider-specific native driver boundaries. SQLite, libpq,
  ODBC, and provider helper types must stay isolated to provider files and
  provider headers.
- `src/cli/` may parse Plan and artifact metadata for commands, but CLI parser
  helpers must not become runtime core dependencies.
- Public headers in `include/sloppy/` expose deliberate API and ABI surfaces.
  Prefer forward declarations and small contracts over exporting internal
  helper structs.

## C Safety

- Use checked size arithmetic for capacities, offsets, lengths, and counts.
- Check multiplication before encoded-size, array, row, or driver-parameter
  capacity calculations. A "large input is impossible" assumption is not a
  safety argument.
- Do not use raw `strcpy`, `strcat`, `sprintf`, or unchecked pointer arithmetic.
- Write through bounded builders or explicit capacity checks.
- Treat external input as untrusted until parsed and validated.
- Keep every allocation paired with a clear owner and cleanup path.
- Clear output pointers or result structs before returning failure when callers
  may pass sentinels or reused storage.
- Roll back arena marks after validation-only allocations that do not become
  part of the successful result.
- Do not store arena-backed pointers beyond the arena's lifetime.
- Do not store request-lifetime views in app-lifetime objects.
- Keep cleanup labels consistent inside a function; cleanup must run on every
  path that acquired resources.

## Status And Diagnostics

- Return `SlStatus` when a caller needs to distinguish invalid input, capacity,
  unsupported features, provider failure, lifecycle failure, or internal bugs.
- Use `bool` only for pure predicates where no diagnostic detail is needed.
- Prefer existing `SlDiagCode` values. Add a new code when the failure needs a
  stable contract.
- Redact secrets before diagnostics, doctor output, CLI output, logs, and test
  reports. Never echo raw connection strings, tokens, passwords, or large
  request/response bodies.
- Redaction covers every rendered diagnostic field, including hints, provider
  details, report JSON, Authorization values with any scheme, and backend error
  text attached to a SQL statement.
- Copy or render driver-owned error text before clearing provider result handles.
  Do not hold pointers returned by a driver after the result, statement, or
  handle that owns them has been released.
- Keep previews bounded and deterministic.
- Construct diagnostics off the success path in hot code whenever possible.

## Memory And Lifetime

- Arena ownership must be visible at the call boundary. If a return value points
  into an arena, the function name or documentation should make that lifetime
  obvious.
- Heap allocations must have one owner and one shutdown path. Close/dispose
  functions should be idempotent where the resource can be reached from multiple
  cleanup paths.
- V8 `Local` handles stay inside handle scopes. Long-lived V8 references require
  bridge-owned persistent/global handles and explicit reset during shutdown.
- libuv handles must be closed through the platform/libuv owner and must not be
  observed as closed before their close callback has run.
- Once a libuv handle initializes successfully, every accept, bind, connect, or
  setup failure path must close it through the owning close callback.
- Provider connections, statements, results, cursors, and transactions must have
  deterministic cleanup on normal completion, early exit, cancellation, timeout,
  and runtime shutdown.

## Security Regression Guardrails

- Narrowing sizes for C, C++, ODBC, libpq, V8, or OS APIs requires an explicit
  upper-bound check against the target type before the cast.
- Read-only database providers must reject obvious writes in Sloppy before
  execution and, where the backend supports it, configure the backend connection
  itself as read-only.
- Provider diagnostics for statement failures should redact the statement and
  suppress backend detail when the backend may echo user SQL or parameter values.
- Recursive delete and directory traversal code must not follow symlinks,
  junctions, or directory reparse points by default. POSIX directory opens use
  `O_NOFOLLOW`; do not define it to `0` or otherwise compile away the guard.
  Platforms without it must fail clearly instead of silently following symlinks.
  Windows directory recursion checks `FILE_ATTRIBUTE_REPARSE_POINT` before descending.
- Windows dynamic-library loading goes through the platform helper and uses
  `LoadLibraryExW` search flags. Plain `LoadLibraryW` is not acceptable in
  Sloppy-owned implementation code.
- Security fixes should leave a guardrail behind when a broad mechanical rule is
  possible. Prefer extending the existing standards scanner or focused tests over
  adding one-off scripts.

## Hot Paths

- Avoid heap allocation, repeated parsing, and string formatting in request,
  route lookup, response writing, provider row iteration, and metric/log disabled
  paths.
- Keep profile and diagnostic hooks cheap when disabled.
- Cache derived request/header/route state only when the ownership and invalidation
  rules are clear.
- Do not trade readable ownership for speculative performance. Measure first,
  and report benchmark data as local measurement rather than a product claim.

## C++ Usage

- Use C++ where the boundary requires it, primarily V8 and C++ test harnesses.
- RAII is welcome inside C++ boundary files when it simplifies V8, provider, or
  platform cleanup.
- Do not leak C++ abstractions into C core headers or C runtime structs.
- C++ exceptions must stop at the bridge and be mapped to `SlStatus`/`SlDiag`.

## Tests, Fuzz, And Benchmarks

- Parser, serializer, binary artifact, path/header, and metadata surfaces need
  unit tests and fuzz coverage for malformed input.
- Error paths should be tested, especially cleanup-on-error, capacity failures,
  redaction, and malformed metadata.
- Binary formats and deterministic CLI output need goldens or equivalent
  fixtures.
- Heavy fuzz, live-provider, sanitizer, stress, and benchmark lanes may be
  opt-in, but default-safe smoke coverage should exist where feasible.
- Benchmarks are measurement data only. They do not replace correctness,
  cleanup, redaction, or fuzz coverage.

## Review Checklist

Before merging native runtime changes, confirm:

- The touched code stays inside the documented subsystem boundary.
- Public headers expose only deliberate API.
- Status/error behavior is consistent with nearby code.
- Every allocation, handle, cursor, and V8 reference has a visible owner.
- Capacity and offset arithmetic is checked.
- Diagnostics are redacted and bounded.
- Hot-path hooks are cheap when disabled.
- Tests or fuzz seeds cover success, malformed input, and cleanup failure paths.
