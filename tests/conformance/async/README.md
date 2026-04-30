# Async Conformance

Status: ENGINE-19.BC executable lane registration.

This lane protects the async behavior that exists today. It keeps native async settlement,
native backend completion transport, and V8 owner-thread Promise continuation evidence
separate.

## Default Non-V8 Cases

`conformance.async.native_settlement` runs the existing `core.async.settlement` executable.
It proves the native `SlAsync` skeleton semantics: pending initialization, fulfill, reject,
cancel, loop-post failure leaving the async record pending, continuation failure
propagation, rejected reinitialization before drain, FIFO dispatch, and exactly-one
settlement.

`conformance.async.backend_completion` runs the existing `core.async.backend` executable.
It proves `SlAsyncLoop` deterministic backend behavior: bounded completion posting,
capacity overflow without ownership transfer, retain/release hooks, cleanup once,
dispatch failure cleanup, invalid arguments, and dispose rejection.

`conformance.async.backend_libuv` runs when the C++/libuv unit target exists. It proves the
internal libuv-backed native completion transport, including cross-thread post and
owner-thread dispatch, without exposing libuv as a public API.

These are default non-V8 evidence. They do not prove V8 Promise settlement.

## V8-Gated Cases

`conformance.v8.native_async_scheduler` is registered only when `SLOPPY_ENABLE_V8=ON`.
It proves native completions settle or reject V8 Promises only through the owner-thread
scheduler, wrong-thread drain fails before entering V8, failed posts do not mark
continuations active, and scope cleanup still runs once.

`conformance.v8.runtime_bridge` and `conformance.async_handler.run_once` cover returned
Promise fulfillment, rejection, pending/deadline diagnostics, bounded microtask behavior,
and direct async handler execution through `sloppy run --artifacts --once`.

When the V8 SDK is missing, these V8-gated cases are skipped/not configured, not passed.
