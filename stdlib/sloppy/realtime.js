import { Text } from "./codec.js";
import { Results } from "./results.js";
import { Schema, isSchema, isValidationError } from "./schema.js";

const DEFAULT_QUEUE_LIMIT = 64;
const EVENT_NAME_PATTERN = /^[A-Za-z0-9_.:-]+$/u;
const REALTIME_IDENTIFIER_PATTERN = /^[A-Za-z][0-9A-Za-z_.:-]*$/u;
const WEBSOCKET_PROTOCOL_PATTERN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/u;
const WEBSOCKET_PROTOCOL_UNSAFE_PATTERN = /[^!#$%&'*+\-.^_`|~0-9A-Za-z]/gu;
const DEFAULT_WEBSOCKET_MAX_MESSAGE_BYTES = 64 * 1024;
const DEFAULT_WEBSOCKET_MAX_SEND_QUEUE_BYTES = 1024 * 1024;
const DEFAULT_WEBSOCKET_CLOSE_TIMEOUT_MS = 5000;
const WEBSOCKET_ROUTE_HANDLER = Symbol.for("sloppy.websocket.routeHandler");
const WEBSOCKET_ROUTE_OPTIONS = Symbol.for("sloppy.websocket.routeOptions");
const REALTIME_CHANNEL = Symbol.for("sloppy.realtime.channel");
const REALTIME_EVENT = Symbol.for("sloppy.realtime.event");
const REALTIME_ROUTE_METADATA = Symbol.for("sloppy.realtime.routeMetadata");
const RESERVED_REALTIME_EVENT_NAMES = new Set([
    "connect",
    "disconnect",
    "error",
    "ping",
    "pong",
    "join",
    "leave",
    "system",
]);
const MAX_ERROR_ISSUES = 8;
const MAX_ERROR_TEXT = 160;
const MAX_GROUP_NAME_LENGTH = 256;
const MAX_PRESENCE_METADATA_BYTES = 4096;

export class SloppyRealtimeError extends Error {
    constructor(code, message, options = undefined) {
        super(message);
        this.name = "SloppyRealtimeError";
        this.code = code;
        this.event = options?.event;
        this.issues = options?.issues === undefined
            ? Object.freeze([])
            : Object.freeze([...options.issues]);
        this.closeCode = options?.closeCode;
        this.__sloppyRealtimeError = true;
    }
}

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }
    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function deepFreeze(value) {
    if (value === null || typeof value !== "object" || Object.isFrozen(value)) {
        return value;
    }
    for (const child of Object.values(value)) {
        deepFreeze(child);
    }
    return Object.freeze(value);
}

function assertRealtimeIdentifier(value, subject) {
    if (typeof value !== "string" || value.length === 0 || !REALTIME_IDENTIFIER_PATTERN.test(value)) {
        throw new TypeError(`Sloppy Realtime ${subject} must be a stable identifier.`);
    }
}

function assertRealtimeEventName(value, subject = "event name") {
    assertRealtimeIdentifier(value, subject);
    if (RESERVED_REALTIME_EVENT_NAMES.has(value)) {
        throw new TypeError(`Sloppy Realtime event name '${value}' is reserved.`);
    }
}

function sanitizeIssueText(value) {
    const text = String(value ?? "");
    return text.length <= MAX_ERROR_TEXT ? text : `${text.slice(0, MAX_ERROR_TEXT)}...`;
}

function sanitizeValidationIssues(issues) {
    return Object.freeze((issues ?? []).slice(0, MAX_ERROR_ISSUES).map((issue) => Object.freeze({
        path: Object.freeze((issue.path ?? []).slice(0, 8).map((part) => sanitizeIssueText(part))),
        code: sanitizeIssueText(issue.code ?? "invalid"),
        message: sanitizeIssueText(issue.message ?? "Validation failed."),
    })));
}

function realtimeErrorEnvelope(error, event = undefined) {
    const code = error instanceof SloppyRealtimeError
        ? error.code
        : "SLOPPY_E_REALTIME_HANDLER_ERROR";
    return deepFreeze({
        type: "error",
        error: {
            code,
            message: error instanceof SloppyRealtimeError
                ? error.message
                : "Realtime message handling failed.",
            ...(event ?? error?.event ? { event: event ?? error.event } : {}),
            ...((error instanceof SloppyRealtimeError && error.issues.length !== 0)
                ? { issues: sanitizeValidationIssues(error.issues) }
                : {}),
        },
    });
}

function normalizeRealtimeAuth(auth = undefined) {
    if (auth === undefined) {
        return undefined;
    }
    return deepFreeze({
        required: auth.required === true,
        scopes: Object.freeze([...(auth.scopes ?? [])]),
        roles: Object.freeze([...(auth.roles ?? [])]),
        policy: auth.policy,
    });
}

