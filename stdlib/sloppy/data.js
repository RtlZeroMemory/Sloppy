const QUERY_MARKER = "__sloppyQuery";
const DB_VALUE_MARKER = Symbol("sloppyDbValue");
const DB_BRIDGE_VALUE_MARKER = "__sloppyDbValue";
const DB_VALUE_KINDS = Object.freeze({
    decimal: true,
    uuid: true,
    date: true,
    time: true,
    localDateTime: true,
    instant: true,
    offsetDateTime: true,
    json: true,
    rawJson: true,
    bytes: true,
});
const LOWERED_QUERIES = new WeakSet();
const PLACEHOLDER_STYLES = Object.freeze({
    question: (index) => ({
        text: "?",
        name: null,
        position: index,
    }),
    postgres: (index) => ({
        text: `$${index}`,
        name: null,
        position: index,
    }),
    named: (index) => ({
        text: `@p${index}`,
        name: `p${index}`,
        position: index,
    }),
});

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }

    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function dbValueToString(kind, value) {
    if (kind === "json") {
        return JSON.stringify(value);
    }
    return String(value);
}

function isKnownDbValueKind(kind) {
    return typeof kind === "string"
        && Object.prototype.hasOwnProperty.call(DB_VALUE_KINDS, kind);
}

function createDbValue(kind, value) {
    if (!isKnownDbValueKind(kind)) {
        throw new TypeError("Sloppy sql value wrapper kind is not supported.");
    }
    const storedValue = kind === "bytes" ? new Uint8Array(value) : value;
    const wrapper = {
        kind,
        toString() {
            return dbValueToString(kind, storedValue);
        },
    };
    Object.defineProperties(wrapper, {
        [DB_VALUE_MARKER]: {
            value: true,
        },
        [DB_BRIDGE_VALUE_MARKER]: {
            value: true,
        },
        value: {
            enumerable: true,
            get() {
                return kind === "bytes" ? new Uint8Array(storedValue) : storedValue;
            },
        },
    });
    return Object.freeze(wrapper);
}

function isDbValue(value) {
    return value !== null
        && typeof value === "object"
        && Object.isFrozen(value)
        && isKnownDbValueKind(value.kind)
        && (
            value[DB_VALUE_MARKER] === true
            || (
                value[DB_BRIDGE_VALUE_MARKER] === true
                && Object.prototype.toString.call(value) === "[object String]"
            )
        );
}

function requireStringValue(value, operation) {
    if (typeof value !== "string" || value.length === 0) {
        throw new TypeError(`Sloppy ${operation} value must be a non-empty string.`);
    }
    return value;
}

function decimal(value) {
    const text = requireStringValue(value, "sql.decimal");
    if (!/^[+-]?(?:\d+|\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?$/.test(text)) {
        throw new TypeError("Sloppy sql.decimal value must be a finite decimal string.");
    }
    return createDbValue("decimal", text);
}

function uuid(value) {
    const text = requireStringValue(value, "sql.uuid").toLowerCase();
    if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(text)) {
        throw new TypeError("Sloppy sql.uuid value must be a canonical UUID string.");
    }
    return createDbValue("uuid", text);
}

function date(value) {
    const text = requireStringValue(value, "sql.date");
    if (!/^\d{4}-\d{2}-\d{2}$/.test(text)) {
        throw new TypeError("Sloppy sql.date value must be YYYY-MM-DD.");
    }
    return createDbValue("date", text);
}

function time(value) {
    const text = requireStringValue(value, "sql.time");
    if (!/^\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?$/.test(text)) {
        throw new TypeError("Sloppy sql.time value must be HH:MM:SS with optional fractional seconds.");
    }
    return createDbValue("time", text);
}

function timestamp(value) {
    const text = requireStringValue(value, "sql.timestamp");
    if (!/^\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?$/.test(text)) {
        throw new TypeError("Sloppy sql.timestamp value must be a local date-time string.");
    }
    return createDbValue("localDateTime", text);
}

function instant(value) {
    const text = requireStringValue(value, "sql.instant");
    if (!/^\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?Z$/.test(text)) {
        throw new TypeError("Sloppy sql.instant value must be a UTC timestamp ending in Z.");
    }
    return createDbValue("instant", text);
}

function offsetDateTime(value) {
    const text = requireStringValue(value, "sql.offsetDateTime");
    if (!/^\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?[+-]\d{2}:\d{2}$/.test(text)) {
        throw new TypeError("Sloppy sql.offsetDateTime value must include an explicit UTC offset.");
    }
    return createDbValue("offsetDateTime", text);
}

function json(value) {
    if (value === undefined || typeof value === "function" || typeof value === "symbol") {
        throw new TypeError("Sloppy sql.json value must be JSON-serializable.");
    }
    try {
        const encoded = JSON.stringify(value);
        if (encoded === undefined) {
            throw new TypeError("not serializable");
        }
    } catch {
        throw new TypeError("Sloppy sql.json value must be JSON-serializable.");
    }
    return createDbValue("json", value);
}

function rawJson(value) {
    const text = requireStringValue(value, "sql.rawJson");
    try {
        JSON.parse(text);
    } catch {
        throw new TypeError("Sloppy sql.rawJson value must be valid JSON text.");
    }
    return createDbValue("rawJson", text);
}

function bytes(value) {
    if (value instanceof Uint8Array) {
        return createDbValue("bytes", value);
    }
    if (value instanceof ArrayBuffer) {
        return createDbValue("bytes", new Uint8Array(value));
    }
    throw new TypeError("Sloppy sql.bytes value must be a Uint8Array or ArrayBuffer.");
}

const values = Object.freeze({
    decimal,
    uuid,
    date,
    time,
    timestamp,
    instant,
    offsetDateTime,
    json,
    rawJson,
    bytes,
    isDbValue,
});

function validatePlaceholderStyle(style) {
    if (!Object.prototype.hasOwnProperty.call(PLACEHOLDER_STYLES, style)) {
        throw new TypeError(
            "Sloppy data placeholderStyle must be one of question, postgres, or named.",
        );
    }
}

function normalizeLoweringOptions(options) {
    if (options === undefined) {
        return {
            placeholderStyle: "question",
        };
    }

    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy query lowering options must be a plain object.");
    }

    const placeholderStyle = options.placeholderStyle ?? "question";
    validatePlaceholderStyle(placeholderStyle);
    return { placeholderStyle };
}

function validateTemplateStrings(strings, operation) {
    if (!Array.isArray(strings)) {
        throw new TypeError(
            `Sloppy data ${operation} must be called as a tagged template or with a lowered query object.`,
        );
    }

    for (const segment of strings) {
        if (typeof segment !== "string") {
            throw new TypeError(`Sloppy data ${operation} template segments must be strings.`);
        }
    }
}

