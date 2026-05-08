const LOG_LEVEL_RANK = Object.freeze({
    trace: 0,
    debug: 1,
    info: 2,
    warn: 3,
    error: 4,
});
const MEMORY_SINK_STATE = new WeakMap();

function validateLogLevel(level) {
    if (!Object.prototype.hasOwnProperty.call(LOG_LEVEL_RANK, level)) {
        throw new TypeError("Sloppy log level must be one of trace, debug, info, warn, or error.");
    }
}

function snapshotLogEntry(entry) {
    return Object.freeze({
        level: entry.level,
        message: entry.message,
        fields: entry.fields,
    });
}

function createLoggingBuilder(guard) {
    const memorySinks = [];
    let minimumLevel = "info";

    const logging = {
        setMinimumLevel(level) {
            guard.assertMutable();
            validateLogLevel(level);
            minimumLevel = level;
            return logging;
        },

        addMemorySink() {
            guard.assertMutable();

            const state = {
                entries: [],
            };

            const sink = Object.freeze({
                entries() {
                    return Object.freeze(state.entries.map(snapshotLogEntry));
                },
            });

            MEMORY_SINK_STATE.set(sink, state);
            memorySinks.push(sink);
            return sink;
        },

        __snapshot() {
            return Object.freeze({
                minimumLevel,
                memorySinks: Object.freeze([...memorySinks]),
            });
        },
    };

    return Object.freeze(logging);
}

function createLogger(snapshot) {
    function write(level, message, fields) {
        validateLogLevel(level);

        if (LOG_LEVEL_RANK[level] < LOG_LEVEL_RANK[snapshot.minimumLevel]) {
            return;
        }

        const entry = Object.freeze({
            level,
            message: String(message),
            fields,
        });

        for (const sink of snapshot.memorySinks) {
            MEMORY_SINK_STATE.get(sink).entries.push(entry);
        }
    }

    return Object.freeze({
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

export { createLogger, createLoggingBuilder };
