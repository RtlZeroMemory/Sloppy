import { Text } from "./codec.js";
import { Results } from "./results.js";

const DEFAULT_QUEUE_LIMIT = 64;
const EVENT_NAME_PATTERN = /^[A-Za-z0-9_.:-]+$/u;
const WEBSOCKET_PROTOCOL_PATTERN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/u;
const DEFAULT_WEBSOCKET_MAX_MESSAGE_BYTES = 64 * 1024;
const DEFAULT_WEBSOCKET_MAX_SEND_QUEUE_BYTES = 1024 * 1024;
const DEFAULT_WEBSOCKET_CLOSE_TIMEOUT_MS = 5000;
const WEBSOCKET_ROUTE_HANDLER = Symbol.for("sloppy.websocket.routeHandler");
const WEBSOCKET_ROUTE_OPTIONS = Symbol.for("sloppy.websocket.routeOptions");

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }
    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function validateEventName(name) {
    if (typeof name !== "string" || name.length === 0 || !EVENT_NAME_PATTERN.test(name)) {
        throw new TypeError("Sloppy SSE event names must be non-empty token strings.");
    }
}

function validateFieldText(value, subject) {
    const text = String(value);
    if (/[\r\n]/u.test(text)) {
        throw new TypeError(`Sloppy SSE ${subject} must not contain CR or LF.`);
    }
    return text;
}

function appendDataLines(lines, value) {
    const text = typeof value === "string" ? value : JSON.stringify(value);
    for (const line of String(text).split("\n")) {
        lines.push(`data: ${line}`);
    }
}

function sseFrame(data, options = undefined) {
    const lines = [];
    if (options !== undefined && !isPlainObject(options)) {
        throw new TypeError("Sloppy SSE event options must be a plain object.");
    }
    if (options?.comment !== undefined) {
        lines.push(`: ${validateFieldText(options.comment, "comment")}`);
    }
    if (options?.event !== undefined) {
        validateEventName(options.event);
        lines.push(`event: ${options.event}`);
    }
    if (options?.id !== undefined) {
        lines.push(`id: ${validateFieldText(options.id, "id")}`);
    }
    if (options?.retry !== undefined) {
        if (!Number.isInteger(options.retry) || options.retry < 0) {
            throw new TypeError("Sloppy SSE retry must be a non-negative integer.");
        }
        lines.push(`retry: ${options.retry}`);
    }
    appendDataLines(lines, data);
    lines.push("", "");
    return lines.join("\n");
}

function createSseStream(userHandler, options = undefined) {
    if (typeof userHandler !== "function") {
        throw new TypeError("Sloppy Realtime.sse handler must be a function.");
    }
    if (options !== undefined && !isPlainObject(options)) {
        throw new TypeError("Sloppy Realtime.sse options must be a plain object.");
    }

    return async function sloppySseHandler(ctx) {
        let closed = false;
        let queued = 0;
        const maxQueuedEvents = options?.maxQueuedEvents ?? DEFAULT_QUEUE_LIMIT;
        if (!Number.isInteger(maxQueuedEvents) || maxQueuedEvents <= 0) {
            throw new TypeError("Sloppy Realtime.sse maxQueuedEvents must be a positive integer.");
        }

        return Results.stream(async (writer) => {
            function writeFrame(frame) {
                if (closed) {
                    throw new TypeError("Sloppy SSE stream is closed.");
                }
                if (queued >= maxQueuedEvents) {
                    throw new TypeError("Sloppy SSE bounded write queue is full.");
                }
                queued += 1;
                writer.writeText(frame);
            }

            const stream = Object.freeze({
                send(data) {
                    writeFrame(sseFrame(data));
                },
                event(name, data, eventOptions = undefined) {
                    validateEventName(name);
                    writeFrame(sseFrame(data, { ...(eventOptions ?? {}), event: name }));
                },
                comment(text) {
                    writeFrame(`: ${validateFieldText(text, "comment")}\n\n`);
                },
                heartbeat() {
                    writeFrame(": heartbeat\n\n");
                },
                close() {
                    if (closed) {
                        return;
                    }
                    closed = true;
                    writer.close();
                },
            });

            await userHandler(ctx, stream);
            if (!closed) {
                stream.close();
            }
        }, {
            contentType: "text/event-stream",
            headers: {
                "Cache-Control": "no-cache",
                "X-Slop-Realtime": "sse",
            },
        });
    };
}

