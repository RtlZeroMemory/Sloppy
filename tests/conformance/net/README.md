# Network Conformance

Status: CORE-NET-01.I evidence index.

Network conformance is split by lane:

- default native loopback execution: `core.net_tcp` covers TCP client connect/write/read,
  embedded-NUL bytes, close/abort, stale handles, loopback listener bind, ephemeral ports,
  accept, listener shutdown, local hostname DNS-backed listen, IPv4/IPv6 address handling
  where the platform supports it, endpoint metadata, and checked `noDelay`/`keepAlive`
  option propagation;
- default diagnostics: `core.diagnostics.foundation` pins stable JSON goldens for network
  feature availability, policy denial, invalid host/port, DNS failure, connect timeout,
  cancellation, connection closed/stale handle, read/write timeout/cancellation,
  backpressure overflow, unsupported option, and backend availability shapes;
- default CLI metadata evidence: `sloppy.cli.doctor_network_*` and
  `sloppy.cli.audit_network_*` prove Plan-visible network capability metadata for
  `connect`, `listen`, and `connect-listen` without claiming OS sandboxing or external
  network access;
- default source examples: `examples.net.api_shape` checks TCP client, listener, echo,
  strict-policy, and deadline/cancellation examples for the supported public API shape and
  rejects obvious Node/Bun/Deno, package-manager, benchmark, and adjacent-protocol claims;
- bootstrap JavaScript stdlib evidence: `bootstrap.stdlib.app_host_foundation` executes
  the ESM stdlib with deterministic native-hook fakes and verifies `TcpClient`,
  `TcpListener`, `TcpConnection`, and `NetworkAddress` surface behavior;
- outbound HTTP client evidence: `bootstrap.stdlib.http_client` verifies the cleartext
  HTTP/1.1 request/response lane over the TCP bridge with deterministic loopback
  status/header/body, body-consumption, malformed-response, body-limit, HTTPS-unavailable,
  ambiguous-body, bounded stream/deadline/cancellation, per-origin pooling, pool
  exhaustion, redirect loop/max, cross-origin sensitive-header stripping/denial,
  strict-network denial, and DNS-failure checks;
- compiler/tooling evidence: Rust compiler tests cover `sloppy/net` import activation for
  `stdlib.net`, `features.network`, and `strongPlan.evidence.network`;
- V8-gated evidence: `conformance.v8.runtime_bridge` covers active `__sloppy.net`
  registration, TCP client/listener loopback smoke, promise settlement through the V8
  owner-thread path, and inactive-feature gating when a V8 SDK is configured.

CORE-NET-02.A/B/F adds local IPC API validation and stable diagnostic shapes under the
same `stdlib.net` feature. This is not Unix domain socket or Windows named pipe backend
execution, TLS, HTTP server behavior beyond the existing inbound lanes, UDP, WebSocket,
Node/Bun/Deno compatibility, direct
WinSock/epoll/kqueue/io_uring backend evidence, crypto implementation, package-manager
behavior, public alpha documentation, benchmark evidence, stress/torture evidence, or
external live-network proof.
