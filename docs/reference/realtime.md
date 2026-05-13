# Realtime Reference

## Public API

| API | Status |
| --- | --- |
| `Realtime.channel(name, definition)` | Creates an immutable typed channel descriptor. |
| `Realtime.event(schema)` | Wraps an event schema with optional auth metadata. |
| `Realtime.backplane.memory()` | Creates the in-process backplane. |
| `app.realtime(pattern, channel, handler, options?)` | Registers a high-level realtime route. |
| `group.realtime(pattern, channel, handler, options?)` | Registers a grouped realtime route. |
| `TestHost.create(app).realtime(target, channel)` | Creates a typed app-host realtime client. |
| `Health.realtime(backplane)` | Reports memory/custom backplane health. |
| `SloppyRealtimeError` | Error type for protocol, validation, auth, and backplane failures. |

## Error Codes

| Code | Meaning |
| --- | --- |
| `SLOPPY_E_REALTIME_MALFORMED_JSON` | The incoming text message was not valid JSON. |
| `SLOPPY_E_REALTIME_MALFORMED_ENVELOPE` | The envelope was not `{ type, data, id? }`. |
| `SLOPPY_E_REALTIME_UNKNOWN_EVENT` | The client event is not registered or has no handler. |
| `SLOPPY_E_REALTIME_VALIDATION_FAILED` | Event payload validation failed. |
| `SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT` | Per-message auth rejected the event. |
| `SLOPPY_E_REALTIME_BACKPLANE_ERROR` | The configured backplane failed. |
| `SLOPPY_E_REALTIME_CLOSED_CONNECTION` | A send targeted a closed connection. |
| `SLOPPY_E_REALTIME_PRESENCE_DISABLED` | Presence was used on a route without `presence: true`. |

## Test Lanes

| Lane | Realtime support |
| --- | --- |
| `TestHost.create(app)` | High-level realtime, groups, presence, auth, metrics, and error envelopes. |
| `TestHost.fromArtifacts(...)` | Same WebSocket boundary as raw WebSockets; unsupported unless a runtime host supplies `websocketConnect`. |
| `TestHost.fromPackage(...)` | Same WebSocket boundary as raw WebSockets; unsupported unless a runtime host supplies `websocketConnect`. |
| Native `sloppy run` | Public V8-backed realtime routes use the generated runtime wrapper over the raw WebSocket backend; protected realtime follows the native auth-principal limitation. |

## Metadata

Plan, routes, and OpenAPI output expose high-level realtime route metadata as
partial alpha metadata. The emitted contract includes transport and the
channel/options expression text; static event names, per-event auth, and schema
shapes are not yet presented as a complete machine-readable contract.

## Backplane Method Shape

Custom backplanes implement:

- `connect(connection)`
- `disconnect(connectionId)`
- `join(connectionId, group)`
- `leave(connectionId, group)`
- `leaveAll(connectionId)`
- `groups(connectionId)`
- `groupSize(group)`
- `broadcast(group, envelope, options)`
- `send(connectionId, envelope)`
- `presenceSet(connectionId, record)`
- `presenceGet(connectionId)`
- `presenceInGroup(group)`
- `dispose()`

The built-in memory backplane is single-process. Distributed membership,
distributed presence, fan-out, server instance IDs, cleanup TTLs, and Redis
pub/sub are reserved for a separate backplane implementation.
