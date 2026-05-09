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
- outbound HTTP client coverage: `bootstrap.stdlib.http_client` verifies the HTTP/1.1
  request/response lane and explicit HTTP/2 `h2c`/TLS `h2` lanes over the TCP and
  private TLS bridges with deterministic loopback status/header/body, HTTPS
  trust-store and insecure-skip verification, body-consumption,
  malformed-response, body-limit, ambiguous-body, bounded
  stream/deadline/cancellation, per-origin pooling, pool
  exhaustion, redirect loop/max, cross-origin sensitive-header stripping/denial,
  strict-network denial, and DNS-failure checks;
- compiler/tooling coverage: Rust compiler tests cover `sloppy/net` import activation for
  `stdlib.net`, `features.network`, and `strongPlan.evidence.network`;
- V8-gated coverage: `conformance.v8.runtime_bridge` covers active `__sloppy.net`
  registration, `stdlib.httpclient` activation of that private bridge dependency, TCP
  client/listener loopback smoke, LocalEndpoint bridge/path validation, promise settlement
  through the V8 owner-thread path, and inactive-feature gating when a V8 SDK is configured.
- HTTP/2 transport coverage: `core.http2_frame`, `core.http2_hpack`,
  `core.http2_session`, `core.http2_mapping`, and `core.http2_dispatch` cover the native
  protocol units. `conformance.transport.http2_h2c`,
  `conformance.transport.http2_h2c_upgrade`, and
  `conformance.transport.http2_tls_alpn` cover h2c prior knowledge, h2c
  Upgrade, and server h2 over TLS ALPN through the libuv listener paths. The
  HTTP client bootstrap lane covers explicit h2c and h2, pooled concurrent h2
  streams over one connection, HTTPS `auto` ALPN h2 selection, HTTP/1.1
  fallback under `auto`, explicit h2 fail-closed behavior, per-stream
  RST_STREAM failure isolation, conservative pooled-client GOAWAY failure,
  informational responses, response content-length validation, frame-size caps,
  SETTINGS ACK, CONTINUATION request headers, and HPACK Huffman response
  decoding. Full outbound client flow-control and graceful GOAWAY drain are
  tracked in #1015 and are not claimed by this lane.
  Optional external-tool smoke wrappers live at `tools/windows/test-http2.ps1`
  and `tools/unix/test-http2.sh`; they report missing h2spec, curl, nghttp,
  and h2load lanes as `UNAVAILABLE` or `SKIPPED`. A wrapper run is not h2spec
  coverage unless h2spec is present and actually executed. The Linux clang CI
  lane installs h2spec and runs full h2spec against a live Sloppy h2c transport
  server. Curl, nghttp, and h2load are separate smoke lanes and count as
  coverage only when the wrapper reports `PASS` for that tool.

Local IPC coverage adds POSIX-gated Unix domain socket native tests, Windows-gated named
pipe native tests, local IPC API validation, stable diagnostic shapes, V8 bridge wiring,
and doctor/audit/source-example coverage under the same `stdlib.net` feature.
HTTP/3, gRPC, WebTransport, server push, HTTP server behavior beyond the
existing inbound lanes, UDP, WebSocket,
direct WinSock/epoll/kqueue/io_uring public APIs, crypto implementation,
package-manager behavior, release docs, benchmark reports, stress/torture, and
external live-network validation are separate work.
