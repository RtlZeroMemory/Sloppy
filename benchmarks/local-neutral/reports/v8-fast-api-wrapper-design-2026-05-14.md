# V8 Fast API Request Wrapper Design Spike - 2026-05-14

Local engineering note only. This is not a public benchmark claim.

## SDK Finding

The pinned Windows V8 SDK under `V:\Slop\.sdeps\v8\windows-x64` includes
`v8-fast-api-calls.h`, `v8::CFunction::Make`,
`v8::CFunctionBuilder`, `v8::FastApiCallbackOptions`,
`v8::FunctionTemplate::New` with a `const CFunction*` parameter, and
`v8::FunctionTemplate::NewWithCFunctionOverloads`.

The header comments mention `CFunction::MakeWithFallbackSupport` and
`options.fallback`, but this pinned header does not expose either API. The
available `FastApiCallbackOptions` only has `isolate` and `data`. That means
Fast API callbacks in this SDK cannot request the modern explicit slow fallback
path from inside the fast callback.

## Native Context Wrapper

Current HTTP contexts are plain `v8::Object::New(isolate)` values. Route params
are copied into a normal JS `ctx.route` object. A safe Fast API route accessor
needs a distinct internal wrapper:

```text
HttpRequestContextWrapper
  internal field 0: SlHttpRequestContext*
  internal field 1: uint64_t request_generation
  internal field 2: SlEngine* or lightweight owner token
```

The wrapper should be created per request and kept private to the runtime. The
public JS `ctx` facade can either hold it in a private slot or inherit from a
template whose methods are backed by the wrapper. The wrapper must never outlive
the request arena. A generation or owner token lets slow callbacks reject stale
or cross-request wrapper use without dereferencing freed memory.

## Route Param Fast Call Shape

The first useful target remains integer route params:

```text
__sloppyFast.routeInt(wrapper, slot)
```

The slow callback validates the receiver/wrapper, slot bounds, route shape, and
integer parse semantics, then returns the same value or the existing rich
fallback behavior. The fast callback may only:

1. Read the internal `SlHttpRequestContext*`.
2. Check the generation token and slot bounds.
3. Read a precomputed route-param slot.
4. Return an `int32_t`.

It must not allocate, throw, execute JS, build strings, or mutate the request.

## Missing Fallback Support Impact

Because this SDK shape lacks `options.fallback`, a fast callback cannot safely
discover an unsupported route shape and then ask V8 to run the slow callback.
The safe options are:

- Only install/register the fast function for route shapes proven at wrapper
  construction time.
- Encode unsupported states so the fast callback returns a sentinel that the JS
  wrapper checks and routes into the slow path.
- Do not expose Fast API for public `ctx.route` until the native wrapper and
  generated call sites can guarantee monomorphic, validated shapes.

The first option is the cleanest target for generated typed routes like
`/users/{id:int}` because the Plan already knows the slot and constraint.

## Compile Probe

`tests/unit/engine/test_v8_fast_api_probe.cc` is a compile-only probe for the
pinned API surface. It creates a `v8::CFunction` for a primitive route-int-like
callback and verifies both `FunctionTemplate::New(..., &fast)` and
`NewWithCFunctionOverloads(...)` compile in the repo V8 CMake lane. It is not
registered as a runtime test and is not wired into production request handling.

## Next Safe Runtime Experiment

1. Add an internal `HttpRequestContextWrapper` template with one aligned
   pointer field and one generation field.
2. Store the wrapper on `ctx` through a private symbol, not public API.
3. Generate a hidden typed-route helper only for validated integer route slots.
4. Count `fast_route_int_calls`, `slow_route_int_calls`, and sentinel slow-path
   fallbacks.
5. Keep the experiment only if `route-param` or `mixed-realistic` improves and
   contracts pass with Fast API disabled or unsupported.
