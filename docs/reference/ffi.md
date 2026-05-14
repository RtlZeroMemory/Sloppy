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

Use `ffi.adopt(...)` when the C library documentation says a returned or
out-param pointer is an opaque handle with a known ownership policy. Adoption is
unsafe: Sloppy can type the JavaScript wrapper, but it cannot prove the pointer
really belongs to that handle type or that the disposer matches the allocator.

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

## Pointer Adoption

Some C APIs return opaque pointers as `void*` or write them through `T**` /
`void**` out parameters. Sloppy surfaces those values as opaque `NativePointer`
objects first. Convert them to typed handles explicitly:

```ts
const Counter = ffi.handle("Counter");

const native = ffi.library("counter", {
  createCounter: ffi.fn(t.ptr, [t.i32], { symbol: "counter_create" }),
  createCounterOut: ffi.fn(t.i32, [t.i32, t.ptr], { symbol: "counter_create_out" }),
  destroyCounter: ffi.fn(t.void, [Counter], { symbol: "counter_destroy" }),
  counterValue: ffi.fn(t.i32, [Counter], { symbol: "counter_value" }),
});

const counter = ffi.adopt(Counter.owned, native.createCounter(1), {
  dispose: native.destroyCounter,
});
try {
  console.log(native.counterValue(counter));
} finally {
  counter.dispose();
}
```

Owned adoption requires a non-null `NativePointer` and a static disposer
function. The resulting handle is typed, rejects use-after-dispose before
native calls, and calls the disposer exactly once. Calling `.dispose()` again is
a deterministic no-op.

Borrowed adoption creates a typed view and never disposes the native resource:

```ts
const borrowed = ffi.adopt(Counter, native.getSharedCounter());
console.log(native.counterValue(borrowed));
```

Borrowed `.dispose()` is a no-op so generic cleanup paths can accept either
borrowed or owned handles. Do not use borrowed adoption to dodge ownership. If
the native API says the caller owns the pointer, adopt it as owned or destroy it
manually.

When an API returns a caller-owned pointer but the binding cannot use owned
adoption, keep the manual destroy path explicit:

```ts
const ptr = native.createCounter(2);
const borrowed = ffi.adopt(Counter, ptr);
try {
  console.log(native.counterValue(borrowed));
} finally {
  native.destroyCounter(borrowed);
}
```

Out-param adoption uses the same rule:

```ts
const outCounter = ffi.out(t.ptr);
try {
  const status = native.createCounterOut(3, outCounter.ptr);
  if (status !== 0) throw new Error(`counter_create_out failed: ${status}`);
  const counter = ffi.adopt(Counter.owned, outCounter.value, {
    dispose: native.destroyCounter,
  });
  try {
    console.log(native.counterValue(counter));
  } finally {
    counter.dispose();
  }
} finally {
  outCounter.dispose();
}
```

Null owned adoption fails with `SLOPPY_E_FFI_NULL_HANDLE`. Passing anything
other than a valid `NativePointer` fails with
`SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE`. Missing owned disposers fail with
`SLOPPY_E_FFI_MISSING_DISPOSER`. Passing an adopted handle to a function that
declares another handle type fails with `SLOPPY_E_FFI_HANDLE_TYPE_MISMATCH`.

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

`ffi.out(type)` and `ffi.inout(type, initial)` are small readability helpers
over `ffi.ref`. They allocate the same opaque Sloppy-owned native cell, expose
the same `.value`, `.ptr`, and `dispose()` members, and never expose raw native
addresses. Use `out` when the C API writes the first value and `inout` when the
C API reads an initial value and writes a replacement.

```ts
const written = ffi.out(t.usize);
const flags = ffi.inout(t.u32, 0);
try {
  const status = native.readConfig(buffer, buffer.byteLength, written.ptr, flags.ptr);
  if (status !== 0) throw new Error(`native readConfig failed: ${status}`);
  console.log(written.value, flags.value);
} finally {
  written.dispose();
  flags.dispose();
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

## Binding A C Library Safely

Prefer C APIs that make ownership and buffer lengths explicit:

- create opaque resources through `int create(T** out)` or a typed owned handle
  factory paired with `void destroy(T*)`;
- return success or failure through an integer status code;
- pass text input as `t.cstring` only when embedded NUL is invalid;
- pass caller-owned output buffers as `t.mutBytes` plus a `t.usize` length;
- report output length through `ffi.out(t.usize)`;
- report primitive out parameters through `ffi.out(t.i32)`, `ffi.out(t.u32)`,
  or another fixed-size type;
- pass structs by pointer with `Struct.alloc(...)`, never by value;
- keep callbacks synchronous and runtime-thread-owned.

Example:

```ts
const Config = ffi.handle("Config");
const native = ffi.library("native_config", {
  create: ffi.fn(t.i32, [t.ptr], { symbol: "native_config_create" }),
  destroy: ffi.fn(t.void, [Config], { symbol: "native_config_destroy" }),
  readName: ffi.fn(t.i32, [Config, t.mutBytes, t.usize, t.ptr], {
    symbol: "native_config_read_name",
  }),
});

const handleOut = ffi.out(t.ptr);
const name = ffi.cstringBuffer(256);
const written = ffi.out(t.usize);
let handle = null;

try {
  if (native.create(handleOut.ptr) !== 0) throw new Error("create failed");
  handle = ffi.adopt(Config.owned, handleOut.value, { dispose: native.destroy });
  const status = native.readName(handle, name.ptr, name.byteLength, written.ptr);
  if (status !== 0) throw new Error(`readName failed: ${status}`);
  console.log(name.readString(), written.value);
} finally {
  if (handle !== null) handle.dispose();
  handleOut.dispose();
  name.dispose();
  written.dispose();
}
```

For APIs that return handles directly, prefer an owned handle descriptor with a
static disposer so review can see the lifetime:

```ts
const Counter = ffi.handle("Counter");
const native = ffi.library("counter", {
  createCounter: ffi.fn(Counter.owned, [t.i32], { dispose: "destroyCounter" }),
  destroyCounter: ffi.fn(t.void, [Counter]),
});

ffi.using(native.createCounter(1), (counter) => {
  // use counter
});
```

If a C API reports truncation, keep it explicit. A common pattern is `0` for
success and `1` for truncation while always writing the required length to a
`size_t*` out parameter.

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
`native.ffiDispatchTables`. Statically visible pointer adoption calls are
emitted under `native.ffiAdoptions` with the handle type, ownership mode,
owned-disposer function when present, and source location.

`sloppy doctor`, `sloppy audit`, and `sloppy capabilities` report the FFI
feature requirement, library IDs, packaged native paths and SHA-256 state when
available, symbols, conventions, structs, handles, callbacks, and dispatch
tables, and pointer adoptions. The output is metadata only and does not expose
raw native addresses.

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
- implicit ownership/freeing of arbitrary returned pointers
- async/off-thread FFI calls
- foreign-thread callback entry into JavaScript
- native library download or remote loading
- graphics bindings
