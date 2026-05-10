# Native FFI Internals

The FFI implementation has four layers:

- compiler extraction in `compiler/src/sloppyc.rs`;
- Plan parsing and interned metadata in `src/core/plan_parse.c` and
  `src/core/plan.c`;
- dynamic library and libffi preparation in `src/runtime/ffi/ffi_registry.c`;
- V8 binding and marshaling in `src/engine/v8/intrinsics_ffi.cc`.

The public JavaScript surface lives in `stdlib/sloppy/ffi.js` and is bundled
into `stdlib/sloppy/internal/runtime-classic.js`.

## Startup

When `stdlib.ffi` is active, V8 startup initializes an `SlFfiRegistry` from the
Plan before evaluating the generated bundle. The registry opens each library
once, resolves every symbol once, prepares a libffi `ffi_cif` once per function,
and keeps those descriptors for the engine lifetime.

Package runs can pass `SlFfiLibraryOverride` entries through `SlEngineOptions`.
The override maps a Plan-visible library ID to the copied package path recorded
in `manifest.json`. Unmapped libraries use platform loader resolution.

## Call Path

JavaScript calls `unsafeFfi.library(...)` once to bind a frozen object of
callable functions. The bridge checks that the JavaScript descriptors match the
Plan metadata and then attaches each function wrapper directly to the cached
native descriptor.

Hot calls validate argument count, range-check and marshal JavaScript values,
call `ffi_call`, and convert supported return types. Type descriptors and
symbols are not parsed or resolved on every call.

## Ownership

Refs, buffers, C string buffers, UTF-16 buffers, and struct instances are
bridge-owned resources stored behind V8 private `External` values. JavaScript
sees opaque objects, not native addresses. Passing a resource as `ptr`,
`bytes`, or `mutBytes` uses the resource's owned byte storage for the
synchronous call.

Returned raw non-null pointers are not surfaced as usable JavaScript values in
v1. Future pointer ownership work must keep the same opaque-handle boundary.

## Limits

The registry supports C ABI calls through libffi. It does not support callbacks,
variadic functions, C++ ABI calls, struct-by-value, automatic pointer ownership,
or off-thread FFI execution. Long-running native calls block the V8 owner thread.
