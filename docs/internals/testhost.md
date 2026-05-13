# TestHost Internals

`stdlib/sloppy/testing.js` owns the first-party JavaScript TestHost API.
The API is experimental while Sloppy is pre-alpha; keep behavior under test and
avoid treating helper shape as stable runtime surface.

## Modes

`TestHost.create(app)` wraps the existing app-host dispatcher. It freezes the
app, snapshots routes by Sloppy route precedence, materializes request context
objects, opens a request service scope, invokes route middleware/handlers, and
normalizes `Results.*` descriptors into immutable response objects.

`TestHost.fromArtifacts(path)` and `TestHost.fromPackage(path)` use the runtime
CLI as the process boundary. In one-off CLI mode, each request becomes:

```text
RequestBuilder.send()
  -> withRequestBodyFile()
  -> sloppy run <path> --once METHOD TARGET
  -> parseHttpResponseBytes()
  -> TestResponse assertion helpers
```

Loopback mode starts:

```text
reserveLoopbackPort()
  -> sloppy run <path> --host 127.0.0.1 --port <reserved-port>
  -> waitForLoopbackReady()
  -> HttpClient.request()
```

The host then sends HTTP/1.1 requests with Sloppy's `HttpClient` and stops the
child process through Sloppy's process API during disposal.

App-host helpers are local to each host:

- `ctx.config` is wrapped with per-host config and secret overrides.
- `ctx.services` is wrapped with per-host service and provider overrides.
- `host.diagnostics` records request, routing, body, and ProblemDetails events
  with secret-field redaction.
- `host.metrics` records request counters by method and status.
- `host.health` asserts mapped health routes by making TestHost requests.
- `host.openapi` builds an app-host route snapshot or runs `sloppy openapi`
  through Sloppy's process API for artifact/package hosts.
- `host.jobs` is an explicit test hook object; when no scheduler integration is
  supplied it stores in-memory job assertions.

## Cleanup

The app-host mode waits for active requests before disposing the root service
provider. Native one-off CLI mode deletes temporary body directories in a
`finally` block. Loopback mode sends `SIGINT` to the child server and waits for
exit.

## Boundaries

The app-host mode intentionally does not pretend to validate runtime-only
contracts such as Plan parsing, route artifacts, native JSON validation, V8
handler registration, or transport parsing. Those are covered by artifact and
package modes, CTest conformance, and transport lanes.

The public API is test-runner neutral. It throws plain JavaScript errors and
does not import Vitest, Jest, Bun, or `node:test`.