function createLoweredQuery(strings, values, options) {
    validateTemplateStrings(strings, "sql");

    if (strings.length !== values.length + 1) {
        throw new TypeError("Sloppy sql tag received an invalid template segment/value count.");
    }

    const normalized = normalizeLoweringOptions(options);
    const placeholders = [];
    let text = strings[0];

    for (let index = 0; index < values.length; index += 1) {
        const placeholder = PLACEHOLDER_STYLES[normalized.placeholderStyle](index + 1);
        placeholders.push(Object.freeze({
            index,
            text: placeholder.text,
            name: placeholder.name,
            position: placeholder.position,
        }));
        text += placeholder.text + strings[index + 1];
    }

    const lowered = Object.freeze({
        [QUERY_MARKER]: true,
        text,
        parameters: Object.freeze([...values]),
        parameterCount: values.length,
        placeholderStyle: normalized.placeholderStyle,
        placeholders: Object.freeze(placeholders),
    });

    LOWERED_QUERIES.add(lowered);
    return lowered;
}

function isLoweredQuery(value) {
    return value !== null && typeof value === "object" && LOWERED_QUERIES.has(value);
}

function createOperationCancelledError(operation, reason) {
    const detail = reason === undefined || reason === null || reason === ""
        ? "operation cancellation was requested"
        : String(reason);
    return new Error(`SLOPPY_E_CANCELLED: Sloppy data ${operation} was cancelled

Operation:
  ${operation}

Reason:
  ${detail}`);
}

function createOperationDeadlineError(operation) {
    return new Error(`SLOPPY_E_DEADLINE_EXCEEDED: Sloppy data ${operation} deadline was exceeded

Operation:
  ${operation}

Reason:
  The operation deadline was already expired before provider dispatch.`);
}

function normalizeOperationOptions(options, operation) {
    if (options === undefined) {
        return undefined;
    }
    if (!isPlainObject(options)) {
        throw new TypeError(`Sloppy data ${operation} options must be a plain object.`);
    }
    const allowedKeys = new Set(["deadline", "signal", "timeoutMs"]);
    const keys = Object.keys(options);
    for (const key of keys) {
        if (!allowedKeys.has(key)) {
            throw new TypeError(
                `Sloppy data ${operation} option '${key}' is not supported by the current runtime bridge.`,
            );
        }
    }

    const signal = options.signal;
    if (
        signal !== undefined
        && signal !== null
        && (typeof signal !== "object" || Array.isArray(signal))
    ) {
        throw new TypeError(`Sloppy data ${operation} signal option must be an object.`);
    }

    const deadline = options.deadline;
    if (
        deadline !== undefined
        && deadline !== null
        && (typeof deadline !== "object" || Array.isArray(deadline))
    ) {
        throw new TypeError(`Sloppy data ${operation} deadline option must be an object or null.`);
    }

    let timeoutMs = options.timeoutMs;
    if (timeoutMs !== undefined) {
        if (!Number.isInteger(timeoutMs) || timeoutMs < 0 || timeoutMs > 0xffffffff) {
            throw new TypeError(
                `Sloppy data ${operation} timeoutMs option must be an integer from 0 to 4294967295.`,
            );
        }
    }

    if (deadline !== undefined && deadline !== null) {
        if (deadline.expired === true) {
            throw createOperationDeadlineError(operation);
        }
        if (deadline.remainingMs !== undefined) {
            if (typeof deadline.remainingMs !== "function") {
                throw new TypeError(
                    `Sloppy data ${operation} deadline.remainingMs must be a function when supplied.`,
                );
            }
            const remaining = deadline.remainingMs();
            if (typeof remaining !== "number" || Number.isNaN(remaining)) {
                throw new TypeError(
                    `Sloppy data ${operation} deadline.remainingMs must return a number.`,
                );
            }
            if (remaining <= 0) {
                throw createOperationDeadlineError(operation);
            }
            if (Number.isFinite(remaining)) {
                const rounded = Math.ceil(remaining);
                timeoutMs = timeoutMs === undefined ? rounded : Math.min(timeoutMs, rounded);
            }
        }
    }

    const normalized = Object.freeze({
        deadline: deadline ?? undefined,
        signal: signal ?? undefined,
        timeoutMs,
    });
    return normalized.deadline === undefined
        && normalized.signal === undefined
        && normalized.timeoutMs === undefined
        ? undefined
        : normalized;
}

function throwIfOperationCancelled(options, operation) {
    if (options === undefined) {
        return;
    }
    if (options.signal !== undefined) {
        if (typeof options.signal.throwIfAborted === "function") {
            options.signal.throwIfAborted();
        }
        if (options.signal.aborted === true) {
            throw createOperationCancelledError(operation, options.signal.reason);
        }
    }
    if (options.timeoutMs === 0) {
        throw createOperationDeadlineError(operation);
    }
    if (options.deadline !== undefined) {
        if (options.deadline.expired === true) {
            throw createOperationDeadlineError(operation);
        }
        if (typeof options.deadline.remainingMs === "function") {
            const remaining = options.deadline.remainingMs();
            if (typeof remaining !== "number" || Number.isNaN(remaining)) {
                throw new TypeError(
                    `Sloppy data ${operation} deadline.remainingMs must return a number.`,
                );
            }
            if (remaining <= 0) {
                throw createOperationDeadlineError(operation);
            }
        }
    }
}

function invokeProviderOperation(operation, options, callback) {
    throwIfOperationCancelled(options, operation);
    return callback();
}

function normalizeProviderCallArguments(operation, placeholderStyle, args) {
    if (args.length === 2 && isLoweredQuery(args[0])) {
        return {
            query: args[0],
            options: normalizeOperationOptions(args[1], operation),
        };
    }
    if (args.length === 1 && isLoweredQuery(args[0])) {
        return {
            query: args[0],
            options: undefined,
        };
    }
    return {
        query: normalizeQueryArguments(operation, placeholderStyle, args),
        options: undefined,
    };
}

function validateOperationOptions(options, operation) {
    const normalized = normalizeOperationOptions(options, operation);
    if (normalized !== undefined) {
        throwIfOperationCancelled(normalized, operation);
    }
}

function normalizeQueryArguments(operation, placeholderStyle, args) {
    if (args.length === 1 && isLoweredQuery(args[0])) {
        return args[0];
    }
    if (args.length === 2 && isLoweredQuery(args[0])) {
        validateOperationOptions(args[1], operation);
        return args[0];
    }

    const strings = args[0];
    const values = args.slice(1);
    validateTemplateStrings(strings, operation);
    return createLoweredQuery(strings, values, { placeholderStyle });
}

