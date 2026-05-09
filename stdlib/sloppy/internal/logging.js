import { isPlainObject } from "./shared.js";

const LOG_LEVEL_RANK = Object.freeze({
    trace: 0,
    debug: 1,
    info: 2,
    warn: 3,
    error: 4,
    off: 5,
});
const MEMORY_SINK_STATE = new WeakMap();
const DEFAULT_REDACTION_KEYS = Object.freeze([
    "password",
    "passwd",
    "pwd",
    "secret",
    "token",
    "authorization",
    "cookie",
    "set-cookie",
    "apiKey",
    "clientSecret",
    "privateKey",
    "passphrase",
    "connectionString",
]);

function validateLogLevel(level, options = {}) {
    if (!Object.prototype.hasOwnProperty.call(LOG_LEVEL_RANK, level)) {
        throw new TypeError("Sloppy log level must be one of trace, debug, info, warn, error, or off.");
    }
    if (level === "off" && options.allowOff !== true) {
        throw new TypeError("Sloppy log write level must be one of trace, debug, info, warn, or error.");
    }
}

function normalizeRedactionKey(key) {
    return String(key).toLowerCase().replace(/[_\-.:]/g, "");
}

function isSensitiveKey(key, extraKeys) {
    const normalized = normalizeRedactionKey(key);

    for (const candidate of DEFAULT_REDACTION_KEYS) {
        const normalizedCandidate = normalizeRedactionKey(candidate);
        if (normalized === normalizedCandidate || normalized.endsWith(normalizedCandidate)) {
            return true;
        }
    }
    return extraKeys.has(normalized);
}

function validateSinkFormat(format, allowed, subject) {
    if (!allowed.includes(format)) {
        throw new TypeError(`Sloppy ${subject} logging format is not supported.`);
    }
}

function validateCapacity(capacity, subject) {
    if (!Number.isInteger(capacity) || capacity <= 0) {
        throw new TypeError(`Sloppy ${subject} capacity must be a positive integer.`);
    }
}

function copyLogFields(fields, redactionKeys) {
    if (fields === undefined || fields === null) {
        return undefined;
    }
    if (!isPlainObject(fields)) {
        throw new TypeError("Sloppy log fields must be a shallow plain object.");
    }

    const entries = Object.entries(fields);
    if (entries.length > 8) {
        throw new TypeError("Sloppy log fields support at most 8 fields per event.");
    }

    const copied = {};
    for (const [key, value] of entries) {
        if (key.length === 0) {
            throw new TypeError("Sloppy log field keys must be non-empty strings.");
        }
        if (isSensitiveKey(key, redactionKeys)) {
            copied[key] = "[REDACTED]";
            continue;
        }
        if (
            value === null ||
            typeof value === "string" ||
            typeof value === "boolean" ||
            (typeof value === "number" && Number.isFinite(value))
        ) {
            copied[key] = value;
            continue;
        }
        throw new TypeError(
            "Sloppy log fields support null, boolean, finite number, and string values.",
        );
    }
    return Object.freeze(copied);
}

function snapshotLogEntry(entry) {
    const snapshot = {
        level: entry.level,
        message: entry.message,
        fields: entry.fields,
    };
    if (entry.category !== "app") {
        snapshot.category = entry.category;
    }
    return Object.freeze(snapshot);
}