function createRealtimeEvent(schemaValue, auth = undefined) {
    if (!isSchema(schemaValue)) {
        throw new TypeError("Sloppy Realtime event schema must be a Sloppy schema.");
    }
    const normalizedAuth = normalizeRealtimeAuth(auth);
    const event = {
        [REALTIME_EVENT]: true,
        schema: schemaValue,
        metadata: deepFreeze({
            schema: schemaValue.metadata,
            ...(normalizedAuth === undefined ? {} : { auth: normalizedAuth }),
        }),
        validate(value) {
            return Schema.validate(value, schemaValue);
        },
        requiresAuth() {
            return createRealtimeEvent(schemaValue, {
                ...(normalizedAuth ?? {}),
                required: true,
            });
        },
        requiresScope(...scopes) {
            for (const scope of scopes) {
                assertRealtimeIdentifier(scope, "event authorization scope");
            }
            return createRealtimeEvent(schemaValue, {
                ...(normalizedAuth ?? {}),
                required: true,
                scopes: Object.freeze([...new Set([...(normalizedAuth?.scopes ?? []), ...scopes])]),
            });
        },
        requiresRole(...roles) {
            for (const role of roles) {
                assertRealtimeIdentifier(role, "event authorization role");
            }
            return createRealtimeEvent(schemaValue, {
                ...(normalizedAuth ?? {}),
                required: true,
                roles: Object.freeze([...new Set([...(normalizedAuth?.roles ?? []), ...roles])]),
            });
        },
        authorize(policy) {
            assertRealtimeIdentifier(policy, "event authorization policy");
            return createRealtimeEvent(schemaValue, {
                ...(normalizedAuth ?? {}),
                required: true,
                policy,
            });
        },
    };
    return Object.freeze(event);
}

function isRealtimeEvent(value) {
    return value !== null && typeof value === "object" && value[REALTIME_EVENT] === true;
}

function normalizeEventDescriptor(value, subject) {
    if (isRealtimeEvent(value)) {
        return value;
    }
    if (isSchema(value)) {
        return createRealtimeEvent(value);
    }
    throw new TypeError(`Sloppy Realtime ${subject} must be a Sloppy schema or Realtime.event(...).`);
}

function normalizeEventMap(value, subject) {
    if (value === undefined) {
        return Object.freeze({});
    }
    if (!isPlainObject(value)) {
        throw new TypeError(`Sloppy Realtime ${subject} events must be a plain object.`);
    }
    const out = {};
    for (const [name, descriptor] of Object.entries(value)) {
        assertRealtimeEventName(name, `${subject} event name`);
        out[name] = normalizeEventDescriptor(descriptor, `${subject}.${name}`);
    }
    return Object.freeze(out);
}

function eventMetadataMap(events) {
    const out = {};
    for (const [name, descriptor] of Object.entries(events)) {
        out[name] = descriptor.metadata;
    }
    return deepFreeze(out);
}

function assertNoDuplicateEventNames(client, server) {
    const duplicates = Object.keys(client).filter((name) => Object.hasOwn(server, name));
    if (duplicates.length !== 0) {
        throw new TypeError(`Sloppy Realtime event '${duplicates[0]}' cannot be both a client and server event.`);
    }
}

function normalizeEnvelope(value, direction) {
    let envelope = value;
    if (typeof value === "string") {
        try {
            envelope = JSON.parse(value);
        } catch {
            throw new SloppyRealtimeError(
                "SLOPPY_E_REALTIME_MALFORMED_JSON",
                "Realtime message must be valid JSON.",
                { closeCode: 1003 },
            );
        }
    }
    if (!isPlainObject(envelope) ||
        typeof envelope.type !== "string" ||
        envelope.type.length === 0 ||
        !Object.hasOwn(envelope, "data") ||
        (envelope.id !== undefined && typeof envelope.id !== "string"))
    {
        throw new SloppyRealtimeError(
            "SLOPPY_E_REALTIME_MALFORMED_ENVELOPE",
            `Realtime ${direction} message envelope is invalid.`,
            { closeCode: 1003 },
        );
    }
    return envelope;
}

