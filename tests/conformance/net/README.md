# Network Conformance

This directory is the network conformance validation index.
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
- default CLI metadata coverage: `sloppy.cli.doctor_network_*` and
  `sloppy.cli.audit_network_*` validate Plan-visible network capability metadata for
  `connect`, `listen`, and `connect-listen` while keeping separate OS sandboxing or external
  network access;
- default HTTP client CLI metadata coverage: `sloppy.cli.doctor_http_client_*` and
  `sloppy.cli.audit_http_client_*` validate Plan-visible `stdlib.httpclient`, named-client,
  static target, dynamic/partial target, and strict-network metadata without leaking
  URLs, headers, cookies, bearer tokens, API keys, or TLS-sensitive material;
- default source examples: `examples.net.api_shape` checks TCP client, listener, echo,
  strict-policy, deadline/cancellation, LocalEndpoint local IPC, and HTTP client examples
  for the supported public API shape and rejects adjacent compatibility wording;
- bootstrap JavaScript stdlib coverage: `bootstrap.stdlib.app_host_foundation` executes
  the ESM stdlib with deterministic native-hook fakes and verifies `TcpClient`,
  `TcpListener`, `TcpConnection`, `LocalEndpoint`, `UnixSocket`, `NamedPipe`, and
  `NetworkAddress` surface behavior;
- outbound HTTP client coverage: `bootstrap.stdlib.http_client` verifies the cleartext
  HTTP/1.1 request/response lane over the TCP bridge with deterministic loopback
  status/header/body, body-consumption, malformed-response, body-limit, HTTPS-unavailable,
  ambiguous-body, bounded stream/deadline/cancellation, per-origin pooling, pool
  exhaustion, redirect loop/max, cross-origin sensitive-header stripping/denial,
  strict-network denial, and DNS-failure checks;
- compiler/tooling coverage: Rust compiler tests cover `sloppy/net` import activation for
  `stdlib.net`, `features.network`, and `strongPlan.evidence.network`;
- V8-gated coverage: `conformance.v8.runtime_bridge` covers active `__sloppy.net`
  registration, `stdlib.httpclient` activation of that private bridge dependency, TCP
  client/listener loopback smoke, LocalEndpoint bridge/path validation, promise settlement
  through the V8 owner-thread path, and inactive-feature gating when a V8 SDK is configured.

Local IPC coverage adds POSIX-gated Unix domain socket native tests, Windows-gated named
pipe native tests, local IPC API validation, stable diagnostic shapes, V8 bridge wiring,
and doctor/audit/source-example coverage under the same `stdlib.net` feature.
TLS, HTTP server behavior beyond the existing inbound lanes, UDP, WebSocket,
direct WinSock/epoll/kqueue/io_uring public APIs, crypto implementation,
package-manager behavior, release docs, benchmark reports, stress/torture, and
external live-network validation are separate work.