function validateProviderDefinition(definition) {
    if (!isPlainObject(definition)) {
        throw new TypeError("Sloppy fake data provider definition must be a plain object.");
    }

    for (const method of ["query", "queryOne", "exec"]) {
        if (definition[method] !== undefined && typeof definition[method] !== "function") {
            throw new TypeError(`Sloppy fake data provider '${method}' handler must be a function.`);
        }
    }

    if (
        definition.transaction !== undefined
        && typeof definition.transaction !== "function"
        && !isPlainObject(definition.transaction)
    ) {
        throw new TypeError(
            "Sloppy fake data provider transaction handler must be a function or plain object.",
        );
    }
}

function validateSqliteOpenOptions(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy sqlite.open options must be a plain object.");
    }

    const allowedKeys = new Set(["database", "path", "capability", "access"]);
    for (const key of Object.keys(options)) {
        if (!allowedKeys.has(key)) {
            throw new TypeError(`Sloppy sqlite.open option '${key}' is not supported.`);
        }
    }

    const database = options.database ?? options.path;
    if (typeof database !== "string" || database.length === 0) {
        throw new TypeError("Sloppy sqlite.open database must be a non-empty string.");
    }

    if (
        typeof options.database === "string"
        && typeof options.path === "string"
        && options.database !== options.path
    ) {
        throw new TypeError("Sloppy sqlite.open database and path must match when both are supplied.");
    }

    if (typeof options.capability !== "string" || options.capability.length === 0) {
        throw new TypeError("Sloppy sqlite.open capability must be a non-empty string.");
    }

    const access = options.access ?? "readwrite";
    if (access !== "read" && access !== "write" && access !== "readwrite") {
        throw new TypeError("Sloppy sqlite.open access must be read, write, or readwrite.");
    }

    return Object.freeze({
        provider: "sqlite",
        database,
        path: database,
        capability: options.capability,
        access,
        placeholderStyle: "question",
    });
}

function normalizeSqliteProviderToken(name) {
    if (typeof name !== "string" || name.length === 0) {
        throw new TypeError("Sloppy data.sqlite provider name must be a non-empty string.");
    }

    return name.includes(".") ? name : `data.${name}`;
}

function sqliteNativeBridge() {
    return globalThis.__sloppy?.data?.sqlite ?? null;
}

function createSqliteClosedError(operation) {
    return new Error(`sloppy: sqlite connection is closed

Provider:
  sqlite

Operation:
  ${operation}

Fix:
  Open a new SQLite connection before using ${operation}.`);
}

function createSqliteTransactionClosedError(operation) {
    return new Error(`sloppy: sqlite transaction scope is closed

Provider:
  sqlite

Operation:
  ${operation}

Fix:
  Do not use the transaction object after transaction(...) resolves or rejects.`);
}

function createSqliteNestedTransactionError() {
    return new Error(`sloppy: sqlite nested transactions are not supported

Provider:
  sqlite

Operation:
  transaction

Fix:
  Use the transaction object passed to the current callback, or start a new transaction after it settles.`);
}

function createSqliteTransactionActiveError(operation) {
    return new Error(`sloppy: sqlite transaction is active

Provider:
  sqlite

Operation:
  ${operation}

Fix:
  Let the active transaction settle before closing this SQLite connection.`);
}

function validateSqliteParams(params, operation) {
    if (params === undefined) {
        return [];
    }

    if (!Array.isArray(params)) {
        throw new TypeError(`Sloppy sqlite.${operation} parameters must be an array.`);
    }

    return params.map((param) => {
        if (isDbValue(param)) {
            if (param.kind === "json") {
                return JSON.stringify(param.value);
            }
            if (param.kind === "rawJson") {
                return param.value;
            }
            if (param.kind === "bytes") {
                return param.value;
            }
            if (
                param.kind === "decimal"
                || param.kind === "uuid"
                || param.kind === "date"
                || param.kind === "time"
                || param.kind === "localDateTime"
                || param.kind === "instant"
                || param.kind === "offsetDateTime"
            ) {
                return param.toString();
            }
        }
        if (param !== null && typeof param === "object" && param[DB_BRIDGE_VALUE_MARKER] === true) {
            throw new TypeError(
                `Sloppy sqlite.${operation} parameter uses an unsupported sql value wrapper.`,
            );
        }
        const type = typeof param;
        if (
            param !== null
            && type !== "string"
            && type !== "number"
            && type !== "bigint"
            && type !== "boolean"
            && !(param instanceof Uint8Array)
        ) {
            throw new TypeError(
                `Sloppy sqlite.${operation} parameters support only null, string, number, bigint, boolean, Uint8Array, and explicit sql value wrappers.`,
            );
        }
        return param;
    });
}

function normalizeSqliteOperation(operation, args) {
    if (args.length === 1 && isLoweredQuery(args[0])) {
        return {
            text: args[0].text,
            parameters: validateSqliteParams(args[0].parameters, operation),
            options: undefined,
        };
    }
    if (args.length === 2 && isLoweredQuery(args[0])) {
        const options = normalizeOperationOptions(args[1], `sqlite.${operation}`);
        return {
            text: args[0].text,
            parameters: validateSqliteParams(args[0].parameters, operation),
            options,
        };
    }

    if (typeof args[0] === "string") {
        if (args.length > 3) {
            throw new TypeError(`Sloppy sqlite.${operation} accepts sql, optional params, and optional options.`);
        }
        const options = normalizeOperationOptions(args[2], `sqlite.${operation}`);

        if (args[0].length === 0) {
            throw new TypeError(`Sloppy sqlite.${operation} SQL must be a non-empty string.`);
        }

        return {
            text: args[0],
            parameters: validateSqliteParams(args[1], operation),
            options,
        };
    }

    const call = normalizeProviderCallArguments(`sqlite.${operation}`, "question", args);
    return {
        text: call.query.text,
        parameters: validateSqliteParams(call.query.parameters, operation),
        options: call.options,
    };
}

