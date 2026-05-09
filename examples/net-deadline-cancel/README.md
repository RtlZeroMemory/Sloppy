# TCP Deadline And Cancellation Example

This source example shows `TcpClient.connect` with `Deadline.after`,
`CancellationController`, connect timeout, read/write deadline options, signal options,
and cleanup after timeout or cancellation.

Late completions are cleanup-only in the runtime contract.

## Limitations

This example is limited to TCP deadlines and cancellation. TLS, HTTP, UDP,
WebSocket, local IPC, and external live-network behavior are outside this
example.