function createLoggingBuilder(guard) {
    const memorySinks = [];
    const consoleSinks = [];
    const fileSinks = [];
    const redactionKeys = new Set(DEFAULT_REDACTION_KEYS.map(normalizeRedactionKey));
    let minimumLevel = "info";
    let queueCapacity = 16;

    const logging = {
        setMinimumLevel(level) {
            guard.assertMutable();
            validateLogLevel(level, { allowOff: true });
            minimumLevel = level;
            return logging;
        },

        setQueueCapacity(capacity) {
            guard.assertMutable();
            validateCapacity(capacity, "queue");
            queueCapacity = capacity;
            return logging;
        },

        addRedactionKey(key) {
            guard.assertMutable();
            if (typeof key !== "string" || key.length === 0) {
                throw new TypeError("Sloppy log redaction key must be a non-empty string.");
            }
            redactionKeys.add(normalizeRedactionKey(key));
            return logging;
        },

        addMemorySink(options = undefined) {
            guard.assertMutable();
            if (options !== undefined && !isPlainObject(options)) {
                throw new TypeError("Sloppy memory log sink options must be a plain object.");
            }
            const capacity = options?.capacity ?? 32;
            validateCapacity(capacity, "memory sink");

            const state = {
                entries: [],
                capacity,
                overwritten: 0,
            };

            const sink = Object.freeze({
                entries() {
                    return Object.freeze(state.entries.map(snapshotLogEntry));
                },
                overwritten() {
                    return state.overwritten;
                },
            });

            MEMORY_SINK_STATE.set(sink, state);
            memorySinks.push(sink);
            return sink;
        },

        writeTo: Object.freeze({
            console(options = undefined) {
                guard.assertMutable();
                if (options !== undefined && !isPlainObject(options)) {
                    throw new TypeError("Sloppy console log sink options must be a plain object.");
                }
                const format = options?.format ?? "pretty";
                validateSinkFormat(format, ["pretty", "jsonl"], "console");
                consoleSinks.push(Object.freeze({ kind: "console", format }));
                return logging;
            },

            file(options) {
                guard.assertMutable();
                if (!isPlainObject(options)) {
                    throw new TypeError("Sloppy file log sink options must be a plain object.");
                }
                if (typeof options.path !== "string" || options.path.length === 0) {
                    throw new TypeError("Sloppy file log sink path must be a non-empty string.");
                }
                const format = options.format ?? "jsonl";
                validateSinkFormat(format, ["jsonl"], "file");
                fileSinks.push(Object.freeze({ kind: "file", path: options.path, format }));
                return logging;
            },
        }),

        __snapshot() {
            return Object.freeze({
                minimumLevel,
                queueCapacity,
                memorySinks: Object.freeze([...memorySinks]),
                consoleSinks: Object.freeze([...consoleSinks]),
                fileSinks: Object.freeze([...fileSinks]),
                redactionKeys: new Set(redactionKeys),
            });
        },
    };

    return Object.freeze(logging);
}

function createLogger(snapshot) {
    function createCategoryLogger(category) {
        function isEnabled(level) {
            validateLogLevel(level, { allowOff: true });
            return level !== "off" && LOG_LEVEL_RANK[level] >= LOG_LEVEL_RANK[snapshot.minimumLevel];
        }

        function write(level, message, fields) {
            validateLogLevel(level);

            if (!isEnabled(level)) {
                return;
            }

            const snapshotFields = copyLogFields(fields, snapshot.redactionKeys);
            const entry = Object.freeze({
                level,
                category,
                message: String(message),
                fields: snapshotFields,
            });

            for (const sink of snapshot.memorySinks) {
                const state = MEMORY_SINK_STATE.get(sink);
                if (state.entries.length >= state.capacity) {
                    state.entries.shift();
                    state.overwritten += 1;
                }
                state.entries.push(entry);
            }
        }

        return Object.freeze({
            isEnabled,
            forCategory(nextCategory) {
                if (typeof nextCategory !== "string" || nextCategory.length === 0) {
                    throw new TypeError("Sloppy log category must be a non-empty string.");
                }
                return createCategoryLogger(nextCategory);
            },
            trace(message, fields) {
                write("trace", message, fields);
            },
            debug(message, fields) {
                write("debug", message, fields);
            },
            info(message, fields) {
                write("info", message, fields);
            },
            warn(message, fields) {
                write("warn", message, fields);
            },
            error(message, fields) {
                write("error", message, fields);
            },
        });
    }

    return createCategoryLogger("app");
}

export { createLogger, createLoggingBuilder };