function validateEnvelopeEvent(events, envelope, direction) {
    const descriptor = events[envelope.type];
    if (descriptor === undefined) {
        throw new SloppyRealtimeError(
            "SLOPPY_E_REALTIME_UNKNOWN_EVENT",
            `Realtime ${direction} event is not registered.`,
            { event: envelope.type, closeCode: 1008 },
        );
    }
    try {
        return descriptor.validate(envelope.data);
    } catch (error) {
        if (isValidationError(error)) {
            throw new SloppyRealtimeError(
                "SLOPPY_E_REALTIME_VALIDATION_FAILED",
                "Realtime message validation failed.",
                { event: envelope.type, issues: sanitizeValidationIssues(error.issues), closeCode: 1007 },
            );
        }
        throw error;
    }
}

function validateNamedEvent(events, eventName, data, direction) {
    assertRealtimeEventName(eventName, `${direction} event name`);
    return validateEnvelopeEvent(events, { type: eventName, data }, direction);
}

function createEnvelope(type, data, id = undefined) {
    return deepFreeze({
        type,
        data,
        ...(id === undefined ? {} : { id }),
    });
}

function validateGroupName(name) {
    if (typeof name !== "string" ||
        name.length === 0 ||
        name.length > MAX_GROUP_NAME_LENGTH ||
        /[\x00-\x1F\x7F]/u.test(name))
    {
        throw new TypeError("Sloppy Realtime group names must be non-empty bounded strings without control characters.");
    }
    return name;
}

function safeUserId(user) {
    return typeof user?.sub === "string" && user.sub.length !== 0
        ? user.sub
        : typeof user?.id === "string" && user.id.length !== 0
            ? user.id
            : undefined;
}

function createMemoryRealtimeBackplane() {
    const connections = new Map();
    const groups = new Map();
    const presence = new Map();
    let disposed = false;

    function assertOpen() {
        if (disposed) {
            throw new SloppyRealtimeError(
                "SLOPPY_E_REALTIME_BACKPLANE_ERROR",
                "Realtime backplane is disposed.",
            );
        }
    }

    function groupMembers(name) {
        let members = groups.get(name);
        if (members === undefined) {
            members = new Set();
            groups.set(name, members);
        }
        return members;
    }

    function pruneGroup(name) {
        const members = groups.get(name);
        if (members !== undefined && members.size === 0) {
            groups.delete(name);
        }
    }

    function removeConnectionState(connectionId) {
        const connection = connections.get(connectionId);
        if (connection !== undefined) {
            for (const groupName of connection.groups) {
                groups.get(groupName)?.delete(connectionId);
                pruneGroup(groupName);
            }
        }
        connections.delete(connectionId);
        presence.delete(connectionId);
        return connection !== undefined;
    }

    function snapshotPresence(record) {
        return deepFreeze({
            connectionId: record.connectionId,
            userId: record.userId,
            groups: Object.freeze([...record.groups]),
            connectedAt: record.connectedAt,
            metadata: record.metadata,
        });
    }

    const backplane = {
        kind: "memory",
        connect(connection) {
            assertOpen();
            if (!isPlainObject(connection) || typeof connection.connectionId !== "string") {
                throw new TypeError("Sloppy Realtime backplane connection must include a connectionId.");
            }
            removeConnectionState(connection.connectionId);
            connections.set(connection.connectionId, {
                ...connection,
                groups: new Set(),
            });
            return Promise.resolve();
        },
        disconnect(connectionId) {
            if (disposed) {
                return Promise.resolve(false);
            }
            const connection = connections.get(connectionId);
            if (connection === undefined) {
                presence.delete(connectionId);
                return Promise.resolve(false);
            }
            removeConnectionState(connectionId);
            return Promise.resolve(true);
        },
        join(connectionId, groupName) {
            assertOpen();
            validateGroupName(groupName);
            const connection = connections.get(connectionId);
            if (connection === undefined) {
                throw new SloppyRealtimeError(
                    "SLOPPY_E_REALTIME_CLOSED_CONNECTION",
                    "Realtime connection is closed.",
                    { closeCode: 1001 },
                );
            }
            connection.groups.add(groupName);
            groupMembers(groupName).add(connectionId);
            return Promise.resolve({ count: groups.get(groupName)?.size ?? 0 });
        },
        leave(connectionId, groupName) {
            assertOpen();
            validateGroupName(groupName);
            const connection = connections.get(connectionId);
            connection?.groups.delete(groupName);
            groups.get(groupName)?.delete(connectionId);
            pruneGroup(groupName);
            return Promise.resolve({ count: groups.get(groupName)?.size ?? 0 });
        },
        leaveAll(connectionId) {
            assertOpen();
            const connection = connections.get(connectionId);
            if (connection === undefined) {
                return Promise.resolve({ count: 0 });
            }
            const count = connection.groups.size;
            for (const groupName of connection.groups) {
                groups.get(groupName)?.delete(connectionId);
                pruneGroup(groupName);
            }
            connection.groups.clear();
            return Promise.resolve({ count });
        },
        groups(connectionId) {
            assertOpen();
            return Promise.resolve(Object.freeze([...(connections.get(connectionId)?.groups ?? [])]));
        },
        groupSize(groupName) {
            assertOpen();
            validateGroupName(groupName);
            return Promise.resolve(groups.get(groupName)?.size ?? 0);
        },
        async send(connectionId, envelope) {
            assertOpen();
            const connection = connections.get(connectionId);
            if (connection === undefined) {
                return { count: 0 };
            }
            await connection.send(envelope);
            return { count: 1 };
        },
        async broadcast(groupName, envelope, options = undefined) {
            assertOpen();
            validateGroupName(groupName);
            const except = new Set(options?.except ?? []);
            if (options?.exceptSelf === true && typeof options.senderId === "string") {
                except.add(options.senderId);
            }
            let count = 0;
            for (const connectionId of [...(groups.get(groupName) ?? [])]) {
                if (except.has(connectionId)) {
                    continue;
                }
                const connection = connections.get(connectionId);
                if (connection === undefined) {
                    groups.get(groupName)?.delete(connectionId);
                    pruneGroup(groupName);
                    continue;
                }
                await connection.send(envelope);
                count += 1;
            }
            return { count };
        },
        presenceSet(connectionId, record) {
            assertOpen();
            const connection = connections.get(connectionId);
            if (connection === undefined) {
                throw new SloppyRealtimeError(
                    "SLOPPY_E_REALTIME_CLOSED_CONNECTION",
                    "Realtime connection is closed.",
                    { closeCode: 1001 },
                );
            }
            const metadata = record?.metadata ?? {};
            const encoded = JSON.stringify(metadata);
            if (encoded === undefined || Text.utf8.encode(encoded).byteLength > MAX_PRESENCE_METADATA_BYTES) {
                throw new TypeError("Sloppy Realtime presence metadata must be bounded JSON.");
            }
            presence.set(connectionId, {
                connectionId,
                userId: record?.userId ?? connection.userId,
                groups: new Set(connection.groups),
                connectedAt: record?.connectedAt ?? connection.connectedAt,
                metadata: deepFreeze(JSON.parse(encoded)),
            });
            return Promise.resolve(snapshotPresence(presence.get(connectionId)));
        },
        presenceGet(connectionId) {
            assertOpen();
            const record = presence.get(connectionId);
            return Promise.resolve(record === undefined ? undefined : snapshotPresence(record));
        },
        presenceInGroup(groupName) {
            assertOpen();
            validateGroupName(groupName);
            const records = [];
            for (const connectionId of groups.get(groupName) ?? []) {
                const record = presence.get(connectionId);
                if (record !== undefined) {
                    record.groups = new Set(connections.get(connectionId)?.groups ?? record.groups);
                    records.push(snapshotPresence(record));
                }
            }
            return Promise.resolve(Object.freeze(records));
        },
        dispose() {
            disposed = true;
            connections.clear();
            groups.clear();
            presence.clear();
            return Promise.resolve();
        },
        health() {
            return Object.freeze({
                status: disposed ? "unhealthy" : "healthy",
                connections: connections.size,
                groups: groups.size,
            });
        },
    };
    return Object.freeze(backplane);
}

