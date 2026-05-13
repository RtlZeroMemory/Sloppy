# WebSocket TestHost

Plain JavaScript TestHost example for the WebSocket echo app.

```powershell
node examples/websocket-testhost/test.mjs
```

The test uses the app-host lane. Artifact, package, and loopback lanes report
`SLOPPY_E_TESTHOST_WEBSOCKET_UNSUPPORTED` until native upgrade support exists.
