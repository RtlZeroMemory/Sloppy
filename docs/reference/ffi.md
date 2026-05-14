# Native FFI Reference

Sloppy FFI is an experimental, unsafe native interop surface. It is closer to
.NET P/Invoke than to dynamic "call anything" reflection: native imports are
declared up front, typed, marshaled explicitly, cached by the runtime, and
visible in the Plan.

Use it only with trusted C ABI libraries. C ABI only: Sloppy does not support
C++ ABI calls or variadic functions. A wrong signature can crash the
process. Sloppy validates declarations and JavaScript values, but it cannot
prove the native function actually has the signature you wrote.

## Import

```ts
import { unsafeFfi as ffi, t } from "sloppy/ffi";
```

The `unsafeFfi` name is intentional. It keeps the unsafe boundary visible at
the call site and in code review.

## Types

`t` exports frozen type descriptors:

- integers: `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, `u64`, `isize`, `usize`
- scalars: `void`, `bool`, `bool32`, `f32`, `f64`
- pointers and handles: `ptr`, `handle`, `hwnd`, `hmodule`
- strings and bytes: `cstring`, `lpcstr`, `utf16`, `lpcwstr`, `bytes`, `mutBytes`
- Windows readability alias: `ntstatus`

Aliases normalize to their ABI type: `handle`, `hwnd`, and `hmodule` are
pointer-sized handles; `bool32` and `ntstatus` are `i32`; `lpcstr` is
`cstring`; `lpcwstr` is `utf16`.

## Library Declarations

```ts
const native = ffi.library("sloppy_ffi_test", {
  addI32: ffi.fn(t.i32, [t.i32, t.i32], {
    symbol: "sloppy_ffi_add_i32",
    convention: "system",
  }),
});
```

`ffi.library(name, functions, options?)` declares one native library. `name` is
the Plan-visible library ID and, unless package metadata overrides it, the
platform loader name/path.

`ffi.fn(returnType, parameterTypes, options?)` declares one C ABI function.
`options.symbol` defaults to the JavaScript property name. `options.convention`
overrides the library convention.

Supported conventions:

| Convention | Behavior |
| --- | --- |
| `system` | platform default C ABI |
| `cdecl` | C ABI |
| `stdcall` | Windows-only stdcall; non-Windows runtimes reject it |

Declarations must be static top-level calls. Dynamic names, generated
descriptor objects, callback descriptors, variadic functions, C++ ABI calls,
and struct-by-value signatures are rejected.

## Marshaling

Integer arguments are range checked. `i64` and `u64` require `BigInt`; smaller
integer types use finite integer numbers. `isize` and `usize` accept safe
integer numbers or `BigInt` values in range. `f32` and `f64` use JavaScript
numbers. `bool` uses JavaScript booleans and maps to C `_Bool` / one-byte
`bool`. Do not use `t.bool` for WinAPI `BOOL`; use `t.bool32` or `t.i32`.

`cstring` converts a JavaScript string to a NUL-terminated UTF-8 temporary for
the duration of the call. `utf16` does the same with UTF-16 code units.
Embedded NUL is rejected.

`bytes` and `mutBytes` accept `Uint8Array` and pass its backing storage for the
synchronous call duration. Native writes through `mutBytes` are visible in the
same `Uint8Array` after return.

Pointer-like parameters accept `null` or Sloppy-owned FFI resources such as
refs, buffers, C string buffers, UTF-16 buffers, and struct instances. Raw
native addresses are not exposed as JavaScript numbers.

Returned non-null `ptr` values are borrowed `NativePointer` objects with
`isNull()`. They can be passed back to `ptr` parameters, but they do not expose
arithmetic, numeric addresses, or `dispose()` because ownership is unknown.
Returned `cstring`, `utf16`, `bytes`, and `mutBytes` are rejected. Return
strings or buffers through caller-owned out parameters instead.

## Owned Handles And Scoped Disposal

Owned handles make pointer ownership explicit without exposing raw addresses:

```ts
const Counter = ffi.handle("Counter");

const native = ffi.library("sloppy_ffi_test", {
  createCounter: ffi.fn(Counter.owned, [t.i32], {
    symbol: "sloppy_ffi_counter_create",
    dispose: "destroyCounter",
  }),
  addCounter: ffi.fn(t.i32, [Counter, t.i32], {
    symbol: "sloppy_ffi_counter_add",
  }),
  counterValue: ffi.fn(t.i32, [Counter], {
    symbol: "sloppy_ffi_counter_value",
  }),
  destroyCounter: ffi.fn(t.void, [Counter], {
    symbol: "sloppy_ffi_counter_destroy",
  }),
});
```

`unsafeFfi remains unsafe`: the handle type records the review contract and the
Plan metadata, not a runtime proof that the native pointer really has that C
type. Owned handles expose `.ptr` for interop, `.dispose()` for explicit
cleanup, and a `disposed` state. Use-after-dispose is rejected before Sloppy
calls native code. Double dispose is a deterministic no-op.

Use scoped disposal for short lifetimes:

```ts
ffi.using(native.createCounter(1), (counter) => {
  native.addCounter(counter, 2);
  return native.counterValue(counter);
});
```

Scoped disposal is the preferred pattern for owned native resources whose
lifetime fits in one synchronous or asynchronous operation.

`ffi.using(resource, callback)` disposes on success and on throw. If the
callback returns a promise, disposal happens after settlement.

## Refs, Buffers, And Structs

`ffi.ref(type, initial?)` allocates one native cell. Use `.value` to read/write
the cell, `.ptr` to pass it as a pointer, and `dispose()` when done.

```ts
const value = ffi.ref(t.u32, 0);
try {
  native.writeU32(value.ptr);
  console.log(value.value);
} finally {
  value.dispose();
}
```

`ffi.buffer(byteLength)`, `ffi.cstringBuffer(textOrByteLength)`, and
`ffi.utf16Buffer(textOrCodeUnits)` allocate owned native buffers. Buffers expose
`read()`, `write(bytes, offset?)`, `.ptr`, and `dispose()`. String buffers also
expose `readString()` and `writeString(text)`.

`ffi.struct(name, fields, options?)` supports pointer-based sequential layouts:

```ts
const Point = ffi.struct("Point", { x: t.i32, y: t.i32 }, { layout: "sequential", pack: 4 });
const point = Point.alloc({ x: 10, y: 20 });
try {
  console.log(native.pointSum(point.ptr));
} finally {
  point.dispose();
}
```

Struct fields support fixed-size primitive and pointer-like field types. Fixed
arrays and nested structs are supported for pointer-based layouts:

```ts
const Matrix = ffi.struct("Matrix", {
  values: ffi.array(t.f32, 16),
});

