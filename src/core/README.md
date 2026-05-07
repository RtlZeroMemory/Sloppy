# Runtime Core

The C runtime core owns portable foundation code that other runtime modules can depend on.

Current core capabilities include:

- status and source-location primitives;
- borrowed string and byte views;
- checked arithmetic helpers;
- internal assertions;
- caller-backed arenas with marks, reset, and high-water accounting;
- bounded byte/string builders;
- interned app/static metadata;
- generation-counted resource tables;
- cancellation/deadline/shutdown/backpressure snapshots;
- loop/completion primitives and async ownership helpers;
- inline worker-pool foundation;
- complete-buffer HTTP request parsing, route matching, dispatch, response writing, and
  backend state helpers;
- Plan parsing and validation helpers;
- diagnostics, source spans, related spans, hints, text/JSON rendering, and redaction;
- app-host lifecycle, runtime feature, capability, filesystem, OS, crypto, network, HTTP,
  provider-executor, async backend, worker-pool, and resource/leak foundations where their
  ownership is documented under `docs/modules/` or `docs/project/`.

Core modules must stay bounded. Ownership, tests, diagnostics, and public headers must be
specified before adding behavior. Test-only hooks must remain narrow and must not be
presented as public API.
