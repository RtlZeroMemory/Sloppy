# Execution Plan: Memory SIMD Expansion

## Goal

Expand Wave 4 SIMD from a first byte-search candidate into a guarded SIMD backend family
for Sloppy's existing scalar memory APIs, with automatic safe baseline enablement,
explicit advanced tiers, and strong parity/fuzz/sanitizer evidence.

SIMD must remain invisible to callers: public APIs stay length-based scalar contracts such
as `sl_bytes_find`, `sl_bytes_find_any`, `sl_str_equal_ci_ascii`, and
`sl_str_contains_nul`. Backends are selected by CMake configuration and must preserve the
scalar reference behavior exactly.

## Source Docs

- `AGENTS.md`
- `docs/c-standards.md`
- `docs/c-style.md`
- `docs/memory.md`
- `docs/performance.md`
- `docs/testing-strategy.md`
- `docs/quality-gates.md`
- `docs/modules/core/README.md`
- `docs/exec-plans/README.md`
- `#762`, `#794`, `#795`, `#796`, `#797`, `#798`

## Non-goals

- No AVX-512.
- No broad parser rewrite.
- No public performance claims from smoke or debug evidence.
- No runtime CPU dispatch until platform support is explicit and tested.
- No new public SIMD API surface.
- No compatibility or V2 APIs.
- No SIMD path that can run on unsupported CPUs by accident.
- No hidden SSO in borrowed `SlStr`; borrowed views do not own storage.

## Scope

### In Scope Now

- Build configuration:
  - default `SLOPPY_ENABLE_SIMD=AUTO`;
  - `SLOPPY_ENABLE_SIMD=OFF` scalar-only fallback;
  - `SLOPPY_ENABLE_SIMD=ON` fail-fast if no supported backend exists;
  - `SLOPPY_SIMD_LEVEL=AUTO|SSE2|AVX2`;
  - `windows-simd` baseline preset;
  - `windows-avx2` advanced preset;
  - CI matrix for baseline and AVX2 parity lanes.
- Existing scalar APIs:
  - `sl_bytes_find`;
  - `sl_bytes_find_any`;
  - `sl_str_contains_nul`;
  - `sl_str_equal_ci_ascii`;
  - `sl_str_starts_with_ci_ascii` and `sl_str_ends_with_ci_ascii` through canonical
    `sl_str_equal_ci_ascii` slices.
- SSO:
  - do not hide inline storage under arena-backed builders whose views can escape;
  - add explicit small-builder or short-owned primitives only where lifetime is local or
    materialized before return;
  - keep fixed builders caller-owned and non-growing;
  - keep `SlStr`/`SlBytes` borrowed and non-owning;
  - report small storage in stats only for explicit small-storage primitives.
- Tests:
  - scalar-vs-SIMD parity matrices for short, exact-vector, tail, embedded-zero,
    non-ASCII, and first-match behavior;
  - default seed replay through `fuzz_memory_primitives`;
  - benchmark smoke only as harness/backend-selection evidence;
  - sanitizers and libFuzzer seed replay through the mandatory lanes.

### Candidate Scope After Current Backends Are Stable

- Add internal byte equality/prefix/suffix SIMD helpers only if they can replace existing
  `memcmp` hot paths behind current APIs without reducing libc quality or adding risk.
- Consider ASCII trim/classification helpers only after route/HTTP callers expose a
  canonical scalar primitive that tests can pin.
- Consider array fill/copy helpers only after Sloppy owns a real array/buffer abstraction
  or a repeated hot path currently open-codes this behavior.

## Steps

### Step 1: Stabilize Current WIP

- Fix current failing `core.str.views` parity test across `windows-dev`, `windows-simd`,
  and `windows-avx2`.
- Verify `core.bytes.views` remains green across all three presets.
- Keep current backend files isolated:
  - `src/core/bytes_simd_sse2.c`;
  - `src/core/bytes_simd_avx2.c`;
  - `src/core/string_simd_sse2.c`;
  - `src/core/string_simd_avx2.c`.
- Ensure public dispatch stays in:
  - `src/core/bytes.c`;
  - `src/core/string.c`.

Acceptance:

- `ctest --preset windows-dev -R "core\.(bytes|str)" --output-on-failure`
- `ctest --preset windows-simd -R "core\.(bytes|str)" --output-on-failure`
- `ctest --preset windows-avx2 -R "core\.(bytes|str)" --output-on-failure`

### Step 2: Lock Configuration Semantics

