# Local IPC Example

This source example shows the scoped `sloppy/net` local IPC API shape:
`LocalEndpoint.listen`, `LocalEndpoint.connect`, async iterable accept, bounded byte
reads/writes, delimiter reads that preserve binary data, deadline/cancellation options,
stale cleanup intent, permissions where supported, and explicit `UnixSocket`/`NamedPipe`
entry points.

## Limitations

TLS, HTTP, UDP, WebSocket, external live-network behavior, and Windows AF_UNIX
portability are outside this example.
