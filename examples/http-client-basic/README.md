# HTTP Client Basic

This source example shows the outbound `HttpClient` API shape from `sloppy/net`: reusable
client creation, base URL joins, bounded response bodies, JSON helpers, redirect policy,
per-origin pooling, and strict outbound-network metadata.

## Limitations

This example is limited to the outbound `HttpClient` API shape shown in source.
Live external network behavior, TLS policy, proxy policy, UDP, and WebSocket
behavior are separate examples or runtime lanes.
