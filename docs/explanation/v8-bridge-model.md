# V8 Bridge Model

V8 is an optional backend behind a strict boundary, not the runtime's public
type system.

`src/engine/v8/*` owns V8-specific code. Core C modules and public headers stay
on Sloppy-owned types.

Current bridge behavior emphasizes ownership and deterministic failure:

- isolate ownership is bound to one engine owner thread;
- wrong-thread entry fails before mutable V8 work;
- generated handlers are registered explicitly
  (`__sloppy_register_handler` in `engine_v8.cc`);
- request dispatch targets registered handler IDs;
- Promise/microtask handling is bounded and owner-thread controlled;
- exceptions and rejections are converted into Sloppy diagnostics.

Provider and core intrinsics are also installed through explicit bridge modules,
not by exposing native pointers to JavaScript.

Building artifacts does not require V8. Running handlers does. The default
non-V8 build validates native/runtime contracts, while V8-enabled checks
exercise JavaScript execution and bridge behavior.
