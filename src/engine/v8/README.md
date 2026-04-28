# V8 Bridge

This directory is reserved for the isolated C++ V8 bridge.

TASK 07.A adds only SDK detection and build-option plumbing. It does not initialize V8,
include V8 headers, call V8 APIs, load JavaScript, or expose a runtime bridge.

Rules:

- V8 integration is not implemented in the foundation phase.
- The default build leaves the V8 bridge disabled.
- Enable SDK validation explicitly with `-DSLOPPY_ENABLE_V8=ON` or `-DSLOPPY_ENGINE=v8`.
- `SLOPPY_V8_ROOT` must point to the documented prebuilt SDK layout when V8 is enabled.
- C++ is allowed here only for engine binding work.
- `v8::*` types must never leak outside this directory.
- V8 headers may appear only under this directory when bridge implementation tasks begin.
- JS code must never receive raw C pointers.
- Native handles exposed to JS must flow through resource IDs with generation checks.

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
