# WebSocket Echo

WebSocket echo endpoint with text, JSON, schema validation, origin policy,
subprotocol selection, and message limits.

Use `TestHost.create(app)` for the full app-host behavior, including JSON
validation, heartbeat, idle timeout, and queue limits. Native `sloppy run`
supports HTTP/1.1 Upgrade plus text and binary frame delivery for the same
route, but does not model the full TestHost helper surface.
