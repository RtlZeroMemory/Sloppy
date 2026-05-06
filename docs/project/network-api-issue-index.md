# CORE-NET-01 Issue Index

Parent EPIC: #581 CORE-NET-01: TCP/IP Networking Runtime API.

Status: PR 4 DNS/address/socket-option implementation.

| Issue | Slice | PR Group | Status |
| --- | --- | --- | --- |
| #584 | API Contract and Network Policy | PR 1 | Contract source docs, public TCP API shape, lifecycle, and network policy defined. |
| #585 | Feature, Plan, Capability, and Diagnostics Model | PR 1 | `stdlib.net` feature, compiler Plan activation, and stable diagnostics defined. |
| #586 | Native TCP Backend Contract and libuv Implementation | PR 2 | Native TCP client/connection contract and private libuv backend landed. |
| #587 | TcpClient and TcpConnection API | PR 2 | `TcpClient` and `TcpConnection` native/std/V8 surface landed. |
| #588 | TcpListener and Accept Loop API | PR 3 | `TcpListener`, loopback listen, ephemeral ports, accept, close/abort, and JS async accept iteration landed. |
| #589 | Streams, Backpressure, Deadlines, and Cancellation | PR 3 | Bounded connection stream helpers, accept timeout, stale/closed lifecycle checks, and cleanup-once listener/connection paths landed; broader stress/torture remains final evidence. |
| #590 | DNS, Address Parsing, IPv4/IPv6, and Socket Options | PR 4 | `NetworkAddress` text/object parsing, local DNS-backed listen, IPv4/IPv6 address handling, endpoint metadata, and checked `noDelay`/`keepAlive` option propagation landed. |
| #591 | V8/Stdlib Integration and JS Surface | PR 2 | Native intrinsics and bootstrap stdlib surface landed, extended by PR 3 listener methods. |
| #592 | Doctor/Audit, Conformance, Examples, Docs, and Goldens | PR 5 | Deferred until the final evidence/examples pass. |

## PR Order

1. CORE-NET-01.A/B: contract, network policy, feature id, Plan metadata, diagnostics.
2. CORE-NET-01.C/D/H: native TCP backend, client/connection API, and V8/stdlib surface.
3. CORE-NET-01.E/F: listener/accept loop, streams, backpressure, deadlines, cancellation.
4. CORE-NET-01.G: DNS, address parsing, IPv4/IPv6, endpoint metadata, socket options.
5. CORE-NET-01.I: doctor/audit, conformance, examples, docs, and goldens.

## Decisions Locked In PR 1

- Public import is `sloppy/net`.
- Runtime feature id is `stdlib.net`.
- Private V8 intrinsic namespace is `__sloppy.net`.
- Default runtime availability is true when the TCP backend dependencies are available.
- TCP is implemented through Slop-owned contracts over libuv, not direct OS socket APIs.
- Development mode allows easy loopback workflows; strict mode can require explicit
  external connect/listen allow rules.
- Dynamic host/port metadata is represented as partial/dynamic, not guessed.
- No TLS, HTTP client, UDP, WebSocket, local IPC, Node/Bun/Deno compatibility,
  package-manager behavior, public alpha docs, benchmark claims, or crypto implementation
  are included in this EPIC.

## Evidence Expected Later

Future PRs must add examples, doctor/audit goldens, final network conformance, and
platform skip reporting. External live-network and benchmark lanes remain optional and
separately reported.