function createUnavailableWebSocketHandler() {
    return function sloppyWebSocketUnavailable() {
        return Results.problem({
            status: 501,
            title: "WebSocket runtime is not available",
            code: "SLOPPY_E_REALTIME_WEBSOCKET_UNAVAILABLE",
        }, {
            status: 501,
            headers: {
                "X-Slop-Realtime": "websocket",
            },
        });
    };
}

function positiveIntegerOption(value, name, defaultValue = undefined) {
    if (value === undefined) {
        return defaultValue;
    }
    if (!Number.isInteger(value) || value <= 0) {
        throw new TypeError(`Sloppy WebSocket ${name} must be a positive integer.`);
    }
    return value;
}

function validateProtocolToken(value) {
    if (typeof value !== "string" || value.length === 0 || !WEBSOCKET_PROTOCOL_PATTERN.test(value)) {
        throw new TypeError("Sloppy WebSocket protocols must be non-empty WebSocket subprotocol tokens.");
    }
    return value;
}

function normalizeWebSocketProtocols(value) {
    if (value === undefined) {
        return Object.freeze([]);
    }
    if (!Array.isArray(value)) {
        throw new TypeError("Sloppy WebSocket protocols must be an array when provided.");
    }
    return Object.freeze([...new Set(value.map(validateProtocolToken))]);
}

function normalizeWebSocketOrigins(value) {
    if (value === undefined) {
        return undefined;
    }
    if (value === "*") {
        return "*";
    }
    if (typeof value === "string") {
        if (value.length === 0) {
            throw new TypeError("Sloppy WebSocket origins must be non-empty strings.");
        }
        return Object.freeze([value]);
    }
    if (!Array.isArray(value) || value.length === 0) {
        throw new TypeError("Sloppy WebSocket origins must be '*', a string, or a non-empty string array.");
    }
    for (const origin of value) {
        if (typeof origin !== "string" || origin.length === 0) {
            throw new TypeError("Sloppy WebSocket origins must be non-empty strings.");
        }
        if (origin === "*" && value.length !== 1) {
            throw new TypeError("Sloppy WebSocket '*' origin cannot be combined with explicit origins.");
        }
    }
    return value[0] === "*" ? "*" : Object.freeze([...new Set(value)]);
}

export function normalizeWebSocketRouteOptions(options = undefined) {
    if (options === undefined) {
        return Object.freeze({
            protocols: Object.freeze([]),
            maxMessageBytes: DEFAULT_WEBSOCKET_MAX_MESSAGE_BYTES,
            maxSendQueueBytes: DEFAULT_WEBSOCKET_MAX_SEND_QUEUE_BYTES,
            closeTimeoutMs: DEFAULT_WEBSOCKET_CLOSE_TIMEOUT_MS,
            compression: false,
            slowClientPolicy: "error",
        });
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy WebSocket options must be a plain object.");
    }
    if (options.compression !== undefined && options.compression !== false) {
        throw new TypeError("Sloppy WebSocket compression is not supported by this runtime.");
    }
    const slowClientPolicy = options.slowClientPolicy ?? "error";
    if (slowClientPolicy !== "error" && slowClientPolicy !== "close") {
        throw new TypeError("Sloppy WebSocket slowClientPolicy must be 'error' or 'close'.");
    }
    const heartbeatMs = positiveIntegerOption(options.heartbeatMs, "heartbeatMs");
    const idleTimeoutMs = positiveIntegerOption(options.idleTimeoutMs, "idleTimeoutMs");
    return Object.freeze({
        protocols: normalizeWebSocketProtocols(options.protocols),
        origins: normalizeWebSocketOrigins(options.origins),
        maxMessageBytes: positiveIntegerOption(
            options.maxMessageBytes,
            "maxMessageBytes",
            DEFAULT_WEBSOCKET_MAX_MESSAGE_BYTES,
        ),
        maxSendQueueBytes: positiveIntegerOption(
            options.maxSendQueueBytes,
            "maxSendQueueBytes",
            DEFAULT_WEBSOCKET_MAX_SEND_QUEUE_BYTES,
        ),
        ...(heartbeatMs === undefined ? {} : { heartbeatMs }),
        ...(idleTimeoutMs === undefined ? {} : { idleTimeoutMs }),
        closeTimeoutMs: positiveIntegerOption(
            options.closeTimeoutMs,
            "closeTimeoutMs",
            DEFAULT_WEBSOCKET_CLOSE_TIMEOUT_MS,
        ),
        compression: false,
        slowClientPolicy,
    });
}

