# FFI Contracts

Run the FFI contract area with:

```sh
node tests/contracts/runner/contract-runner.mjs --area ffi --tier pr
```

The area checks that the unsafe native interop surface remains typed,
Plan-visible, package-aware, scoped to C ABI calls, and explicit about
ownership and unsupported behavior.
