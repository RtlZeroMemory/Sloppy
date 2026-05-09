# Strict Network Policy Example

This source example shows the intended Plan-visible shape for strict network access:
literal connect/listen targets, explicit access grants, loopback listen metadata, and
bounded admission before socket work.

The policy object is example metadata, not a package manifest or an OS sandbox.

## Limitations

External live-network access, TLS, HTTP, UDP, WebSocket, local IPC, and secret
endpoint handling are outside this example.
