# WebSocket Auth

Protected WebSocket route using JWT bearer auth and a required `realtime`
scope. Test it with `host.websocket("/secure/ws").withJwt(...)`.

Native runtime WebSocket upgrade execution is unavailable in this alpha.