const Rect = ffi.struct("Rect", {
  origin: Point,
  size: Point,
  flags: t.u32,
});
```

Fixed arrays reserve contiguous inline storage. Nested structs preserve the
nested layout alignment when placed inline. Fixed
arrays and nested structs are inline layout fields. They do not enable
struct-by-value parameters or returns.

## Callbacks

Callbacks allow synchronous native-to-JavaScript calls when native code invokes
the callback on the same Sloppy runtime thread:

```ts
const callback = ffi.callback(t.i32, [t.i32], (value) => {
  return value + 1;
}, { thread: "runtime" });
```

Callbacks are explicit-lifetime resources. Pass them to `ptr` callback
parameters and call `.dispose()` when the native side must stop invoking them.
Callback return values are marshaled and range checked. The current callback
surface supports `void`, `i32`, and `u32` returns, and `i32` and `u32`
parameters. Pointer callback parameters and pointer callback returns are rejected
for now because raw callback pointer arguments need stricter ownership rules.
JavaScript exceptions do not cross the C ABI; Sloppy returns the documented
zero/default value for the callback return type and keeps the process alive.

Foreign-thread callback entry is still unsupported. Native code must not call
Sloppy callbacks after runtime shutdown or from arbitrary worker threads. The
native bridge checks the callback owner thread before entering V8 and returns a
default value instead of touching the isolate from a foreign thread.

## Dispatch Tables

Dispatch tables support declaration-first function pointer resolution through a
typed resolver. They are for loader-style C APIs while still avoiding raw public
`GetProcAddress` or `dlsym` from JavaScript:

```ts
const native = ffi.library("sloppy_ffi_test", {
  resolveSymbol: ffi.fn(t.ptr, [t.cstring], {
    symbol: "sloppy_ffi_resolve_symbol",
  }),
});

const dispatch = ffi.dispatchTable("ffiDispatch", {
  resolver: native.resolveSymbol,
  symbols: {
    addI32: ffi.fn(t.i32, [t.i32, t.i32], {
      symbol: "sloppy_ffi_add_i32",
    }),
  },
});
```

Symbol names and signatures are static and Plan-visible. Missing symbols fail
with an FFI error. Function pointer addresses are cached internally and are not
serialized or exposed as numbers.

## Plan Metadata

The compiler emits `requiredFeatures: ["stdlib.ffi"]`, `capabilities` entries
for `ffi/use`, and `native.ffi` metadata with library names, symbols, return
types, parameter types, conventions, and source locations. Struct layouts are
emitted under `native.ffiStructs`; handles, callbacks, and dispatch tables are
emitted under `native.ffiHandles`, `native.ffiCallbacks`, and
`native.ffiDispatchTables`.

The runtime resolves each library once, resolves each symbol once, prepares one
libffi call interface per function, and then reuses those descriptors for calls.

## Packaging

System libraries use the platform dynamic loader behavior. Local native
libraries can be mapped in `sloppy.json`:

```json
{
  "entry": "src/main.ts",
  "ffiLibraries": {
    "myhash": {
      "windows-x64": "native/windows-x64/myhash.dll",
      "linux-x64": "native/linux-x64/libmyhash.so",
      "macos-arm64": "native/macos-arm64/libmyhash.dylib"
    }
  }
}
```

`sloppy package` copies the selected local native library into
`artifacts/native/`, records its package path and SHA-256 hash in
`manifest.json`, and `sloppy run <package>` resolves the Plan library ID to the
packaged path after verifying the hash.

## Contracts

The FFI contract area validates the unsafe boundary as artifact semantics, not
format trivia. It checks declaration visibility, package-safe native library
paths and hashes, struct size/offset metadata, fixed arrays, nested structs,
refs and buffers, embedded-NUL rejection, BigInt requirements for 64-bit
integers, no raw pointer address exposure, owned handle disposal, synchronous
callbacks, dispatch resolution, and redacted diagnostics.

## Not Supported

These surfaces are still unsupported:

- Node native addons and N-API
- raw public `GetProcAddress` / `dlsym` calls
- variadic functions
- C++ ABI calls
- struct-by-value params or returns
- automatic ownership/freeing of arbitrary returned pointers
- async/off-thread FFI calls
- foreign-thread callback entry into JavaScript
- native library download or remote loading
