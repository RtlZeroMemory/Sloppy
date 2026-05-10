# Native FFI Reference

Sloppy FFI is an experimental, unsafe native interop surface. It is closer to
.NET P/Invoke than to dynamic "call anything" reflection: native imports are
declared up front, typed, marshaled explicitly, cached by the runtime, and
visible in the Plan.

Use it only with trusted C ABI libraries. A wrong signature can crash the
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
- scalars: `void`, `bool`, `f32`, `f64`
- pointers and handles: `ptr`, `handle`, `hwnd`, `hmodule`
- strings and bytes: `cstring`, `lpcstr`, `utf16`, `lpcwstr`, `bytes`, `mutBytes`
- Windows readability alias: `ntstatus`

Aliases normalize to their ABI type: `handle`, `hwnd`, and `hmodule` are
pointer-sized handles; `ntstatus` is `i32`; `lpcstr` is `cstring`; `lpcwstr`
is `utf16`.

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
numbers. `bool` uses JavaScript booleans.

`cstring` converts a JavaScript string to a NUL-terminated UTF-8 temporary for
the duration of the call. `utf16` does the same with UTF-16 code units.
Embedded NUL is rejected.

`bytes` and `mutBytes` accept `Uint8Array` and pass its backing storage for the
synchronous call duration. Native writes through `mutBytes` are visible in the
same `Uint8Array` after return.

Pointer-like parameters accept `null` or Sloppy-owned FFI resources such as
refs, buffers, C string buffers, UTF-16 buffers, and struct instances. Raw
native addresses are not exposed as JavaScript numbers.

Returned `cstring`, `utf16`, `bytes`, and `mutBytes` are rejected in v1. Return
strings or buffers through caller-owned out parameters instead.

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

Struct fields support fixed-size primitive and pointer-like field types. Unions,
bitfields, nested struct-by-value fields, struct-by-value params/returns, and
callbacks are not supported.

## Plan Metadata

The compiler emits `requiredFeatures: ["stdlib.ffi"]`, `capabilities` entries
for `ffi/use`, and `native.ffi` metadata with library names, symbols, return
types, parameter types, conventions, and source locations. Struct layouts are
emitted under `native.ffiStructs`.

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

## Not Supported

- Node native addons and N-API
- raw public `GetProcAddress` / `dlsym` calls
- callbacks from native code into JavaScript
- variadic functions
- C++ ABI calls
- struct-by-value params or returns
- automatic ownership/freeing of arbitrary returned pointers
- async/off-thread FFI calls
- native library download or remote loading
