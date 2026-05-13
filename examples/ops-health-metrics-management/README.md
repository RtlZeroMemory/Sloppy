# Operations Health Metrics Management

This example wires the first-party operations backend:

- liveness at `/live`;
- readiness at `/ready`;
- startup at `/startup`;
- detailed health at `/health`;
- protected management endpoints under `/_sloppy`;
- Prometheus metrics at `/_sloppy/metrics`;
- JSON metrics at `/_sloppy/metrics.json`.

The management group uses a small local `protect` hook. Real deployments should
use an auth policy or ingress policy appropriate for the environment.