- Make `AUTO` baseline-safe and enabled on supported x86/x64 platforms.
- Keep AVX2 explicit through `SLOPPY_SIMD_LEVEL=AVX2` and `windows-avx2`.
- Add configure messages that name the selected backend.
- Confirm stale CMake cache behavior is avoided by pinning preset values.
- Verify unsupported architecture behavior falls back under `AUTO` and fails under `ON`.

Acceptance:

- Configure logs show `SIMD byte/string backend: SSE2` for normal x64 presets.
- Configure logs show `SIMD byte/string backend: AVX2` for `windows-avx2`.
- Presets parse as JSON.
- Workflow YAML parses.

### Step 3: Expand Only Existing Contracted Hot Paths

- Keep vectorized implementations limited to APIs with already documented semantics:
  - byte find;
  - byte find-any;
  - no-NUL scan;
  - ASCII case-insensitive equality;
  - ASCII case-insensitive starts/ends through equality slices.
- Do not add SIMD-only behavior to parser internals unless a scalar primitive is first
  introduced and adopted.
- Do not add generic array SIMD until there is a current Sloppy-owned array/buffer API.

Acceptance:

- No new public SIMD headers.
- No duplicate public old/new paths.
- No direct backend calls outside `src/core/bytes.c` and `src/core/string.c`.

### Step 4: Strengthen Parity Tests

- Expand byte tests to cover:
  - lengths 0 through at least 129;
  - first byte, boundary byte, vector-width boundary, and tail matches;
  - repeated needles;
  - high-bit bytes;
  - empty needle set;
  - invalid non-empty `NULL` inputs leaving outputs unchanged.
- Expand string tests to cover:
  - lengths 0 through at least 129;
  - exact ASCII case fold;
  - punctuation that must not case-fold;
  - non-ASCII exact-byte behavior;
  - embedded NUL detection;
  - starts/ends case-insensitive slices.
- Keep scalar oracle inside tests rather than comparing one backend against another
  through public dispatch.

Acceptance:

- Tests fail if SIMD returns a later match than scalar.
- Tests fail if non-ASCII bytes are treated as ASCII.
- Tests fail if embedded NUL behavior changes.

### Step 5: Validate Mandatory Evidence

Run local focused gates first:

```powershell
.\tools\windows\dev.ps1 configure -Preset windows-dev
.\tools\windows\dev.ps1 build -Preset windows-dev
ctest --preset windows-dev -R "core\.(bytes|str)|fuzz\.memory_primitives\.seed_replay|benchmarks\.sloppy_bench" --output-on-failure

.\tools\windows\dev.ps1 configure -Preset windows-simd
.\tools\windows\dev.ps1 build -Preset windows-simd
ctest --preset windows-simd -R "core\.(bytes|str)|fuzz\.memory_primitives\.seed_replay|benchmarks\.sloppy_bench" --output-on-failure

.\tools\windows\dev.ps1 configure -Preset windows-avx2
.\tools\windows\dev.ps1 build -Preset windows-avx2
ctest --preset windows-avx2 -R "core\.(bytes|str)|fuzz\.memory_primitives\.seed_replay|benchmarks\.sloppy_bench" --output-on-failure
```

Then run full local gates before PR:

```powershell
.\tools\windows\dev.ps1 test -Preset windows-dev
.\tools\windows\dev.ps1 format-check -Preset windows-dev
.\tools\windows\dev.ps1 lint -Preset windows-dev
.\tools\windows\dev.ps1 analyze -Preset windows-dev
.\tools\windows\dev.ps1 build -Preset windows-asan
ctest --preset windows-asan -R "core\.(bytes|str)|fuzz\.memory_primitives\.seed_replay" --output-on-failure
.\tools\windows\dev.ps1 build -Preset windows-libfuzzer
ctest --preset windows-libfuzzer -L fuzz --output-on-failure
git diff --check
```

Run V8 evidence because this is core/runtime code:

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

### Step 6: Add Safe SSO

- Add explicit small-storage builder entry points rather than changing arena-builder
  lifetime under existing call sites.
- Candidate API:
  - `SL_BYTE_BUILDER_SMALL_CAPACITY`;
  - `sl_byte_builder_init_small`;
  - `sl_string_builder_init_small`.
- Small builders are fixed-capacity builder instances backed by storage inside the builder.
  Their views have builder lifetime, not arena lifetime.
- Do not use small builders for APIs that return a builder view through `out` unless the
  result is copied/materialized to arena storage before return.
