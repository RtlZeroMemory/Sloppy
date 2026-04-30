# ENGINE-04 HTTP API Runtime Execution Plan

Status: Active implementation plan for issues #284, #285, and #286 under ENGINE-04 (#261).

## Goal

Complete the bounded framework HTTP runtime for realistic local APIs without claiming
production web-server breadth.

## Scope

- Dispatch GET, POST, PUT, PATCH, and DELETE from compiler/Plan route metadata.
- Preserve route params and query behavior while adding request headers and bounded
  JSON/text request bodies.
- Keep parser/body policy deterministic: unsupported framing, unsupported media types,
  invalid JSON, and oversized bodies fail before handler entry.
- Serialize supported `Results.*` descriptors into stable native HTTP responses.
- Map handler/runtime failures to safe dev error responses.
- Keep HTTP-specific V8 conversion in `src/engine/v8/http_bridge.cc`, not
  `src/engine/v8/engine_v8.cc`.

## Non-Goals

- No TLS, production internet-facing hardening, middleware/filter pipeline, multipart/file
  upload, streaming request or response bodies, cookies/sessions, Node/npm compatibility,
  package manager behavior, or provider work.
- No public-alpha documentation claims beyond the implemented local/dev runtime.

## Implementation Steps

1. Extend core HTTP parsing/dispatch/result types with body bytes, content-type policy,
   request body kind, custom response headers, and deterministic diagnostics.
2. Consume runnable Plan route metadata for GET/POST/PUT/PATCH/DELETE and preserve 404/405
   behavior.
3. Split HTTP request-context and `Results.*` V8 conversion into `http_bridge.cc`.
4. Wire `sloppy run` to read bounded Content-Length bodies for the dev-only local server.
5. Update bootstrap/classic result helpers and route method helpers.
6. Add default unit coverage and V8-gated integration coverage.
7. Update source-of-truth docs, quality/debt trackers, and conformance notes.

## Validation

Required gates are the canonical Windows workflow plus JS/Rust standards, cargo fmt,
clippy, cargo tests, `git diff --check`, and final status/ignored review. V8-gated tests
must be reported separately when an SDK-enabled configuration is run.
