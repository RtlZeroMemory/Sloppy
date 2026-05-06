# V8 Bridge

This directory owns the isolated C++ V8 bridge.

The V8 bridge has grown beyond the initial TASK 07 smoke. It still remains opt-in and
isolated under this directory. Current V8-gated evidence covers SDK detection, the
engine-neutral C ABI, isolate/context lifecycle, classic-script evaluation, owner-thread
checks, Promise scheduling, runtime bridge smoke, selected stdlib intrinsic registration,
HTTP dispatch execution, and guarded provider/feature activation.

Rules:

- The default build leaves the V8 bridge disabled.
- Enable SDK validation explicitly with `-DSLOPPY_ENABLE_V8=ON` or `-DSLOPPY_ENGINE=v8`.
- `SLOPPY_V8_ROOT` must point to the documented prebuilt SDK layout when V8 is enabled.
- C++ is allowed here only for engine binding work.
- `v8::*` types must never leak outside this directory.
- V8 headers may appear only under this directory.
- JS code must never receive raw C pointers.
- Native handles exposed to JS must flow through `SlResourceId` values with generation,
  liveness, and kind checks in `SlResourceTable`.
- `engine_v8.cc` owns isolate/context lifecycle, handler registration, source evaluation,
  owner-thread checks, and Promise orchestration only.
- Framework-specific V8 bridge code such as HTTP request context materialization and
  `Results.*` conversion lives in dedicated sibling modules such as `http_bridge.cc`; do
  not add it directly to `engine_v8.cc`.
- Provider-specific JS-to-native bridges live in `intrinsics_<provider>.cc` files and are
  reached through `intrinsics.cc`; do not add SQLite/PostgreSQL/SQL Server bridge logic
  directly to `engine_v8.cc`.
- `engine_v8_internal.h` is private to this directory. It exposes the V8 backend shape and
  resource table to sibling intrinsic modules without making V8 a public runtime type.
- No Node compatibility, package-manager behavior, inspector, snapshots, or raw native
  handle exposure belongs in this directory. Missing feature paths must report stable
  diagnostics instead of fake-success placeholders.
- Process-wide V8 platform state is initialized once and intentionally kept alive for the
  process lifetime until a future explicit runtime shutdown task decides disposal policy.
- `sl_engine_destroy` releases per-engine isolate/context state only.

Expected SDK layout:

```text
<SLOPPY_V8_ROOT>/
  include/v8.h
  include/libplatform/libplatform.h
  lib/v8.lib or lib/v8_monolith*.lib
  lib/v8_libplatform*.lib
  lib/v8_libbase*.lib
  bin/  # optional runtime DLLs for dynamic SDKs
```
