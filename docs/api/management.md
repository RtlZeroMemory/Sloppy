# Management

`app.management()` installs an opt-in actuator-style backend endpoint group.

```ts
import { Sloppy } from "sloppy";

const app = Sloppy.create();

app.management({
    path: "/_sloppy",
    protect: (ctx) => ctx.request.headers.get("x-ops-key") === "local-dev-key",
});
```

Management endpoints are disabled by default. Calling `app.management()` adds:

| Endpoint | Description |
| --- | --- |
| `/_sloppy/health` | detailed health output |
| `/_sloppy/live` | liveness output |
| `/_sloppy/ready` | readiness output |
| `/_sloppy/startup` | startup output |
| `/_sloppy/metrics` | Prometheus metrics text |
| `/_sloppy/metrics.json` | JSON metrics snapshot |
| `/_sloppy/info` | safe app/runtime information |
| `/_sloppy/runtime` | safe route, health, metrics, and runtime summary |

The `protect` hook applies to the management endpoint group. Return `true` to
allow a request; any other result returns `403`.

## Info

`/_sloppy/info` includes safe app metadata:

- app name and version from config when present;
- Sloppy runtime surface;
- whether detailed management endpoints are protected.

It does not dump environment variables, headers, cookies, config values, or
secrets.

## Runtime

`/_sloppy/runtime` includes:

- route counts;
- management and health endpoint counts;
- worker resource count;
- registered health checks;
- current metrics snapshot.

Runtime output is redacted and bounded. It is intended for operators and
diagnostics, not for public unauthenticated traffic.

## Health Defaults

If no checks were registered with `app.health()`, `app.management()` registers
default `self`, `runtime`, and `memory` checks so the endpoint group is useful
without extra user code.
