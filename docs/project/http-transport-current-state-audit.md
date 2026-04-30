# HTTP Transport Current State Audit

Status: planning audit for ENGINE-24.

This document separates the completed ENGINE-13 HTTP backend semantics from the still
missing real transport runtime server. It is evidence for creating ENGINE-24 and not an
implementation claim.

## ENGINE-13 Provides

ENGINE-13 now provides the portable HTTP backend state and semantic foundations that a real
transport server should consume:

- request-head parsing with bounded target, header count, header name, header value, total
  header bytes, and body limits in `include/sloppy/http.h` and `src/core/http.c`;
- backend, listener, connection, and request lifecycle state in
  `include/sloppy/http_backend.h` and `src/core/http_backend.c`;
- bounded connection and active-request admission counters in
  `SlHttpBackendOptions.max_connections` and `max_active_requests`;
- body-reader policy in `sl_http_request_body_reader_*`: Content-Length sized body
  accumulation into the request arena, JSON/text media classification, over-limit
  rejection, and cleanup-once failure paths;
- cancellation/deadline/shutdown hooks through `SlCancellationToken` and
  `sl_http_request_cancel`, `sl_http_request_timeout`, and `sl_http_request_shutdown`;
- dispatch preflight to route metadata, request context materialization, and V8 handler
  calls through `src/core/http_dispatch.c`;
- deterministic response serialization with `Connection: close`, `Content-Type`, and
  `Content-Length` in `src/core/http_response.c`;
- default non-V8 unit and conformance-style smoke over parser/lifecycle/body policy,
  overload, shutdown, dispatch diagnostics, and response serialization in
  `tests/unit/core/test_http*.c` and `tests/integration/http_dispatch/`.

These pieces are intentionally core C semantics. They do not own sockets, bind ports,
drive libuv TCP callbacks, or prove a localhost socket/curl flow.

## Missing Real Transport

The next layer must provide:

- TCP bind/listen over a Slop-owned platform/libuv boundary;
- accept lifecycle and connection allocation that consumes ENGINE-13 connection admission;
- connection object lifetime tied to libuv close/write callbacks;
- bounded read loop and partial request accumulation across TCP chunks;
- request head/body framing over the TCP byte stream before feeding ENGINE-13 parser/body
  reader APIs;
- transport-level cancellation when a client disconnects;
- response write loop that retains serialized bytes until `uv_write` completion;
- close-after-response policy for the MVP;
- graceful stop over the real listener and real active connections;
- deterministic localhost socket or curl smoke evidence.

## Current Server Behavior

`src/main.c` has an EPIC-22/23 dev-only `sloppy run --artifacts` socket loop. It uses
libuv directly inside the CLI executable:

- `SlRunServer` owns `uv_loop_t`, `uv_tcp_t listener`, and a fixed array of
  `SlRunClient` records;
- each `SlRunClient` owns one `uv_tcp_t`, fixed request buffer, fixed response buffer, and
  one `uv_write_t`;
- `sl_run_server` binds an IPv4 host/port, listens with backlog 16, and runs
  `uv_run(UV_RUN_DEFAULT)`;
- `sl_run_client_read_cb` appends bytes into a fixed request buffer, waits for
  `\r\n\r\n`, scans Content-Length and Transfer-Encoding, parses a complete request with
  `sl_http_parse_request_head`, dispatches through the existing runtime path, writes one
  serialized response, and closes the connection.

That path is useful executable development plumbing, but it is not the final transport
runtime:

- libuv types and callback policy live in `src/main.c`, not under `src/platform/libuv/` or
  a reusable runtime transport boundary;
- it bypasses `SlHttpBackend`, `SlHttpConnection`, `SlHttpRequestLifecycle`, and
  `SlHttpBodyReader`, so ENGINE-13 lifecycle, admission, shutdown, timeout, and body-reader
  semantics are not the authoritative server path;
- it has no real shutdown/drain API, no transport-level request cancellation on
  disconnect, no timer integration, and no structured server diagnostics;
- it should be retired or wrapped by ENGINE-24 once the real transport server exists.

## Findings

