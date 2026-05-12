# Core Streaming

Core streams live in `include/sloppy/stream.h` and `src/core/stream.c`. They
are Sloppy's native byte-stream vocabulary for bounded runtime components. They
are not exposed as public JavaScript stream handles.

## Contract

Core streams are synchronous, caller-owned, and platform-neutral:

- public types contain Sloppy byte, status, string, and cancellation types only;
- no V8, libuv, socket, file descriptor, or platform handle appears in the API;
- chunks are `SlBytes` views with explicit pointer and length;
- non-empty chunks are borrowed for the lifetime documented by the adapter;
- writable streams report `BACKPRESSURE` instead of allocating unbounded buffers;
- writable adapters may accept partial chunk prefixes and must report the exact
  accepted byte count;
- `SlStreamPump` retains the unwritten tail after partial writes or
  backpressure and retries that tail after the caller drains or resumes;
- cancellation and deadline tokens map to stream statuses without starting
  timers.

Returning `OK` with zero bytes for a non-empty write is an invalid writable
adapter contract. Returning `OK` or `BACKPRESSURE` with `bytes_written` smaller
than the chunk length is valid; the caller owns retrying the remaining slice.

## Adapters

Implemented adapters:

- memory readable over caller-owned chunk views;
- memory writable over caller-owned fixed storage with partial-write
  backpressure;
- bounded chunk-list readable and writable adapters;
- `SlHttpResponse` stream descriptor readable adapter;
- bounded request-body readable adapter over `SlHttpRequestLifecycle.head.body`.

The chunk-list writer stores borrowed chunk views, not byte copies. Callers must
keep the bytes alive until the list is consumed.

## HTTP Runtime Integration

`SlHttpResponse.stream_chunks` uses `SlStreamChunk` directly. The older
response-only stream chunk type is gone.

The core HTTP/1.1 response writer supports bounded stream responses. It validates
chunks, computes `Content-Length` with checked arithmetic, preserves correct
metadata for `HEAD`, rejects non-empty stream bodies for `204` and `304`, and
writes the bounded body when not suppressed.

The libuv HTTP/1.1 transport keeps live socket scheduling in the transport, but
bounded stream descriptors are lowered into `SlReadableStream` before chunked
frame emission. The transport enforces `max-pending-write-bytes` per write and
`max-response-bytes` across the serialized stream response. HTTP/2 mapping also
consumes stream descriptors through the Core readable adapter before DATA bytes
are submitted.

Request bodies are still bounded before handler dispatch. Internally, native
code can adapt the current bounded request body to `SlReadableStream`; public JS
request body helpers still expose bounded `text()`, `json()`, and byte helpers.
Native schema-backed JSON request validation consumes the current bounded body;
incremental native JSON parsing from a live stream is not implemented here.

## JavaScript Surfaces

`Results.stream` builds a bounded descriptor before the handler returns. SSE
uses that same descriptor path and is serialized by the native response stream
path, but it is not live browser push: the handler does not stay attached to the
socket after returning.

`Compression.gzipStream` and `Compression.gunzipStream` are bounded compatibility
wrappers over the current whole-buffer compression bridge. They accept
iterable/async-iterable input chunks, enforce size/cancellation/deadline
options, and yield one output chunk. They are not native Core zlib stream
adapters yet.

`HttpClientResponse.stream()`, TCP `readChunks()`, LocalEndpoint `readChunks()`,
and filesystem `readChunks()` are JavaScript async-iterator helpers over bounded
or repeated reads. They do not expose native Core stream handles.

## Benchmarks And Fuzzing

`sloppy_bench` includes stream pump, partial-write/backpressure resume, HTTP
response serialization, SSE descriptor serialization, response adapter, and
request-body adapter benchmarks. Stream benchmark output includes ns/op,
bytes/sec, chunks/sec, checksum, and deterministic backpressure count where
applicable.

`fuzz_stream` covers arbitrary chunk bytes through memory adapters, bounded
chunk lists, pump retry state, invalid chunks, empty chunks, large chunk counts,
and stream response serialization.

## Non-Goals

- No WHATWG Streams clone.
- No Node stream compatibility.
- No JavaScript object wrapping for native stream handles.
- No live SSE socket push.
- No WebSocket upgrade execution.
- No live incremental native JSON parser or public JSON stream writer.
- No native incremental zlib stream adapter.
