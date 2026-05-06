# TCP Echo Example

This source example keeps the echo loop in the scoped TCP lane: ephemeral loopback
listen, async accept, `readChunks`, bounded read/write calls, endpoint metadata, and
cleanup for accepted and outbound connections.

It is not a throughput benchmark, stress/torture lane, TLS, HTTP client/server,
UDP, WebSocket, local IPC, Node/Bun/Deno compatibility, package-manager behavior,
public alpha documentation, or external live-network evidence.
