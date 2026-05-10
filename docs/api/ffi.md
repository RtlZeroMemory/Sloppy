# FFI API

Use `sloppy/ffi` for typed, Plan-visible calls into trusted C ABI libraries.
This API is experimental and unsafe.

```ts
import { unsafeFfi as ffi, t } from "sloppy/ffi";
```

Common shape:

```ts
const native = ffi.library("sloppy_ffi_test", {
  addI32: ffi.fn(t.i32, [t.i32, t.i32], {
    symbol: "sloppy_ffi_add_i32",
  }),
});
```

The compiler extracts static declarations into the Plan. The runtime resolves
libraries and symbols at startup and calls through libffi.

`t.bool` is C `_Bool` / one-byte `bool`. For WinAPI `BOOL`, use `t.bool32` or
`t.i32`.

See the full [Native FFI reference](../reference/ffi.md) for types, marshaling,
packaging, diagnostics, and limits.
