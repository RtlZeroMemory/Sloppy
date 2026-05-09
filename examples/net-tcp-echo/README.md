# TCP Echo Example

This source example keeps the echo loop in the scoped TCP lane: ephemeral loopback
listen, async accept, `readChunks`, bounded read/write calls, endpoint metadata, and
cleanup for accepted and outbound connections.

## Limitations

This example is limited to scoped TCP echo behavior. Throughput benchmarks,
stress/torture runs, TLS, HTTP, UDP, WebSocket, local IPC, and external
live-network behavior are outside this example.
