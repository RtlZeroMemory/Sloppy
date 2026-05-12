# Core Streaming

The native Core stream foundation lives in `include/sloppy/stream.h` and
`src/core/stream.c`. It is an internal alpha primitive for bounded byte flow
between native components. It is not a public JavaScript stream API.

## Contract

Core streams are synchronous, caller-owned, and platform-neutral:

- public types contain Sloppy byte, status, string, and cancellation types only;
- no V8, libuv, socket, file descriptor, or platform handle appears in the API;
- chunks are `SlBytes` views with explicit pointer and length;
- non-empty chunks are borrowed for the duration documented by the adapter;
- writable streams report `BACKPRESSURE` instead of allocating unbounded buffers;
- `close`, `fail`, and `abort` are explicit state transitions;
- cancellation and deadline tokens map to stream statuses without starting timers.

`SlStreamPump` moves data from a readable stream to a writable stream. If a
write returns backpressure, the pump keeps the pending chunk and retries it
after the caller drains or resumes the writable side.

## Adapters

Current adapters are deliberately small:

- memory readable over caller-owned chunk views;
- memory writable over caller-owned fixed storage;
- bounded chunk-list readable and writable adapters;
- `SlHttpResponse` stream descriptor readable adapter.

The chunk-list writer stores borrowed chunk views, not copied bytes. Callers
must keep the bytes alive until the list is consumed.

## Alpha ABI Note

This foundation intentionally breaks the older native HTTP stream chunk shape:
HTTP response stream descriptors now use `SlStreamChunk` directly instead of a
separate response-only chunk type. New native code should treat `SlStreamChunk`
as the single byte-chunk descriptor for Core stream adapters and bounded HTTP
response descriptors.

## HTTP Compatibility

`Results.stream` still builds a bounded JavaScript descriptor before native
HTTP response submission. The HTTP/1.1 libuv transport serializes stream
descriptors as bounded chunked frames with a pending-write cap. `HEAD` requests
still suppress the body, and `204`/`304` responses never write body bytes.

The Core stream adapter lets native code consume existing
`SlHttpResponse.stream_chunks` through the same pump/backpressure vocabulary.
It does not yet replace every transport path, and it does not expose live
handler push or request-body streaming to JavaScript.

## Benchmarks And Fuzzing

`sloppy_bench` includes stream pump benchmarks for memory-to-memory, many
small chunks, a few large chunks, backpressure/resume, and the current
`SlHttpResponse.stream_chunks` adapter path. These benchmarks report
bytes/sec and chunks/sec where applicable, but they are measurement data only.

`fuzz_stream` replays arbitrary chunk bytes through memory adapters, bounded
chunk lists, pump state, and backpressure retry paths.

## Non-Goals

- No WHATWG Streams clone.
- No Node stream compatibility.
- No JavaScript object wrapping for native stream handles.
- No native JSON dispatch or codec redesign.
- No full transport migration in this foundation slice.
