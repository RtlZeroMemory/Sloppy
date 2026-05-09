# TCP Deadline And Cancellation Example

This source example shows `TcpClient.connect` with `Deadline.after`,
`CancellationController`, connect timeout, read/write deadline options, signal options,
and cleanup after timeout or cancellation.

It does not claim to cancel arbitrary already-completed native work. Late completions are
cleanup-only in the runtime contract. This example is not TLS, HTTP, UDP, WebSocket,
local IPC, Node/Bun/Deno compatibility, package-manager behavior, public release
documentation, benchmark evidence, or external live-network evidence.
