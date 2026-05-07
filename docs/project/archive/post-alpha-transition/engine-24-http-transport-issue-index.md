# ENGINE-24 HTTP Transport Issue Index

Status: issue index for ENGINE-24 planning. This index was created after checking the open
issue list for existing ENGINE-24 or HTTP transport server duplicates.

## Issues

| Role | Issue | Title |
| --- | --- | --- |
| EPIC | #411 | EPIC ENGINE-24: HTTP Transport Runtime Server |
| Task | #412 | TASK ENGINE-24.A: Transport Architecture and Libuv Boundary |
| Task | #413 | TASK ENGINE-24.B: TCP Bind, Listen, and Accept Lifecycle |
| Task | #414 | TASK ENGINE-24.C: Connection Read Loop and Request Accumulation |
| Task | #415 | TASK ENGINE-24.D: Dispatch and Response Write Loop |
| Task | #416 | TASK ENGINE-24.E: Transport Cancellation, Timeout, and Shutdown |
| Task | #417 | TASK ENGINE-24.F: Localhost Transport Smoke and Conformance |
| Task | #418 | TASK ENGINE-24.G: Keep-Alive Decision and Deferred HTTP/1.1 Upgrade Plan |

## Recommended Implementation Order

1. #412 ENGINE-24.A: Transport Architecture and Libuv Boundary.
2. #413 ENGINE-24.B: TCP Bind, Listen, and Accept Lifecycle.
3. #414 ENGINE-24.C: Connection Read Loop and Request Accumulation.
4. #415 ENGINE-24.D: Dispatch and Response Write Loop.
5. #416 ENGINE-24.E: Transport Cancellation, Timeout, and Shutdown.
6. #417 ENGINE-24.F: Localhost Transport Smoke and Conformance.
7. #418 ENGINE-24.G: Keep-Alive Decision and Deferred HTTP/1.1 Upgrade Plan.

## Dependencies

- #412 should land before or with #413 so the libuv boundary is not invented ad hoc in the
  bind/listen PR.
- #413 depends on #412 and establishes the listener/connection lifecycle used by all later
  work.
- #414 depends on #412/#413 and should consume ENGINE-13 parser/body-reader semantics.
- #415 depends on #414 for complete request materialization and on existing ENGINE-13
  dispatch/response semantics.
- #416 depends on #414/#415 because cancellation, timeout, and shutdown must cover read,
  dispatch, and write terminal states.
- #417 depends on #413/#414/#415/#416 and should prove the whole localhost MVP.
- #418 is docs-only and may land after #412 or after MVP evidence, but it must not enable
  keep-alive by implication.

## Parallelism

Can run in parallel:

- #418 can run alongside implementation tasks if it remains docs-only and does not change
  MVP semantics.
- Test harness scaffolding for #417 can be drafted after #413 if it avoids assuming #414/#415
  interfaces before they land.

Must not run fully in parallel:

- #412/#413 should land first.
- #414 and #415 should not run fully in parallel unless their request/response interfaces
  are locked by #412/#413.
- #416 should wait until #414/#415 exist so terminal-state coverage is real.
- #417 should wait until #413/#414/#415/#416 exist; otherwise it risks becoming synthetic
  smoke instead of transport evidence.

## Suggested PR Grouping

- PR 1: #412/#413.
- PR 2: #414.
- PR 3: #415.
- PR 4: #416.
- PR 5: #417.
- PR 6: #418 docs-only if not already landed.

## Non-Goals For The Sequence

- No keep-alive in the MVP.
- No pipelining, TLS, HTTP/2, HTTP/3, WebSockets, static files, compression, streaming
  bodies, reverse proxy behavior, production internet-edge claims, or benchmark claims.
- No PostgreSQL/SQL Server JS bridge work.
- No framework/app-layer planning beyond documenting that it follows real transport plus
  end-to-end proof.