function createSqliteConnection(nativeBridge, handle) {
    const state = {
        closed: false,
        handle,
        transactionActive: false,
    };

    function assertOpen(operation) {
        if (state.closed) {
            throw createSqliteClosedError(operation);
        }
    }

    function createSqliteTransaction() {
        const txState = {
            closed: false,
        };

        function assertTransactionOpen(operation) {
            assertOpen(operation);
            if (txState.closed) {
                throw createSqliteTransactionClosedError(operation);
            }
        }

        const tx = Object.freeze({
            query(...args) {
                assertTransactionOpen("transaction.query");
                const query = normalizeSqliteOperation("query", args);
                return invokeProviderOperation("sqlite.transaction.query", query.options, () =>
                    nativeBridge.transactionQuery(state.handle, query.text, query.parameters));
            },

            queryOne(...args) {
                assertTransactionOpen("transaction.queryOne");
                const query = normalizeSqliteOperation("queryOne", args);
                return invokeProviderOperation("sqlite.transaction.queryOne", query.options, () =>
                    nativeBridge.transactionQueryOne(state.handle, query.text, query.parameters));
            },

            exec(...args) {
                assertTransactionOpen("transaction.exec");
                const query = normalizeSqliteOperation("exec", args);
                return invokeProviderOperation("sqlite.transaction.exec", query.options, () =>
                    nativeBridge.transactionExec(state.handle, query.text, query.parameters));
            },

            transaction() {
                throw createSqliteNestedTransactionError();
            },
        });

        return {
            tx,
            close() {
                txState.closed = true;
            },
        };
    }

    async function rollbackAfterCallbackError(error, transaction) {
        try {
            await nativeBridge.transactionRollback(state.handle);
        } catch {
            if (transaction !== undefined) {
                transaction.close();
            }
            state.closed = true;
            try {
                nativeBridge.close(state.handle);
            } catch {
                // Preserve the original callback or thenable error while preventing reuse.
            }
            throw error;
        }
        if (transaction !== undefined) {
            transaction.close();
        }
        state.transactionActive = false;
        throw error;
    }

    async function commitTransaction(transaction) {
        try {
            await nativeBridge.transactionCommit(state.handle);
        } catch (error) {
            transaction.close();
            state.transactionActive = false;
            state.closed = true;
            try {
                nativeBridge.close(state.handle);
            } catch {
                // Keep the commit failure as the observable error.
            }
            throw error;
        }
        transaction.close();
        state.transactionActive = false;
    }

    return Object.freeze({
        query(...args) {
            assertOpen("query");
            const query = normalizeSqliteOperation("query", args);
            return invokeProviderOperation("sqlite.query", query.options, () =>
                nativeBridge.query(state.handle, query.text, query.parameters));
        },

        queryOne(...args) {
            assertOpen("queryOne");
            const query = normalizeSqliteOperation("queryOne", args);
            return invokeProviderOperation("sqlite.queryOne", query.options, () =>
                nativeBridge.queryOne(state.handle, query.text, query.parameters));
        },

        exec(...args) {
            assertOpen("exec");
            const query = normalizeSqliteOperation("exec", args);
            return invokeProviderOperation("sqlite.exec", query.options, () =>
                nativeBridge.exec(state.handle, query.text, query.parameters));
        },

        async transaction(callback) {
            assertOpen("transaction");
            if (typeof callback !== "function") {
                throw new TypeError("Sloppy sqlite.transaction callback must be a function.");
            }
            if (state.transactionActive) {
                throw createSqliteNestedTransactionError();
            }

            state.transactionActive = true;
            try {
                await nativeBridge.transactionBegin(state.handle);
            } catch (error) {
                state.transactionActive = false;
                throw error;
            }

            const transaction = createSqliteTransaction();
            let value;
            try {
                value = await callback(transaction.tx);
            } catch (error) {
                return rollbackAfterCallbackError(error, transaction);
            }
            await commitTransaction(transaction);
            return value;
        },

        close() {
            if (state.closed) {
                return;
            }
            if (state.transactionActive) {
                throw createSqliteTransactionActiveError("close");
            }

            nativeBridge.close(state.handle);
            state.closed = true;
        },

        __debug() {
            return Object.freeze({
                kind: "sqlite-connection",
                closed: state.closed,
                transactionActive: state.transactionActive,
                resource: "opaque",
            });
        },
    });
}

function redactConnectionString(value) {
    return value
        .replace(
            /(^|[\s&])(password=)(?:'(?:\\.|[^'])*'|"(?:\\.|[^"])*"|[^\s&]*)/gi,
            (_match, prefix, key) => `${prefix}${key}<redacted>`,
        )
        .replace(/(postgres(?:ql)?:\/\/[^:\s/@]+:)[^@\s/]+(@)/gi, "$1<redacted>$2");
}

function validatePostgresOpenOptions(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy postgres.open options must be a plain object.");
    }

    if (typeof options.connectionString !== "string" || options.connectionString.length === 0) {
        throw new TypeError("Sloppy postgres.open connectionString must be a non-empty string.");
    }

    const access = options.access ?? "readwrite";
    if (access !== "read" && access !== "readwrite") {
        throw new TypeError("Sloppy postgres.open access must be read or readwrite.");
    }

    const maxConnections = options.maxConnections ?? 1;
    if (!Number.isInteger(maxConnections) || maxConnections < 1 || maxConnections > 16) {
        throw new TypeError("Sloppy postgres.open maxConnections must be an integer from 1 to 16.");
    }

    return Object.freeze({
        provider: "postgres",
        connectionString: options.connectionString,
        redactedConnectionString: redactConnectionString(options.connectionString),
        access,
        maxConnections,
        capability: options.capability ?? "data.postgres",
        placeholderStyle: "postgres",
    });
}

function postgresNativeBridge() {
    return globalThis.__sloppy?.data?.postgres ?? null;
}

function sqlserverNativeBridge() {
    return globalThis.__sloppy?.data?.sqlserver ?? null;
}

function redactOdbcConnectionString(value) {
    return value.replace(
        /(^|;)(\s*)(password|pwd|access token|accesstoken)(\s*)=(\s*)({(?:}}|[^}])*}|[^;]*)/gi,
        (_match, prefix, leading, key, beforeEquals, afterEquals) =>
            `${prefix}${leading}${key}${beforeEquals}=${afterEquals}<redacted>`,
    );
}

function extractOdbcDriverName(connectionString) {
    const match = /(?:^|;)\s*driver\s*=\s*({(?:}}|[^}])*}|[^;]*)/i.exec(connectionString);

    if (!match) {
        return "";
    }
    const value = match[1];
    if (value.startsWith("{") && value.endsWith("}")) {
        return value.slice(1, -1).replaceAll("}}", "}");
    }
    return value;
}

