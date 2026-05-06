# Strict Network Policy Example

This source example shows the intended Plan-visible shape for strict network access:
literal connect/listen targets, explicit access grants, loopback listen metadata, and
bounded admission before socket work.

The policy object is example metadata, not a package-manager manifest or an OS sandbox.
It does not prove external live-network access, TLS, HTTP, UDP, WebSocket, local IPC,
Node/Bun/Deno compatibility, public alpha readiness, benchmark evidence, or secret
endpoint disclosure.
