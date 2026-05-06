# CORE-NET-02 Issue Index

Parent EPIC: #582 CORE-NET-02: Local IPC: Unix Domain Sockets and Windows Named Pipes.

| Issue | Slice | Status |
| --- | --- | --- |
| #593 | API contract and platform policy | PR IPC-1 defines `LocalEndpoint`, `UnixSocket`, `NamedPipe`, platform support, and non-goals. |
| #594 | Feature, Plan, capability, and diagnostics model | PR IPC-1 keeps `stdlib.net`, network capability metadata, and adds stable local IPC diagnostics. |
| #595 | Unix domain socket backend | Deferred to POSIX backend PR. |
| #596 | Windows named pipe backend | Deferred to Windows backend PR. |
| #597 | LocalEndpoint JS API | PR IPC-1 adds validating stdlib surface that fails honestly until backend bridge functions land. Backend integration is deferred. |
| #598 | Path/FS policy, stale cleanup, permissions | PR IPC-1 defines named-root-only endpoint paths, stale cleanup rules, and permission-mode policy. |
| #599 | Streams, backpressure, deadline, cancellation | Deferred to lifecycle/streams PR after both backend paths exist. |
| #600 | Doctor/audit, conformance, examples, docs, goldens | Deferred to final evidence PR after executable behavior exists. |

## Current Completion Boundary

IPC-1 is a policy and contract PR. It does not implement Unix domain sockets, Windows named
pipes, executable V8 local IPC bridge calls, doctor/audit output, examples, conformance
aliases, public alpha docs, or benchmark/performance claims.

## PR Sequence

1. IPC-1: #593, #594, #598, plus API-shape validation for #597 without backend success.
2. IPC-2: #595 and POSIX `LocalEndpoint` integration.
3. IPC-3: #596 and Windows `LocalEndpoint` integration.
4. IPC-4: #599 stream, backpressure, deadline, cancellation cleanup.
5. IPC-5: #600 doctor/audit, conformance, examples, docs, and goldens.