function validateSqlServerOpenOptions(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy sqlserver.open options must be a plain object.");
    }

    if (typeof options.connectionString !== "string" || options.connectionString.length === 0) {
        throw new TypeError("Sloppy sqlserver.open connectionString must be a non-empty string.");
    }

    const access = options.access ?? "readwrite";
    if (access !== "read" && access !== "readwrite") {
        throw new TypeError("Sloppy sqlserver.open access must be read or readwrite.");
    }

    const maxConnections = options.maxConnections ?? 1;
    if (!Number.isInteger(maxConnections) || maxConnections < 1 || maxConnections > 16) {
        throw new TypeError("Sloppy sqlserver.open maxConnections must be an integer from 1 to 16.");
    }

    return Object.freeze({
        provider: "sqlserver",
        connectionString: options.connectionString,
        redactedConnectionString: redactOdbcConnectionString(options.connectionString),
        driver: extractOdbcDriverName(options.connectionString),
        capability: options.capability ?? "data.sqlserver",
        access,
        maxConnections,
        placeholderStyle: "question",
    });
}

function missingProviderMethod(method) {
    throw new Error(`sloppy: fake data provider method missing

Method:
  ${method}

Fix:
  Pass a '${method}' function to data.createFakeProvider(...) for this test or example.`);
}

function createSqliteUnavailableError(operation) {
    return new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature provider.sqlite is inactive or unavailable

Provider:
  sqlite

Feature:
  provider.sqlite

Operation:
  ${operation}

Reason:
  The active Sloppy Plan did not enable the __sloppy.data.sqlite V8 intrinsic namespace.

Fix:
  Add SQLite provider metadata to the Plan, or keep SQLite usage behind a documented deferral.`);
}

function createPostgresUnavailableError(operation, options) {
    const safeOptions = validatePostgresOpenOptions(options);
    return new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature provider.postgres is unavailable

Provider:
  postgres

Feature:
  provider.postgres

Operation:
  ${operation}

Connection:
  ${safeOptions.redactedConnectionString}

Reason:
  The active Sloppy Plan did not enable the __sloppy.data.postgres V8 intrinsic namespace.

Fix:
  Add PostgreSQL provider metadata to the Plan, or keep PostgreSQL usage behind a documented capability boundary.`);
}

