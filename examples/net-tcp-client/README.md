# TCP Client Example

This source example shows the scoped `sloppy/net` TCP client API shape for a
loopback service: `NetworkAddress.parse`, `TcpClient.connect`, `writeText`,
`readLine`, deadline propagation, `noDelay`, `keepAlive`, and deterministic close.

## Limitations

TLS, HTTP, UDP, WebSocket, and external live-network behavior are outside this
example.
