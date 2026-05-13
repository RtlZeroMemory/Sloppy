# WebSocket TestHost

Plain JavaScript TestHost example for the WebSocket echo app.

```powershell
node examples/websocket-testhost/test.mjs
```

The test uses the app-host lane. Artifact, package, and loopback TestHost lanes
report `SLOPPY_E_TESTHOST_WEBSOCKET_UNSUPPORTED` unless a supplied runtime host
implements `websocketConnect`.