- Preserve fixed builder behavior exactly.
- Preserve self-overlap append behavior for small, fixed, and arena storage.
- Extend stats with a storage kind that distinguishes explicit small storage.
- Add tests for:
  - small builder append/view/reset;
  - capacity exceeded without modifying the prefix;
  - self-overlap append;
  - `view_with_nul` fitting small storage and failing cleanly when exact capacity has no
    terminator room;
  - builder stats/counters.
- Adopt small builders only in local-only hot paths where the view is consumed before the
  builder goes out of scope. If a candidate path returns a view, keep arena builders or
  materialize the result first.
- Extend benchmark smoke notes to identify small-builder counters without making
  performance claims.

Acceptance:

- `core.builder_foundation` passes.
- `fuzz.memory_primitives.seed_replay` passes.
- Builder docs describe SSO as an implementation of arena builders, not new ownership.

## Acceptance Criteria

- Scalar API behavior is unchanged.
- Default supported x86/x64 builds enable baseline SIMD automatically.
- AVX2 is available only through explicit advanced configuration.
- Unsupported architectures fall back under `AUTO`.
- Forced SIMD fails configuration when unsupported.
- SIMD test lanes cover bytes, strings, fuzz seed replay, and benchmark smoke.
- Sanitizer/libFuzzer mandatory lanes remain intact.
- No benchmark/performance claims are added.
- No OS APIs enter core SIMD files.
- No V8 types or JS/native bridge changes are introduced.
- Explicit small builders provide SSO for local construction without changing borrowed-view
  or arena-backed output contracts.

## Validation Commands

Primary:

```powershell
ctest --preset windows-dev -R "core\.(bytes|str)|fuzz\.memory_primitives\.seed_replay|benchmarks\.sloppy_bench" --output-on-failure
ctest --preset windows-simd -R "core\.(bytes|str)|fuzz\.memory_primitives\.seed_replay|benchmarks\.sloppy_bench" --output-on-failure
ctest --preset windows-avx2 -R "core\.(bytes|str)|fuzz\.memory_primitives\.seed_replay|benchmarks\.sloppy_bench" --output-on-failure
```

Full before PR:

```powershell
.\tools\windows\dev.ps1 test -Preset windows-dev
.\tools\windows\dev.ps1 format-check -Preset windows-dev
.\tools\windows\dev.ps1 lint -Preset windows-dev
.\tools\windows\dev.ps1 analyze -Preset windows-dev
ctest --preset windows-asan -R "core\.(bytes|str)|fuzz\.memory_primitives\.seed_replay" --output-on-failure
ctest --preset windows-libfuzzer -L fuzz --output-on-failure
git diff --check
```

## Decision Log

- 2026-05-07: Keep scalar APIs as the only public surface.
- 2026-05-07: Enable baseline SIMD automatically on supported x86/x64.
- 2026-05-07: Keep AVX2 explicit through `SLOPPY_SIMD_LEVEL=AVX2` to avoid accidental
  illegal-instruction risk in general binaries.
- 2026-05-07: Do not add array SIMD until Sloppy owns a canonical array/buffer API or a
  repeated hot path needs a scalar helper first.
- 2026-05-07: Do not hide SSO inside arena builders because existing diagnostics, HTTP,
  provider, and platform APIs return builder views from local builders expecting arena
  lifetime.

## Progress Log

- 2026-05-07: Initial SSE2 and AVX2 backend files are in progress.
- 2026-05-07: `core.bytes.views` is passing in scalar/SSE2/AVX2 after parity test fix.
- 2026-05-07: `core.str.views` currently fails in SIMD-enabled presets and must be fixed
  before expanding or opening the PR.

## Risks

- AVX2 builds can compile on CI but may fail at runtime if the runner lacks AVX2. Mitigate
  by keeping AVX2 in its own lane and disabling that lane only with an explicit issue
  comment if the runner proves unsupported.
- Signed byte SIMD comparisons can accidentally treat non-ASCII as alphabetic. Mitigate
  with high-bit and punctuation parity tests.
- First-match SIMD masks can return a later match than scalar. Mitigate with boundary and
  repeated-needle tests.
- `memcmp` may already be highly optimized; replacing equality/prefix/suffix blindly could
  regress performance. Keep it as a candidate only after benchmark evidence or a clear
  owned-contract reason.
- Runtime CPU dispatch would require platform abstraction and careful testing. Keep this
  out of the current wave unless explicitly scoped.
- Hidden SSO inside `SlOwnedStr` or arena builders can create self-pointer/copy/lifetime
  bugs. Keep SSO explicit and local-lifetime unless a future owned-string type is designed.

## Completion Notes

Pending. The current implementation must not be pushed until all scalar, SSE2, and AVX2
parity lanes pass locally.
