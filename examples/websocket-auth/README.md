# WebSocket Auth

Protected WebSocket route using JWT bearer auth and a required `realtime`
scope. Test it with `host.websocket("/secure/ws").withJwt(...)`.

Native WebSocket routes fail closed when the route requires auth until the auth
principal bridge is attached to upgraded connections. Use TestHost for this
protected-route example.
