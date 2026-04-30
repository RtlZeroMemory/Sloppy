# Unsupported Behavior Conformance

Default compiler conformance cases:

| Case | Source fixture | Expected behavior |
| --- | --- | --- |
| Dynamic route registration | `compiler/tests/fixtures/unsupported-dynamic-route/input.js` | Build fails with `SLOPPYC_E_UNSUPPORTED_DYNAMIC_ROUTE_PATTERN` and leaves no success artifacts. |
| Arbitrary bare import | `compiler/tests/fixtures/unsupported-import-specifier/input.js` | Build fails with `SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER` and leaves no success artifacts. |
| Unsupported async handler body | `compiler/tests/fixtures/unsupported-async-handler-body/input.js` | Build fails with `SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY` and leaves no success artifacts. |
| Secret-bearing capability metadata | `compiler/tests/fixtures/unsupported-secret-capability/input.js` | Build fails with `SLOPPYC_E_SECRET_PLAN_METADATA` and leaves no success artifacts. |

Default runtime/process conformance cases already registered under `sloppy.run.*`:

- source-input `sloppy run examples/compiler-hello/app.js` reports source-input handoff as
  deferred;
- missing artifact directory reports a missing artifact path;
- missing `app.plan.json` reports the missing plan;
- malformed `app.plan.json` reports malformed plan input;
- missing or drifted artifact hashes fail before serving;
- non-V8 `sloppy run --artifacts ... --once` reports that V8 is required.

V8-gated unsupported cases:

- unsupported methods return `405 Method Not Allowed`;
- invalid result descriptors return a safe `500 Internal Server Error`;
- missing or duplicate handler registrations fail during startup validation.

Unsupported request body behavior is covered by HTTP request parser and dispatch tests.
Malformed JSON, oversized bodies, unsupported content types, and unsupported transfer
framing fail before handler entry. Socket-mode conformance still needs a real body-bearing
request fixture.