function defaultRealtimeProtocol(name) {
    return `sloppy.realtime.${name.replace(WEBSOCKET_PROTOCOL_UNSAFE_PATTERN, "-")}.v1`;
}

function validateBackplane(backplane) {
    const required = [
        "connect",
        "disconnect",
        "join",
        "leave",
        "leaveAll",
        "broadcast",
        "send",
        "presenceSet",
        "presenceGet",
        "presenceInGroup",
        "dispose",
    ];
    for (const method of required) {
        if (typeof backplane?.[method] !== "function") {
            throw new TypeError(`Sloppy Realtime backplane must implement ${method}().`);
        }
    }
    return backplane;
}

function incrementMetric(ctx, name, labels = undefined, value = 1) {
    const metrics = ctx?.__sloppyTestHostMetrics ?? ctx?.metrics;
    metrics?.increment?.(name, labels, value);
}

function gaugeMetric(ctx, name, labels = undefined, value = 0) {
    const metrics = ctx?.__sloppyTestHostMetrics ?? ctx?.metrics;
    metrics?.gauge?.(name, labels, value);
}

function authPolicyFromHandlerOptions(options = undefined) {
    if (options === undefined) {
        return undefined;
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Realtime ctx.on options must be a plain object.");
    }
    return deepFreeze({
        required: options.requiresAuth === true || options.required === true,
        scopes: Object.freeze([
            ...((typeof options.requiresScope === "string") ? [options.requiresScope] : []),
            ...((Array.isArray(options.requiresScope)) ? options.requiresScope : []),
            ...((Array.isArray(options.scopes)) ? options.scopes : []),
        ]),
        roles: Object.freeze([
            ...((typeof options.requiresRole === "string") ? [options.requiresRole] : []),
            ...((Array.isArray(options.requiresRole)) ? options.requiresRole : []),
            ...((Array.isArray(options.roles)) ? options.roles : []),
        ]),
        policy: options.policy,
    });
}