| File path | Current behavior | Risk | Required ENGINE-24 task |
| --- | --- | --- | --- |
| `include/sloppy/http_backend.h` | Defines backend/listener/connection/request lifecycle and an opaque `SlHttpPlatformListener`, but no real transport server API or libuv-owned listener object. | Later code may keep adding socket behavior to CLI code or leak platform types into public/core headers. | ENGINE-24.A defines the Slop-owned transport boundary, config/state types, init/dispose contracts, and libuv ownership rules. |
| `src/core/http_backend.c` | Starts/stops core state and records admission counters, parser limits, cancellation, body-reader, and shutdown semantics without sockets or timers. | Core semantics can be proven while real TCP paths bypass them, causing false confidence about disconnects, deadlines, and drain behavior. | ENGINE-24.A/B/E must wire real listener/connection/request work through this semantic layer or document any boundary adapter explicitly. |
| `src/main.c` | CLI-local dev server binds/listens/accepts/reads/writes with raw libuv callbacks and fixed client buffers. | Runtime transport policy is hidden in the CLI, cannot be reused by tests or app-host code, and is easy to evolve separately from ENGINE-13. | ENGINE-24.B/C/D replaces or wraps this path with a real transport runtime server and leaves CLI as a caller of that boundary. |
| `src/main.c` | `SlRunClient` uses fixed request/response arrays and closes after `uv_write`; client slots are marked inactive in the close callback. | Use-after-free/double-close risk if future callbacks outlive slot reuse, and no shared cleanup-once transport lifecycle exists. | ENGINE-24.B/D defines connection ownership, write buffer lifetime until completion, and exactly-once cleanup. |
| `src/main.c` | Read accumulation scans for header terminator and Content-Length before complete-buffer parsing. It rejects extra bytes after the first request. | Partial request handling exists only in the CLI path and does not consume ENGINE-13 body-reader cancellation/shutdown semantics. Keep-alive and pipelining could be accidentally implied. | ENGINE-24.C owns bounded accumulation, Content-Length-only framing, no pipelining, and malformed/over-limit behavior. |
| `src/main.c` | Transfer-Encoding gets a 501-style response; unsupported media/body diagnostics are later mapped through dispatch. | Transport and message-layer errors are split by ad hoc checks instead of one planned matrix. | ENGINE-24.C/D/F define deterministic 400/413/415/501/503/408-style mapping where enough context exists. |
| `src/main.c` | There is no request timeout, header timeout, write timeout, disconnect cancellation, or server stop/drain API around active clients. | Shutdown races, hanging requests, and provider/V8 work continuing after disconnect can become invisible. | ENGINE-24.E owns disconnect cancellation, timer policy, stop accepting, drain/cancel active connections, and late-completion safety. |
| `src/core/http_dispatch.c` | Dispatch materializes request context and calls V8 through the runtime-contract helper; invariants forbid socket/libuv/V8 type leakage into HTTP dispatch. | Transport callbacks could call into V8 from the wrong thread if the server path is bolted on directly. | ENGINE-24.D/E route dispatch through the owner-thread scheduler/runtime boundary; no worker/transport callback may enter V8 directly. |
| `include/sloppy/async_backend.h` and `src/platform/libuv/async_backend_libuv.c` | Libuv-backed async completion posting exists and hides libuv handles from public headers. | HTTP transport could bypass the existing owner-thread completion model and invent a second scheduling policy. | ENGINE-24.A/D/E define when transport callbacks dispatch inline on the owner loop versus post completions, preserving V8 owner-thread rules. |
| `include/sloppy/cancellation.h` and `src/core/cancellation.c` | Cancellation tokens are small caller-owned snapshots; they do not start timers or interrupt blocking native work. | Disconnect/timeout may be overclaimed as provider interruption. | ENGINE-24.E documents that cancellation marks terminal request/provider state; active blocking provider interruption remains provider-specific. |
| `src/core/http_response.c` | Response serialization writes complete bounded bytes into caller-owned storage and includes `Connection: close`. | A write buffer freed or reused before `uv_write` completion can corrupt responses or crash. | ENGINE-24.D owns response byte lifetime through write completion and close-after-response cleanup. |
| `tests/unit/core/test_http*.c` | Tests cover parser, response, context, backend lifecycle/body/shutdown/admission, and synthetic dispatch. | Unit tests do not prove localhost bind/listen/read/write or callback lifetime behavior. | ENGINE-24.F adds real localhost socket/curl smoke without benchmark claims. |
| `tests/integration/http_dispatch/test_http_dispatch_execution.c` | V8-gated integration exercises dispatch/handler paths when SDK evidence exists. | End-to-end app proof remains incomplete without real transport and provider combination. | ENGINE-24.F plus ENGINE-17.E plan localhost HTTP + SQLite users API proof after transport lands. |

## Risk Register

- Accidental unbounded connection queues or read buffers if libuv backlog/read callbacks are
  treated as application queues.
- Use-after-free when connection slots are reused before close/write callbacks finish.
- Shutdown races between listener close, active reads, active writes, and request/provider
  completions.
- V8 owner-thread violations if transport callbacks call handlers directly from the wrong
  execution context.
- Provider operations continuing after client disconnect without terminal request state and
  cleanup-only late completion rules.
- Confusing keep-alive or pipelining semantics if extra bytes are accepted without a
  documented sequential request model.
- Fake production claims from localhost or stress smoke that does not prove internet-edge
  hardening.

ENGINE-24 should keep the first transport server boring: HTTP/1.1 over localhost TCP,
Content-Length only, close-after-response, bounded buffers/admission, deterministic
errors, and honest local smoke evidence.
