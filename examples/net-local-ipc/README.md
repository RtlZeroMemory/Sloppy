# Local IPC Example

This source example shows the scoped `sloppy/net` local IPC API shape:
`LocalEndpoint.listen`, `LocalEndpoint.connect`, async accept, bounded byte
reads/writes, delimiter reads that preserve binary data, stale cleanup intent,
permissions where supported, and explicit `UnixSocket`/`NamedPipe` entry points.

It is not a TLS client, HTTP client, UDP client, WebSocket client, Node/Bun/Deno
compatibility claim, package-manager behavior, public alpha documentation,
benchmark evidence, external live-network evidence, or a claim that Windows
AF_UNIX is portable.