function mergeMessagePolicies(eventPolicy = undefined, handlerPolicy = undefined) {
    if (eventPolicy === undefined && handlerPolicy === undefined) {
        return undefined;
    }
    return deepFreeze({
        required: eventPolicy?.required === true || handlerPolicy?.required === true,
        scopes: Object.freeze([...new Set([...(eventPolicy?.scopes ?? []), ...(handlerPolicy?.scopes ?? [])])]),
        roles: Object.freeze([...new Set([...(eventPolicy?.roles ?? []), ...(handlerPolicy?.roles ?? [])])]),
        policy: eventPolicy?.policy,
    });
}

async function authorizeMessage(ctx, policy, eventName, resource = undefined) {
    if (policy === undefined) {
        return;
    }
    const user = ctx.user;
    if (policy.required === true && user?.authenticated !== true) {
        throw new SloppyRealtimeError(
            "SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT",
            "Realtime event requires an authenticated user.",
            { event: eventName, closeCode: 1008 },
        );
    }
    for (const scope of policy.scopes ?? []) {
        if (typeof user?.hasScope === "function" ? !user.hasScope(scope) : !(user?.scopes ?? []).includes(scope)) {
            throw new SloppyRealtimeError(
                "SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT",
                "Realtime event requires a missing scope.",
                { event: eventName, closeCode: 1008 },
            );
        }
    }
    for (const role of policy.roles ?? []) {
        if (typeof user?.hasRole === "function" ? !user.hasRole(role) : !(user?.roles ?? []).includes(role)) {
            throw new SloppyRealtimeError(
                "SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT",
                "Realtime event requires a missing role.",
                { event: eventName, closeCode: 1008 },
            );
        }
    }
    if (policy.policy !== undefined) {
        if (typeof ctx.authorize !== "function") {
            throw new SloppyRealtimeError(
                "SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT",
                "Realtime event authorization policy is unavailable in this runtime.",
                { event: eventName, closeCode: 1008 },
            );
        }
        if (await ctx.authorize(policy.policy, resource) !== true) {
            throw new SloppyRealtimeError(
                "SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT",
                "Realtime event authorization policy denied the message.",
                { event: eventName, closeCode: 1008 },
            );
        }
    }
}

function createChannel(name, definition) {
    assertRealtimeIdentifier(name, "channel name");
    if (!isPlainObject(definition)) {
        throw new TypeError("Sloppy Realtime channel definition must be a plain object.");
    }
    const client = normalizeEventMap(definition.client, "client");
    const server = normalizeEventMap(definition.server, "server");
    assertNoDuplicateEventNames(client, server);
    const metadata = deepFreeze({
        name,
        protocol: definition.protocol ?? defaultRealtimeProtocol(name),
        client: eventMetadataMap(client),
        server: eventMetadataMap(server),
    });
    const channel = {
        [REALTIME_CHANNEL]: true,
        name,
        client,
        server,
        metadata,
        validateClientEvent(eventName, data) {
            return validateNamedEvent(client, eventName, data, "client");
        },
        validateServerEvent(eventName, data) {
            return validateNamedEvent(server, eventName, data, "server");
        },
        parseClientMessage(value) {
            const envelope = normalizeEnvelope(value, "client");
            const data = validateEnvelopeEvent(client, envelope, "client");
            return createEnvelope(envelope.type, data, envelope.id);
        },
        serializeClientMessage(eventName, data, options = undefined) {
            const value = this.validateClientEvent(eventName, data);
            return createEnvelope(eventName, value, options?.id);
        },
        parseServerMessage(value) {
            const envelope = normalizeEnvelope(value, "server");
            const data = validateEnvelopeEvent(server, envelope, "server");
            return createEnvelope(envelope.type, data, envelope.id);
        },
        serializeServerMessage(eventName, data, options = undefined) {
            const value = this.validateServerEvent(eventName, data);
            return createEnvelope(eventName, value, options?.id);
        },
        stringifyClientMessage(eventName, data, options = undefined) {
            return JSON.stringify(this.serializeClientMessage(eventName, data, options));
        },
        stringifyServerMessage(eventName, data, options = undefined) {
            return JSON.stringify(this.serializeServerMessage(eventName, data, options));
        },
        errorEnvelope(error, event = undefined) {
            return realtimeErrorEnvelope(error, event);
        },
    };
    return deepFreeze(channel);
}

