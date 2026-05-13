# WebSocket JSON Schema

Schema-validated JSON WebSocket messages for app-host tests. Invalid messages
fail through the handler error path and close the socket with an internal-error
close code.