export function webSocketRouteOptions(handler) {
    return handler?.[WEBSOCKET_ROUTE_OPTIONS];
}

export function webSocketUserHandler(handler) {
    return handler?.[WEBSOCKET_ROUTE_HANDLER];
}

function createHub(name) {
    if (typeof name !== "string" || name.length === 0) {
        throw new TypeError("Sloppy Realtime.hub name must be a non-empty string.");
    }
    const connections = new Map();
    const groups = new Map();
    let nextConnectionId = 1;

    function deepFreeze(value) {
        if (value === null || typeof value !== "object" || Object.isFrozen(value)) {
            return value;
        }
        for (const child of Object.values(value)) {
            deepFreeze(child);
        }
        return Object.freeze(value);
    }

    function snapshotJson(value) {
        if (value === undefined) {
            return undefined;
        }
        return deepFreeze(JSON.parse(JSON.stringify(value)));
    }

    function snapshotMessage(message) {
        if (message.type === "json") {
            return deepFreeze({ type: "json", json: snapshotJson(message.json) });
        }
        return deepFreeze({ type: message.type, text: message.text });
    }

    function ensureGroup(groupName) {
        if (typeof groupName !== "string" || groupName.length === 0) {
            throw new TypeError("Sloppy realtime group name must be a non-empty string.");
        }
        let group = groups.get(groupName);
        if (group === undefined) {
            group = new Set();
            groups.set(groupName, group);
        }
        return group;
    }

    function connection(id) {
        const client = connections.get(id);
        return Object.freeze({
            sendText(text) {
                if (client === undefined || client.closed) {
                    return false;
                }
                client.messages.push(deepFreeze({ type: "text", text: String(text) }));
                return true;
            },
            sendJson(value) {
                if (client === undefined || client.closed) {
                    return false;
                }
                client.messages.push(deepFreeze({ type: "json", json: snapshotJson(value) }));
                return true;
            },
            close(code = 1000, reason = "") {
                if (client === undefined || client.closed) {
                    return false;
                }
                client.closed = true;
                client.close = { code, reason: String(reason) };
                hub.unregister(id);
                return true;
            },
        });
    }

    async function sendTo(ids, kind, value) {
        for (const id of ids) {
            const target = connection(id);
            if (kind === "json") {
                target.sendJson(value);
            } else {
                target.sendText(value);
            }
        }
    }

    const hub = {
        name,
        socket(handler) {
            if (typeof handler !== "function") {
                throw new TypeError("Sloppy Realtime.hub socket handler must be a function.");
            }
            return createUnavailableWebSocketHandler();
        },
        register(id = undefined) {
            const connectionId = id ?? `${name}:${nextConnectionId++}`;
            if (connections.has(connectionId)) {
                throw new Error(`Sloppy realtime connection '${connectionId}' is already registered.`);
            }
            const client = { id: connectionId, groups: new Set(), messages: [], closed: false };
            connections.set(connectionId, client);
            return Object.freeze({
                id: connectionId,
                join(groupName) {
                    ensureGroup(groupName).add(connectionId);
                    client.groups.add(groupName);
                },
                leave(groupName) {
                    groups.get(groupName)?.delete(connectionId);
                    client.groups.delete(groupName);
                },
                sendText(text) {
                    if (client.closed || connections.get(connectionId) !== client) {
                        return false;
                    }
                    client.messages.push(deepFreeze({ type: "text", text: String(text) }));
                    return true;
                },
                sendJson(value) {
                    if (client.closed || connections.get(connectionId) !== client) {
                        return false;
                    }
                    client.messages.push(deepFreeze({ type: "json", json: snapshotJson(value) }));
                    return true;
                },
                close(code = 1000, reason = "") {
                    if (client.closed || connections.get(connectionId) !== client) {
                        return false;
                    }
                    client.closed = true;
                    client.close = { code, reason: String(reason) };
                    hub.unregister(connectionId);
                    return true;
                },
            });
        },
        unregister(id) {
            const client = connections.get(id);
            if (client === undefined) {
                return false;
            }
            for (const groupName of client.groups) {
                groups.get(groupName)?.delete(id);
            }
            connections.delete(id);
            return true;
        },
        connection,
        group(groupName) {
            const members = ensureGroup(groupName);
            return Object.freeze({
                sendText(text) {
                    return sendTo(members, "text", String(text));
                },
                sendJson(value) {
                    return sendTo(members, "json", value);
                },
                close(code = 1001, reason = "server shutdown") {
                    for (const id of [...members]) {
                        connection(id).close(code, reason);
                    }
                },
            });
        },
        broadcastText(text) {
            return sendTo(connections.keys(), "text", String(text));
        },
        broadcastJson(value) {
            return sendTo(connections.keys(), "json", value);
        },
        __debug() {
            return Object.freeze({
                connections: Object.freeze([...connections.values()].map((client) => Object.freeze({
                    id: client.id,
                    groups: Object.freeze([...client.groups]),
                    messages: Object.freeze(client.messages.map(snapshotMessage)),
                    closed: client.closed,
                    close: client.close,
                }))),
                groups: Object.freeze([...groups.entries()].map(([groupName, ids]) => Object.freeze({
                    name: groupName,
                    connections: Object.freeze([...ids]),
                }))),
            });
        },
    };

    return Object.freeze(hub);
}