function createSqlServerUnavailableError(operation, options) {
    const safeOptions = validateSqlServerOpenOptions(options);
    return new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature provider.sqlserver is unavailable

Provider:
  sqlserver

Feature:
  provider.sqlserver

Operation:
  ${operation}

Connection:
  ${safeOptions.redactedConnectionString}

Reason:
  The active Sloppy Plan did not enable the __sloppy.data.sqlserver V8 intrinsic namespace.

Fix:
  Add SQL Server provider metadata to the Plan, or keep SQL Server usage behind a documented capability boundary.`);
}

function openSqlite(options) {
    const safeOptions = validateSqliteOpenOptions(options);
    const nativeBridge = sqliteNativeBridge();

    if (nativeBridge === null) {
        throw createSqliteUnavailableError("open");
    }

    return createSqliteConnection(nativeBridge, nativeBridge.open(safeOptions));
}

function openSqliteProvider(name) {
    const nativeBridge = sqliteNativeBridge();

    if (nativeBridge === null) {
        throw createSqliteUnavailableError("open");
    }

    return createSqliteConnection(nativeBridge, nativeBridge.open({
        provider: normalizeSqliteProviderToken(name),
    }));
}

function createPostgresClosedError(operation) {
    return new Error(`sloppy: postgres connection is closed

Provider:
  postgres

Operation:
  ${operation}

Fix:
  Open a new PostgreSQL connection before using ${operation}.`);
}

function createPostgresTransactionClosedError(operation) {
    return new Error(`sloppy: postgres transaction scope is closed

Provider:
  postgres

Operation:
  ${operation}

Fix:
  Do not use the transaction object after transaction(...) resolves or rejects.`);
}

function createPostgresNestedTransactionError() {
    return new Error(`sloppy: postgres nested transactions are not supported

Provider:
  postgres

Operation:
  transaction

Fix:
  Use the transaction object passed to the current callback, or start a new transaction after it settles.`);
}

function normalizePostgresOperation(operation, args) {
    if (args.length === 1 && isLoweredQuery(args[0])) {
        return {
            text: args[0].text,
            parameters: args[0].parameters,
            options: undefined,
        };
    }
    if (args.length === 2 && isLoweredQuery(args[0])) {
        const options = normalizeOperationOptions(args[1], `postgres.${operation}`);
        return {
            text: args[0].text,
            parameters: args[0].parameters,
            options,
        };
    }

    if (typeof args[0] === "string") {
        if (args.length > 3) {
            throw new TypeError(`Sloppy postgres.${operation} accepts sql, optional params, and optional options.`);
        }
        const options = normalizeOperationOptions(args[2], `postgres.${operation}`);
        if (args[0].length === 0) {
            throw new TypeError(`Sloppy postgres.${operation} SQL must be a non-empty string.`);
        }
        if (args[1] !== undefined && !Array.isArray(args[1])) {
            throw new TypeError(`Sloppy postgres.${operation} parameters must be an array.`);
        }
        return {
            text: args[0],
            parameters: args[1] ?? [],
            options,
        };
    }

    const call = normalizeProviderCallArguments(`postgres.${operation}`, "postgres", args);
    return {
        text: call.query.text,
        parameters: call.query.parameters,
        options: call.options,
    };
}

function createPostgresConnection(nativeBridge, handle) {
    const state = {
        closed: false,
        handle,
        transactionActive: false,
    };

    function assertOpen(operation) {
        if (state.closed) {
            throw createPostgresClosedError(operation);
        }
    }

    function createTransaction() {
        const txState = { closed: false };
        function assertTransactionOpen(operation) {
            assertOpen(operation);
            if (txState.closed) {
                throw createPostgresTransactionClosedError(operation);
            }
        }

        const tx = Object.freeze({
            query(...args) {
                assertTransactionOpen("transaction.query");
                const query = normalizePostgresOperation("query", args);
                return invokeProviderOperation("postgres.transaction.query", query.options, () =>
                    nativeBridge.transactionQuery(state.handle, query.text, query.parameters));
            },
            queryOne(...args) {
                assertTransactionOpen("transaction.queryOne");
                const query = normalizePostgresOperation("queryOne", args);
                return invokeProviderOperation("postgres.transaction.queryOne", query.options, () =>
                    nativeBridge.transactionQueryOne(state.handle, query.text, query.parameters));
            },
            exec(...args) {
                assertTransactionOpen("transaction.exec");
                const query = normalizePostgresOperation("exec", args);
                return invokeProviderOperation("postgres.transaction.exec", query.options, () =>
                    nativeBridge.transactionExec(state.handle, query.text, query.parameters));
            },
            transaction() {
                throw createPostgresNestedTransactionError();
            },
        });

        return {
            tx,
            close() {
                txState.closed = true;
            },
        };
    }

    async function rollbackAfterCallbackError(error, transaction) {
        try {
            await nativeBridge.transactionRollback(state.handle);
        } catch {
            transaction.close();
            state.closed = true;
            try {
                nativeBridge.close(state.handle);
            } catch {
                // Preserve the original callback error while preventing reuse.
            }
            throw error;
        }
        transaction.close();
        state.transactionActive = false;
        throw error;
    }

    async function commitTransaction(transaction) {
        try {
            await nativeBridge.transactionCommit(state.handle);
        } catch (error) {
            transaction.close();
            state.transactionActive = false;
            state.closed = true;
            try {
                nativeBridge.close(state.handle);
            } catch {
                // Keep the commit failure as the observable error.
            }
            throw error;
        }
        transaction.close();
        state.transactionActive = false;
    }

    return Object.freeze({
        query(...args) {
            assertOpen("query");
            const query = normalizePostgresOperation("query", args);
            return invokeProviderOperation("postgres.query", query.options, () =>
                nativeBridge.query(state.handle, query.text, query.parameters));
        },
        queryOne(...args) {
            assertOpen("queryOne");
            const query = normalizePostgresOperation("queryOne", args);
            return invokeProviderOperation("postgres.queryOne", query.options, () =>
                nativeBridge.queryOne(state.handle, query.text, query.parameters));
        },
        exec(...args) {
            assertOpen("exec");
            const query = normalizePostgresOperation("exec", args);
            return invokeProviderOperation("postgres.exec", query.options, () =>
                nativeBridge.exec(state.handle, query.text, query.parameters));
        },
        async transaction(callback) {
            assertOpen("transaction");
            if (typeof callback !== "function") {
                throw new TypeError("Sloppy postgres.transaction callback must be a function.");
            }
            if (state.transactionActive) {
                throw createPostgresNestedTransactionError();
            }
            state.transactionActive = true;
            try {
                await nativeBridge.transactionBegin(state.handle);
            } catch (error) {
                state.transactionActive = false;
                throw error;
            }

            const transaction = createTransaction();
            let value;
            try {
                value = await callback(transaction.tx);
            } catch (error) {
                return rollbackAfterCallbackError(error, transaction);
            }
            await commitTransaction(transaction);
            return value;
        },
        close() {
            if (state.closed) {
                return;
            }
            if (state.transactionActive) {
                throw new Error("sloppy: postgres transaction is active");
            }
            nativeBridge.close(state.handle);
            state.closed = true;
        },
        __debug() {
            return Object.freeze({
                kind: "postgres-connection",
                closed: state.closed,
                transactionActive: state.transactionActive,
                resource: "opaque",
            });
        },
    });
}

function createSqlServerClosedError(operation) {
    return new Error(`sloppy: sqlserver connection is closed

Provider:
  sqlserver

Operation:
  ${operation}

Fix:
  Open a new SQL Server connection before using ${operation}.`);
}

function createSqlServerTransactionClosedError(operation) {
    return new Error(`sloppy: sqlserver transaction scope is closed

Provider:
  sqlserver

Operation:
  ${operation}

Fix:
  Do not use the transaction object after transaction(...) resolves or rejects.`);
}

function createSqlServerNestedTransactionError() {
    return new Error(`sloppy: sqlserver nested transactions are not supported

Provider:
  sqlserver

Operation:
  transaction

Fix:
  Use the transaction object passed to the current callback, or start a new transaction after it settles.`);
}

function normalizeSqlServerOperation(operation, args) {
    if (args.length === 1 && isLoweredQuery(args[0])) {
        return {
            text: args[0].text,
            parameters: args[0].parameters,
            options: undefined,
        };
    }
    if (args.length === 2 && isLoweredQuery(args[0])) {
        const options = normalizeOperationOptions(args[1], `sqlserver.${operation}`);
        return {
            text: args[0].text,
            parameters: args[0].parameters,
            options,
        };
    }

    if (typeof args[0] === "string") {
        if (args.length > 3) {
            throw new TypeError(`Sloppy sqlserver.${operation} accepts sql, optional params, and optional options.`);
        }
        const options = normalizeOperationOptions(args[2], `sqlserver.${operation}`);
        if (args[0].length === 0) {
            throw new TypeError(`Sloppy sqlserver.${operation} SQL must be a non-empty string.`);
        }
        if (args[1] !== undefined && !Array.isArray(args[1])) {
            throw new TypeError(`Sloppy sqlserver.${operation} parameters must be an array.`);
        }
        return {
            text: args[0],
            parameters: args[1] ?? [],
            options,
        };
    }

    const call = normalizeProviderCallArguments(`sqlserver.${operation}`, "question", args);
    return {
        text: call.query.text,
        parameters: call.query.parameters,
        options: call.options,
    };
}

function createSqlServerConnection(nativeBridge, handle) {
    const state = {
        closed: false,
        handle,
        transactionActive: false,
    };

    function assertOpen(operation) {
        if (state.closed) {
            throw createSqlServerClosedError(operation);
        }
    }

    function createTransaction() {
        const txState = { closed: false };
        function assertTransactionOpen(operation) {
            assertOpen(operation);
            if (txState.closed) {
                throw createSqlServerTransactionClosedError(operation);
            }
        }

        const tx = Object.freeze({
            query(...args) {
                assertTransactionOpen("transaction.query");
                const query = normalizeSqlServerOperation("query", args);
                return invokeProviderOperation("sqlserver.transaction.query", query.options, () =>
                    nativeBridge.transactionQuery(state.handle, query.text, query.parameters));
            },
            queryOne(...args) {
                assertTransactionOpen("transaction.queryOne");
                const query = normalizeSqlServerOperation("queryOne", args);
                return invokeProviderOperation("sqlserver.transaction.queryOne", query.options, () =>
                    nativeBridge.transactionQueryOne(state.handle, query.text, query.parameters));
            },
            exec(...args) {
                assertTransactionOpen("transaction.exec");
                const query = normalizeSqlServerOperation("exec", args);
                return invokeProviderOperation("sqlserver.transaction.exec", query.options, () =>
                    nativeBridge.transactionExec(state.handle, query.text, query.parameters));
            },
            transaction() {
                throw createSqlServerNestedTransactionError();
            },
        });

        return {
            tx,
            close() {
                txState.closed = true;
            },
        };
    }

    async function rollbackAfterCallbackError(error, transaction) {
        try {
            await nativeBridge.transactionRollback(state.handle);
        } catch {
            transaction.close();
            state.closed = true;
            try {
                nativeBridge.close(state.handle);
            } catch {
                // Preserve the original callback error while preventing reuse.
            }
            throw error;
        }
        transaction.close();
        state.transactionActive = false;
        throw error;
    }

    async function commitTransaction(transaction) {
        try {
            await nativeBridge.transactionCommit(state.handle);
        } catch (error) {
            transaction.close();
            state.transactionActive = false;
            state.closed = true;
            try {
                nativeBridge.close(state.handle);
            } catch {
                // Keep the commit failure as the observable error.
            }
            throw error;
        }
        transaction.close();
        state.transactionActive = false;
    }

    return Object.freeze({
        query(...args) {
            assertOpen("query");
            const query = normalizeSqlServerOperation("query", args);
            return invokeProviderOperation("sqlserver.query", query.options, () =>
                nativeBridge.query(state.handle, query.text, query.parameters));
        },
        queryOne(...args) {
            assertOpen("queryOne");
            const query = normalizeSqlServerOperation("queryOne", args);
            return invokeProviderOperation("sqlserver.queryOne", query.options, () =>
                nativeBridge.queryOne(state.handle, query.text, query.parameters));
        },
        exec(...args) {
            assertOpen("exec");
            const query = normalizeSqlServerOperation("exec", args);
            return invokeProviderOperation("sqlserver.exec", query.options, () =>
                nativeBridge.exec(state.handle, query.text, query.parameters));
        },
        async transaction(callback) {
            assertOpen("transaction");
            if (typeof callback !== "function") {
                throw new TypeError("Sloppy sqlserver.transaction callback must be a function.");
            }
            if (state.transactionActive) {
                throw createSqlServerNestedTransactionError();
            }
            state.transactionActive = true;
            try {
                await nativeBridge.transactionBegin(state.handle);
            } catch (error) {
                state.transactionActive = false;
                throw error;
            }
            const transaction = createTransaction();
            let value;
            try {
                value = await callback(transaction.tx);
            } catch (error) {
                return rollbackAfterCallbackError(error, transaction);
            }
            await commitTransaction(transaction);
            return value;
        },
        close() {
            if (state.closed) {
                return;
            }
            if (state.transactionActive) {
                throw new Error("sloppy: sqlserver transaction is active");
            }
            nativeBridge.close(state.handle);
            state.closed = true;
        },
        __debug() {
            return Object.freeze({
                kind: "sqlserver-connection",
                closed: state.closed,
                transactionActive: state.transactionActive,
                resource: "opaque",
            });
        },
    });
}

function openPostgres(options) {
    const safeOptions = validatePostgresOpenOptions(options);
    const nativeBridge = postgresNativeBridge();

    if (nativeBridge === null) {
        throw createPostgresUnavailableError("open", options);
    }

    return createPostgresConnection(nativeBridge, nativeBridge.open(safeOptions));
}

function openSqlServer(options) {
    const safeOptions = validateSqlServerOpenOptions(options);
    const nativeBridge = sqlserverNativeBridge();

    if (nativeBridge === null) {
        throw createSqlServerUnavailableError("open", options);
    }

    return createSqlServerConnection(nativeBridge, nativeBridge.open(safeOptions));
}

function doctorSqlServer(options = {}) {
    const connectionString = typeof options.connectionString === "string"
        ? options.connectionString
        : "";
    const driver = connectionString.length > 0 ? extractOdbcDriverName(connectionString) : "";

    return Object.freeze({
        ok: false,
        provider: "sqlserver",
        driverManager: "native-check-unavailable",
        driver: driver.length > 0 ? "unchecked" : "unknown",
        message: "SQL Server doctor metadata is redacted here; live driver/service validation runs only in the opt-in native or V8 live-provider lanes.",
        connectionString: connectionString.length > 0
            ? redactOdbcConnectionString(connectionString)
            : undefined,
        hints: Object.freeze([
            "install Microsoft ODBC Driver 18 for SQL Server",
            "check driver name",
            "check connection string",
            "use TrustServerCertificate=yes for local dev only when appropriate",
        ]),
    });
}

const sqliteSupports = {
    memory: true,
    file: true,
    queryTemplates: true,
    parameters: Object.freeze([
        "null",
        "string",
        "integer",
        "bigint",
        "float",
        "boolean",
        "bytes",
        "explicit-json-text",
        "explicit-date-time-text",
    ]),
    transactions: true,
    transactionsMode: "callback",
    preparedStatements: false,
    pooling: false,
    migrations: false,
    orm: false,
};

Object.defineProperty(sqliteSupports, "nativeStdlibBridge", {
    enumerable: true,
    get() {
        return sqliteNativeBridge() !== null;
    },
});

const postgresSupports = {
    connectionString: true,
    queryTemplates: true,
    parameters: Object.freeze([
        "null",
        "string",
        "integer",
        "float",
        "boolean",
        "bigint",
        "decimal",
        "bytes",
        "uuid",
        "json",
        "date",
        "time",
        "timestamp",
        "instant",
        "offsetDateTime",
        "array",
    ]),
    transactions: true,
    pooling: true,
    executionMode: "TRUE_ASYNC",
    migrations: false,
    orm: false,
};

Object.defineProperty(postgresSupports, "nativeStdlibBridge", {
    enumerable: true,
    get() {
        return postgresNativeBridge() !== null;
    },
});

const sqlserverSupports = {
    connectionString: true,
    odbc: true,
    queryTemplates: true,
    parameters: Object.freeze([
        "null",
        "string",
        "integer",
        "float",
        "boolean",
        "bigint",
        "decimal",
        "bytes",
        "uuid",
        "date",
        "time",
        "timestamp",
        "offsetDateTime",
        "explicit-json-text",
    ]),
    transactions: true,
    pooling: true,
    executionMode: "TRUE_ASYNC",
    migrations: false,
    orm: false,
};

Object.defineProperty(sqlserverSupports, "nativeStdlibBridge", {
    enumerable: true,
    get() {
        return sqlserverNativeBridge() !== null;
    },
});

function sqlite(name) {
    return openSqliteProvider(name);
}

Object.defineProperties(sqlite, {
    provider: {
        enumerable: true,
        value: "sqlite",
    },
    placeholderStyle: {
        enumerable: true,
        value: "question",
    },
    supports: {
        enumerable: true,
        value: Object.freeze(sqliteSupports),
    },
    open: {
        enumerable: true,
        value: openSqlite,
    },
    __debug: {
        enumerable: true,
        value() {
            return Object.freeze({
                provider: "sqlite",
                placeholderStyle: "question",
                nativeStdlibBridge: sqliteNativeBridge() !== null,
            });
        },
    },
});

Object.freeze(sqlite);

const postgres = Object.freeze({
    provider: "postgres",
    placeholderStyle: "postgres",
    supports: Object.freeze(postgresSupports),
    open: openPostgres,
    redactConnectionString,
    __debug() {
        return Object.freeze({
            provider: "postgres",
            placeholderStyle: "postgres",
            nativeStdlibBridge: postgresNativeBridge() !== null,
            executionMode: "TRUE_ASYNC",
        });
    },
});

const sqlserver = Object.freeze({
    provider: "sqlserver",
    placeholderStyle: "question",
    supports: Object.freeze(sqlserverSupports),
    open: openSqlServer,
    doctor: doctorSqlServer,
    redactConnectionString: redactOdbcConnectionString,
    __debug() {
        return Object.freeze({
            provider: "sqlserver",
            placeholderStyle: "question",
            nativeStdlibBridge: sqlserverNativeBridge() !== null,
            executionMode: "TRUE_ASYNC",
        });
    },
});

function createTransactionState(provider) {
    return {
        provider,
        closed: false,
    };
}

function assertTransactionOpen(state, operation) {
    if (state.closed) {
        throw new Error(`sloppy: transaction scope is closed

Operation:
  ${operation}

Fix:
  Do not use the transaction object after transaction(...) resolves or rejects.`);
    }
}

function createTransactionProvider(state, placeholderStyle) {
    return Object.freeze({
        query(...args) {
            assertTransactionOpen(state, "query");
            const call = normalizeProviderCallArguments("query", placeholderStyle, args);
            return invokeProviderOperation("query", call.options, () =>
                state.provider.query(call.query, call.options));
        },

        queryOne(...args) {
            assertTransactionOpen(state, "queryOne");
            const call = normalizeProviderCallArguments("queryOne", placeholderStyle, args);
            return invokeProviderOperation("queryOne", call.options, () =>
                state.provider.queryOne(call.query, call.options));
        },

        exec(...args) {
            assertTransactionOpen(state, "exec");
            const call = normalizeProviderCallArguments("exec", placeholderStyle, args);
            return invokeProviderOperation("exec", call.options, () =>
                state.provider.exec(call.query, call.options));
        },

        transaction() {
            throw new Error(`sloppy: nested transactions are not supported yet

Operation:
  transaction

Fix:
  Use the transaction object passed to the current callback, or start a new transaction after it settles.`);
        },
    });
}

function createFakeProvider(definition = {}) {
    validateProviderDefinition(definition);

    const events = [];
    const placeholderStyle = definition.placeholderStyle ?? "question";
    let transactionActive = false;
    validatePlaceholderStyle(placeholderStyle);

    const backend = {
        query(query, options) {
            if (definition.query === undefined) {
                missingProviderMethod("query");
            }

            return definition.query(query, options);
        },

        queryOne(query, options) {
            if (definition.queryOne !== undefined) {
                return definition.queryOne(query, options);
            }

            if (definition.query === undefined) {
                missingProviderMethod("queryOne");
            }

            return Promise.resolve(definition.query(query, options)).then((rows) => {
                if (rows == null) {
                    return null;
                }

                if (!Array.isArray(rows)) {
                    throw new TypeError("Sloppy fake data provider queryOne fallback expected query() to return an array.");
                }

                return rows[0] ?? null;
            });
        },

        exec(query, options) {
            if (definition.exec === undefined) {
                missingProviderMethod("exec");
            }

            return definition.exec(query, options);
        },
    };

    const transactionHooks = isPlainObject(definition.transaction) ? definition.transaction : {};

    async function runTransaction(callback) {
        if (typeof callback !== "function") {
            throw new TypeError("Sloppy data transaction callback must be a function.");
        }

        if (transactionActive) {
            throw new Error(`sloppy: nested transactions are not supported yet

Operation:
  transaction

Fix:
  Use the transaction object passed to the current callback, or start a new transaction after it settles.`);
        }

        transactionActive = true;
        events.push("begin");

        const state = createTransactionState(backend);

        try {
            if (typeof transactionHooks.begin === "function") {
                await transactionHooks.begin();
            } else if (typeof definition.transaction === "function") {
                await definition.transaction("begin");
            }

            const tx = createTransactionProvider(state, placeholderStyle);
            const result = await callback(tx);

            state.closed = true;
            events.push("commit");

            if (typeof transactionHooks.commit === "function") {
                await transactionHooks.commit();
            } else if (typeof definition.transaction === "function") {
                await definition.transaction("commit");
            }

            return result;
        } catch (error) {
            state.closed = true;
            events.push("rollback");

            if (typeof transactionHooks.rollback === "function") {
                await transactionHooks.rollback(error);
            } else if (typeof definition.transaction === "function") {
                await definition.transaction("rollback", error);
            }

            throw error;
        } finally {
            transactionActive = false;
        }
    }

    const provider = {
        query(...args) {
            const call = normalizeProviderCallArguments("query", placeholderStyle, args);
            return invokeProviderOperation("query", call.options, () =>
                backend.query(call.query, call.options));
        },

        queryOne(...args) {
            const call = normalizeProviderCallArguments("queryOne", placeholderStyle, args);
            return invokeProviderOperation("queryOne", call.options, () =>
                backend.queryOne(call.query, call.options));
        },

        exec(...args) {
            const call = normalizeProviderCallArguments("exec", placeholderStyle, args);
            return invokeProviderOperation("exec", call.options, () =>
                backend.exec(call.query, call.options));
        },

        transaction(callback) {
            return runTransaction(callback);
        },

        __debug() {
            return Object.freeze({
                kind: "fake-data-provider",
                placeholderStyle,
                events: Object.freeze([...events]),
            });
        },
    };

    return Object.freeze(provider);
}

function sql(strings, ...values) {
    return createLoweredQuery(strings, values, { placeholderStyle: "question" });
}

sql.lower = function lower(strings, values = [], options) {
    if (!Array.isArray(values)) {
        throw new TypeError("Sloppy sql.lower values must be an array.");
    }

    return createLoweredQuery(strings, values, options);
};

sql.decimal = decimal;
sql.uuid = uuid;
sql.date = date;
sql.time = time;
sql.timestamp = timestamp;
sql.instant = instant;
sql.offsetDateTime = offsetDateTime;
sql.json = json;
sql.rawJson = rawJson;
sql.bytes = bytes;

Object.freeze(sql);

export { sql };

export const data = Object.freeze({
    createFakeProvider,
    lowerQueryTemplate: createLoweredQuery,
    isQuery: isLoweredQuery,
    values,
    sqlite,
    postgres,
    sqlserver,
});
