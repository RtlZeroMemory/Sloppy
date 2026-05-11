import { Text } from "./codec.js";
import { Results } from "./results.js";

const DEFAULT_QUEUE_LIMIT = 64;
const EVENT_NAME_PATTERN = /^[A-Za-z0-9_.:-]+$/u;

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
                    closed = true;
                    writer.close();
                },
            });

            await userHandler(ctx, stream);
            stream.close();
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

function createHub(name) {
    if (typeof name !== "string" || name.length === 0) {
        throw new TypeError("Sloppy Realtime.hub name must be a non-empty string.");
    }
    const connections = new Map();
    const groups = new Map();

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
                if (client === undefined) {
                    return false;
                }
                client.messages.push({ type: "text", text: String(text) });
                return true;
            },
            sendJson(value) {
                if (client === undefined) {
                    return false;
                }
                client.messages.push({ type: "json", json: value });
                return true;
            },
            close(code = 1000, reason = "") {
                if (client === undefined) {
                    return false;
                }
                client.closed = true;
                client.close = { code, reason: String(reason) };
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
            const connectionId = id ?? `${name}:${connections.size + 1}`;
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
                    client.messages.push({ type: "text", text: String(text) });
                },
                sendJson(value) {
                    client.messages.push({ type: "json", json: value });
                },
                close(code = 1000, reason = "") {
                    client.closed = true;
                    client.close = { code, reason: String(reason) };
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
                    for (const id of members) {
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
                    messages: Object.freeze([...client.messages]),
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

function websocket(handler) {
    return createWebSocketRouteHandler(handler);
}

export function createSseRouteHandler(handler, options = undefined) {
    return createSseStream(handler, options);
}

export function createWebSocketRouteHandler(handler) {
    if (typeof handler !== "function") {
        throw new TypeError("Sloppy WebSocket route handler must be a function.");
    }
    return createUnavailableWebSocketHandler();
}

export const Realtime = Object.freeze({
    sse,
    websocket,
    hub: createHub,
    textBytes(value) {
        return Text.utf8.encode(String(value));
    },
});