function sse(handler, options = undefined) {
    return createSseStream(handler, options);
}

function websocket(handler, options = undefined) {
    return createWebSocketRouteHandler(handler, options);
}

export function createSseRouteHandler(handler, options = undefined) {
    return createSseStream(handler, options);
}

export function createWebSocketRouteHandler(handler, options = undefined) {
    if (typeof handler !== "function") {
        throw new TypeError("Sloppy WebSocket route handler must be a function.");
    }
    const routeOptions = normalizeWebSocketRouteOptions(options);
    function sloppyWebSocketRoute(ctx) {
        if (ctx?.__sloppyWebSocketHandshake === true && ctx.__sloppyWebSocket !== undefined) {
            ctx.__sloppyWebSocket.__setContext?.(ctx);
            if (handler.length >= 2) {
                return handler(ctx, ctx.__sloppyWebSocket);
            }
            return handler(ctx.__sloppyWebSocket);
        }
        return createUnavailableWebSocketHandler()();
    }
    Object.defineProperties(sloppyWebSocketRoute, {
        [WEBSOCKET_ROUTE_HANDLER]: {
            value: handler,
        },
        [WEBSOCKET_ROUTE_OPTIONS]: {
            value: routeOptions,
        },
    });
    return sloppyWebSocketRoute;
}

export const Realtime = Object.freeze({
    sse,
    websocket,
    hub: createHub,
    textBytes(value) {
        return Text.utf8.encode(String(value));
    },
});
