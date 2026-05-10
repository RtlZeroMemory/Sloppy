# Native FFI

This guide shows the smallest useful FFI flow: declare a C ABI function, build
Plan-visible metadata, and package a local native library.

## Declare A Library

```ts
import { unsafeFfi as ffi, t } from "sloppy/ffi";

const native = ffi.library("sloppy_ffi_test", {
  addI32: ffi.fn(t.i32, [t.i32, t.i32], {
    symbol: "sloppy_ffi_add_i32",
  }),
});

export function main() {
  console.log(native.addI32(2, 3));
}
```

The declaration must stay static and top-level. The compiler records the
library, symbol, parameter types, return type, convention, and source location
in `app.plan.json`.

## Pass Strings And Bytes

```ts
const native = ffi.library("sloppy_ffi_test", {
  strlen: ffi.fn(t.usize, [t.cstring], { symbol: "sloppy_ffi_strlen" }),
  sumBytes: ffi.fn(t.u32, [t.bytes, t.usize], { symbol: "sloppy_ffi_sum_bytes" }),
});

const data = new Uint8Array([1, 2, 3]);
console.log(native.strlen("hello"));
console.log(native.sumBytes(data, data.byteLength));
```

Strings are temporary NUL-terminated buffers valid for the call duration.
`Uint8Array` storage is passed directly for the synchronous call.

## Use Out Parameters

```ts
const native = ffi.library("sloppy_ffi_test", {
  writeU32: ffi.fn(t.void, [t.ptr], { symbol: "sloppy_ffi_write_u32" }),
});

const value = ffi.ref(t.u32, 0);
try {
  native.writeU32(value.ptr);
  console.log(value.value);
} finally {
  value.dispose();
}
```

Refs and buffers are Sloppy-owned native resources. Dispose them when the native
call is done.

## Package Local Native Libraries

Use a `sloppy.json` mapping when the library is part of the project:

```json
{
  "entry": "src/main.ts",
  "kind": "program",
  "ffiLibraries": {
    "sloppy_ffi_test": {
      "windows-x64": "native/windows-x64/sloppy_ffi_test.dll",
      "linux-x64": "native/linux-x64/libsloppy_ffi_test.so"
    }
  }
}
```

`sloppy package` copies the selected file, records a SHA-256 hash, and package
runs resolve the Plan library ID to that copied file. System libraries do not
need an `ffiLibraries` mapping.

## Safety Boundary

FFI is unsafe. Wrong signatures, invalid pointers, or native bugs can crash the
process. Sloppy validates JavaScript values and declaration shapes, but it does
not prove that the native function matches the declaration.