export function isRealtimeChannel(value) {
    return value !== null && typeof value === "object" && value[REALTIME_CHANNEL] === true;
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

export function realtimeRouteMetadata(handler) {
    return handler?.[REALTIME_ROUTE_METADATA];
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

function validateRealtimePolicy(value, name, allowed) {
    if (value === undefined) {
        return undefined;
    }
    if (!allowed.includes(value)) {
        throw new TypeError(`Sloppy Realtime ${name} must be ${allowed.map((entry) => `'${entry}'`).join(" or ")}.`);
    }
    return value;
}

function normalizeRealtimeRouteOptions(channel, options = undefined) {
    if (options !== undefined && !isPlainObject(options)) {
        throw new TypeError("Sloppy Realtime route options must be a plain object.");
    }
    const websocketOptions = normalizeWebSocketRouteOptions({
        protocols: options?.protocols ?? [channel.metadata.protocol],
        origins: options?.origins,
        maxMessageBytes: options?.maxMessageBytes,
        maxSendQueueBytes: options?.maxSendQueueBytes,
        heartbeatMs: options?.heartbeatMs,
        idleTimeoutMs: options?.idleTimeoutMs,
        closeTimeoutMs: options?.closeTimeoutMs,
        slowClientPolicy: options?.slowClientPolicy,
        compression: options?.compression,
    });
    const unknownEventPolicy = validateRealtimePolicy(
        options?.unknownEventPolicy,
        "unknownEventPolicy",
        ["error", "close"],
    ) ?? "error";
    const validationFailurePolicy = validateRealtimePolicy(
        options?.validationFailurePolicy,
        "validationFailurePolicy",
        ["error", "close"],
    ) ?? "error";
    const handlerErrorPolicy = validateRealtimePolicy(
        options?.handlerErrorPolicy,
        "handlerErrorPolicy",
        ["error", "close"],
    ) ?? "close";
    if (options?.presence !== undefined && typeof options.presence !== "boolean") {
        throw new TypeError("Sloppy Realtime presence option must be a boolean.");
    }
    const backplane = validateBackplane(options?.backplane ?? createMemoryRealtimeBackplane());
    return deepFreeze({
        websocket: websocketOptions,
        backplane,
        presence: options?.presence === true,
        backplaneKind: backplane.kind ?? "custom",
        unknownEventPolicy,
        validationFailurePolicy,
        handlerErrorPolicy,
    });
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

export function createRealtimeRouteHandler(channel, handler, options = undefined) {
    if (!isRealtimeChannel(channel)) {
        throw new TypeError("Sloppy app.realtime channel must come from Realtime.channel(...).");
    }
    if (typeof handler !== "function") {
        throw new TypeError("Sloppy app.realtime handler must be a function.");
    }
    const routeOptions = normalizeRealtimeRouteOptions(channel, options);
    let activeConnections = 0;
    const routeHandler = createWebSocketRouteHandler(async (ctx, socket) => {
        const backplane = routeOptions.backplane;
        const eventHandlers = new Map();
        let accepted = false;
        let cleanedUp = false;
        const connectionId = socket.id;
        const routePattern = ctx.routePattern ?? ctx.request?.path ?? "";
        const routeBroadcastGroup = `route:${channel.name}:${routePattern}`;
        const metricLabels = Object.freeze({
            route: routePattern,
            channel: channel.name,
        });

        async function cleanup() {
            if (cleanedUp) {
                return;
            }
            cleanedUp = true;
            await backplane.disconnect(connectionId);
            if (accepted && activeConnections > 0) {
                activeConnections -= 1;
                gaugeMetric(ctx, "realtime.connections.active", metricLabels, activeConnections);
            }
        }

        async function sendError(error, eventName = undefined) {
            const envelope = channel.errorEnvelope(error, eventName);
            incrementMetric(ctx, "realtime.errors.total", {
                ...metricLabels,
                code: envelope.error.code,
            });
            await socket.sendJson(envelope);
        }

        async function handleDispatchError(error, eventName = undefined) {
            const code = error instanceof SloppyRealtimeError
                ? error.code
                : "SLOPPY_E_REALTIME_HANDLER_ERROR";
            if (code === "SLOPPY_E_REALTIME_BACKPLANE_ERROR") {
                incrementMetric(ctx, "realtime.backplane.errors.total", metricLabels);
            }
            const policy = code === "SLOPPY_E_REALTIME_UNKNOWN_EVENT"
                ? routeOptions.unknownEventPolicy
                : code === "SLOPPY_E_REALTIME_VALIDATION_FAILED"
                    ? routeOptions.validationFailurePolicy
                    : code === "SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT"
                        ? "error"
                    : routeOptions.handlerErrorPolicy;
            if (policy === "error") {
                await sendError(error, eventName);
                return;
            }
            await socket.close(error?.closeCode ?? 1011, code);
        }

        async function send(eventName, data, sendOptions = undefined) {
            const envelope = channel.serializeServerMessage(eventName, data, sendOptions);
            await socket.sendJson(envelope);
            incrementMetric(ctx, "realtime.messages.out.total", {
                ...metricLabels,
                event: eventName,
                outcome: "sent",
            });
            return envelope;
        }

        function groupHandle(groupName) {
            validateGroupName(groupName);
            return Object.freeze({
                async sendTo(targetConnectionId, eventName, data, sendOptions = undefined) {
                    const envelope = channel.serializeServerMessage(eventName, data, sendOptions);
                    const result = await backplane.send(targetConnectionId, envelope);
                    incrementMetric(ctx, "realtime.messages.out.total", {
                        ...metricLabels,
                        event: eventName,
                        outcome: "group-send",
                    }, result.count);
                    return result;
                },
                async broadcast(eventName, data, broadcastOptions = undefined) {
                    const envelope = channel.serializeServerMessage(eventName, data, broadcastOptions);
                    const result = await backplane.broadcast(groupName, envelope, {
                        ...(broadcastOptions ?? {}),
                        senderId: connectionId,
                    });
                    incrementMetric(ctx, "realtime.groups.broadcast.total", {
                        ...metricLabels,
                        outcome: "ok",
                    });
                    incrementMetric(ctx, "realtime.messages.out.total", {
                        ...metricLabels,
                        event: eventName,
                        outcome: "broadcast",
                    }, result.count);
                    return result;
                },
            });
        }

        const realtimeContext = Object.freeze({
            socket,
            channel,
            params: ctx.params ?? ctx.route ?? Object.freeze({}),
            query: ctx.query ?? ctx.request?.query,
            headers: ctx.request?.headers,
            user: ctx.user,
            metrics: ctx.metrics,
            requireUser() {
                return ctx.requireUser();
            },
            services: ctx.services,
            async accept() {
                if (!accepted) {
                    await socket.accept();
                    accepted = true;
                    await backplane.connect({
                        connectionId,
                        userId: safeUserId(ctx.user),
                        connectedAt: new Date().toISOString(),
                        routePattern,
                        channel: channel.name,
                        async send(envelope) {
                            await socket.sendJson(envelope);
                        },
                    });
                    await backplane.join(connectionId, routeBroadcastGroup);
                    incrementMetric(ctx, "realtime.connections.total", metricLabels);
                    activeConnections += 1;
                    gaugeMetric(ctx, "realtime.connections.active", metricLabels, activeConnections);
                }
            },
            close(code = 1000, reason = "") {
                return socket.close(code, reason);
            },
            on(eventName, optionsOrHandler, maybeHandler = undefined) {
                assertRealtimeEventName(eventName, "client event name");
                const eventHandler = typeof optionsOrHandler === "function" ? optionsOrHandler : maybeHandler;
                if (typeof eventHandler !== "function") {
                    throw new TypeError("Sloppy Realtime ctx.on handler must be a function.");
                }
                if (eventHandlers.has(eventName)) {
                    throw new TypeError(`Sloppy Realtime client event '${eventName}' already has a handler.`);
                }
                const handlerPolicy = typeof optionsOrHandler === "function"
                    ? undefined
                    : authPolicyFromHandlerOptions(optionsOrHandler);
                eventHandlers.set(eventName, Object.freeze({
                    handler: eventHandler,
                    policy: handlerPolicy,
                }));
                return realtimeContext;
            },
            send,
            async broadcast(eventName, data, broadcastOptions = undefined) {
                const envelope = channel.serializeServerMessage(eventName, data, broadcastOptions);
                const result = await backplane.broadcast(routeBroadcastGroup, envelope, {
                    ...(broadcastOptions ?? {}),
                    senderId: connectionId,
                });
                incrementMetric(ctx, "realtime.messages.out.total", {
                    ...metricLabels,
                    event: eventName,
                    outcome: "broadcast",
                }, result.count);
                return result;
            },
            group: groupHandle,
            groups: Object.freeze({
                async join(groupName) {
                    const result = await backplane.join(connectionId, validateGroupName(groupName));
                    incrementMetric(ctx, "realtime.groups.join.total", metricLabels);
                    return result;
                },
                async leave(groupName) {
                    const result = await backplane.leave(connectionId, validateGroupName(groupName));
                    incrementMetric(ctx, "realtime.groups.leave.total", metricLabels);
                    return result;
                },
                list() {
                    return backplane.groups(connectionId).then((groups) => Object.freeze(
                        groups.filter((groupName) => groupName !== routeBroadcastGroup),
                    ));
                },
            }),
            presence: Object.freeze({
                async set(record) {
                    if (routeOptions.presence !== true) {
                        throw new SloppyRealtimeError(
                            "SLOPPY_E_REALTIME_PRESENCE_DISABLED",
                            "Realtime presence is not enabled for this route.",
                        );
                    }
                    const result = await backplane.presenceSet(connectionId, {
                        ...(record ?? {}),
                        userId: record?.userId ?? safeUserId(ctx.user),
                    });
                    incrementMetric(ctx, "realtime.presence.set.total", metricLabels);
                    return result;
                },
                get(targetConnectionId = connectionId) {
                    return backplane.presenceGet(targetConnectionId);
                },
                inGroup(groupName) {
                    return backplane.presenceInGroup(validateGroupName(groupName));
                },
            }),
            connectionId: socket.id,
        });

        try {
            await handler(realtimeContext);
            if (!accepted) {
                return undefined;
            }
            for await (const message of socket.messages()) {
                if (message.kind !== "json" && message.kind !== "text") {
                    await handleDispatchError(new SloppyRealtimeError(
                        "SLOPPY_E_REALTIME_MALFORMED_ENVELOPE",
                        "Realtime messages must be JSON envelopes.",
                        { closeCode: 1003 },
                    ));
                    continue;
                }
                let envelope;
                try {
                    envelope = channel.parseClientMessage(message.kind === "json" ? message.json() : message.text);
                    const registration = eventHandlers.get(envelope.type);
                    if (registration === undefined) {
                        throw new SloppyRealtimeError(
                            "SLOPPY_E_REALTIME_UNKNOWN_EVENT",
                            "Realtime client event has no handler.",
                            { event: envelope.type, closeCode: 1008 },
                        );
                    }
                    const eventPolicy = channel.client[envelope.type]?.metadata.auth;
                    await authorizeMessage(ctx, mergeMessagePolicies(eventPolicy, registration.policy), envelope.type, {
                        event: envelope.type,
                        data: envelope.data,
                        id: envelope.id,
                        connectionId,
                        channel: channel.name,
                    });
                    incrementMetric(ctx, "realtime.messages.in.total", {
                        ...metricLabels,
                        event: envelope.type,
                        outcome: "accepted",
                    });
                    await registration.handler(envelope.data, Object.freeze({
                        id: envelope.id,
                        event: envelope.type,
                    }));
                } catch (error) {
                    if (error instanceof SloppyRealtimeError &&
                        error.code === "SLOPPY_E_REALTIME_VALIDATION_FAILED")
                    {
                        incrementMetric(ctx, "realtime.messages.validation_failed.total", {
                            ...metricLabels,
                            event: error.event ?? "",
                        });
                    }
                    if (error instanceof SloppyRealtimeError &&
                        error.code === "SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT")
                    {
                        incrementMetric(ctx, "realtime.messages.unauthorized.total", {
                            ...metricLabels,
                            event: error.event ?? "",
                        });
                    }
                    await handleDispatchError(error, envelope?.type ?? error?.event);
                    if (socket.closed) {
                        break;
                    }
                }
            }
            return undefined;
        } finally {
            await cleanup();
        }
    }, routeOptions.websocket);
    Object.defineProperty(routeHandler, REALTIME_ROUTE_METADATA, {
        value: deepFreeze({
            kind: "realtime",
            channel: channel.metadata,
            options: {
                presence: routeOptions.presence,
                backplaneKind: routeOptions.backplaneKind,
                unknownEventPolicy: routeOptions.unknownEventPolicy,
                validationFailurePolicy: routeOptions.validationFailurePolicy,
                handlerErrorPolicy: routeOptions.handlerErrorPolicy,
            },
            websocket: routeOptions.websocket,
        }),
    });
    return routeHandler;
}

export const Realtime = Object.freeze({
    sse,
    websocket,
    hub: createHub,
    channel: createChannel,
    event: createRealtimeEvent,
    isChannel: isRealtimeChannel,
    backplane: Object.freeze({
        memory: createMemoryRealtimeBackplane,
    }),
    textBytes(value) {
        return Text.utf8.encode(String(value));
    },
});
