# V8 Bridge

This directory owns the isolated C++ V8 bridge.

TASK 07.A adds only SDK detection and build-option plumbing. TASK 07.B adds the
engine-neutral C ABI outside this directory plus a noop engine stub. TASK 07.C adds the
first opt-in smoke bridge: initialize V8, create one isolate/context, evaluate classic
JavaScript source, call a named global zero-argument function, and copy string results back
to C.

Rules:

- The default build leaves the V8 bridge disabled.
- Enable SDK validation explicitly with `-DSLOPPY_ENABLE_V8=ON` or `-DSLOPPY_ENGINE=v8`.
- `SLOPPY_V8_ROOT` must point to the documented prebuilt SDK layout when V8 is enabled.
- C++ is allowed here only for engine binding work.
- `v8::*` types must never leak outside this directory.
- V8 headers may appear only under this directory.
- JS code must never receive raw C pointers.
- Native handles exposed to JS must flow through resource IDs with generation checks.
- No ESM/module resolver, Sloppy Plan handler execution, event loop, workers, inspector,
  snapshots, Node compatibility, or package-manager behavior belongs in TASK 07.C.
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
