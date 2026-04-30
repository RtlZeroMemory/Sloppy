const QUERY_MARKER = "__sloppyQuery";
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

function normalizeQueryArguments(operation, placeholderStyle, args) {
    if (args.length === 1 && isLoweredQuery(args[0])) {
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
    if (access !== "read" && access !== "readwrite") {
        throw new TypeError("Sloppy sqlite.open access must be read or readwrite.");
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

function validateSqliteParams(params, operation) {
    if (params === undefined) {
        return [];
    }

    if (!Array.isArray(params)) {
        throw new TypeError(`Sloppy sqlite.${operation} parameters must be an array.`);
    }

    for (const param of params) {
        const type = typeof param;
        if (
            param !== null
            && type !== "string"
            && type !== "number"
            && type !== "boolean"
        ) {
            throw new TypeError(
                `Sloppy sqlite.${operation} parameters support only null, string, number, and boolean values.`,
            );
        }
    }

    return params;
}

function normalizeSqliteOperation(operation, args) {
    if (args.length === 1 && isLoweredQuery(args[0])) {
        return {
            text: args[0].text,
            parameters: validateSqliteParams(args[0].parameters, operation),
        };
    }

    if (typeof args[0] === "string") {
        if (args.length > 2) {
            throw new TypeError(`Sloppy sqlite.${operation} accepts sql and optional params.`);
        }

        if (args[0].length === 0) {
            throw new TypeError(`Sloppy sqlite.${operation} SQL must be a non-empty string.`);
        }

        return {
            text: args[0],
            parameters: validateSqliteParams(args[1], operation),
        };
    }

    return normalizeQueryArguments(operation, "question", args);
}

function createSqliteConnection(nativeBridge, handle) {
    const state = {
        closed: false,
        handle,
    };

    function assertOpen(operation) {
        if (state.closed) {
            throw createSqliteClosedError(operation);
        }
    }

    return Object.freeze({
        query(...args) {
            assertOpen("query");
            const query = normalizeSqliteOperation("query", args);
            return nativeBridge.query(state.handle, query.text, query.parameters);
        },

        queryOne(...args) {
            assertOpen("queryOne");
            const query = normalizeSqliteOperation("queryOne", args);
            return nativeBridge.queryOne(state.handle, query.text, query.parameters);
        },

        exec(...args) {
            assertOpen("exec");
            const query = normalizeSqliteOperation("exec", args);
            return nativeBridge.exec(state.handle, query.text, query.parameters);
        },

        close() {
            if (state.closed) {
                return;
            }

            nativeBridge.close(state.handle);
            state.closed = true;
        },

        __debug() {
            return Object.freeze({
                kind: "sqlite-connection",
                closed: state.closed,
                resource: Object.freeze({
                    slot: state.handle.slot,
                    generation: state.handle.generation,
                    kind: state.handle.kind,
                }),
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
        connectionString: redactConnectionString(options.connectionString),
        access,
        maxConnections,
        placeholderStyle: "postgres",
    });
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
        connectionString: redactOdbcConnectionString(options.connectionString),
        driver: extractOdbcDriverName(options.connectionString),
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
    return new Error(`sloppy: sqlite provider native bridge unavailable

Provider:
  sqlite

Operation:
  ${operation}

Reason:
  The native SQLite provider exists, but this JavaScript context did not install the V8 SQLite intrinsics.

Fix:
  Run through a V8-enabled Sloppy runtime that loads the SQLite bridge, or keep SQLite usage behind a documented deferral.`);
}

function createPostgresUnavailableError(operation, options) {
    const safeOptions = validatePostgresOpenOptions(options);
    return new Error(`sloppy: postgres provider native bridge unavailable

Provider:
  postgres

Operation:
  ${operation}

Connection:
  ${safeOptions.connectionString}

Reason:
  The native PostgreSQL provider exists for C/runtime tests, but stdlib-to-native database intrinsics are not wired yet.

Fix:
  Register PostgreSQL as a capability/service shape today, and use the native provider tests until the runtime bridge lands.`);
}

function createSqlServerUnavailableError(operation, options) {
    const safeOptions = validateSqlServerOpenOptions(options);
    return new Error(`sloppy: sqlserver provider native bridge unavailable

Provider:
  sqlserver

Operation:
  ${operation}

Connection:
  ${safeOptions.connectionString}

Reason:
  The native SQL Server provider exists for C/runtime tests through ODBC, but stdlib-to-native database intrinsics are not wired yet.

Fix:
  Register SQL Server as a capability/service shape today, and use the native provider tests until the runtime bridge lands.`);
}

function openSqlite(options) {
    const safeOptions = validateSqliteOpenOptions(
        typeof options === "string" ? { database: options } : options,
    );
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

function openPostgres(options) {
    throw createPostgresUnavailableError("open", options);
}

function openSqlServer(options) {
    throw createSqlServerUnavailableError("open", options);
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
        message: "Native SQL Server doctor diagnostics are available in the C provider tests until the stdlib bridge lands.",
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
    parameters: Object.freeze(["null", "string", "integer", "float", "boolean"]),
    transactions: "native-provider-only",
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
    supports: Object.freeze({
        connectionString: true,
        queryTemplates: true,
        parameters: Object.freeze(["null", "string", "integer", "float", "boolean"]),
        transactions: true,
        pooling: "skeleton",
        migrations: false,
        orm: false,
        nativeStdlibBridge: false,
    }),
    open: openPostgres,
    redactConnectionString,
    __debug() {
        return Object.freeze({
            provider: "postgres",
            placeholderStyle: "postgres",
            nativeStdlibBridge: false,
        });
    },
});

const sqlserver = Object.freeze({
    provider: "sqlserver",
    placeholderStyle: "question",
    supports: Object.freeze({
        connectionString: true,
        odbc: true,
        queryTemplates: true,
        parameters: Object.freeze(["null", "string", "integer", "float", "boolean"]),
        transactions: true,
        pooling: "skeleton",
        migrations: false,
        orm: false,
        nativeStdlibBridge: false,
    }),
    open: openSqlServer,
    doctor: doctorSqlServer,
    redactConnectionString: redactOdbcConnectionString,
    __debug() {
        return Object.freeze({
            provider: "sqlserver",
            placeholderStyle: "question",
            nativeStdlibBridge: false,
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
            return state.provider.query(normalizeQueryArguments("query", placeholderStyle, args));
        },

        queryOne(...args) {
            assertTransactionOpen(state, "queryOne");
            return state.provider.queryOne(normalizeQueryArguments("queryOne", placeholderStyle, args));
        },

        exec(...args) {
            assertTransactionOpen(state, "exec");
            return state.provider.exec(normalizeQueryArguments("exec", placeholderStyle, args));
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
        query(query) {
            if (definition.query === undefined) {
                missingProviderMethod("query");
            }

            return definition.query(query);
        },

        queryOne(query) {
            if (definition.queryOne !== undefined) {
                return definition.queryOne(query);
            }

            if (definition.query === undefined) {
                missingProviderMethod("queryOne");
            }

            return Promise.resolve(definition.query(query)).then((rows) => {
                if (rows == null) {
                    return null;
                }

                if (!Array.isArray(rows)) {
                    throw new TypeError("Sloppy fake data provider queryOne fallback expected query() to return an array.");
                }

                return rows[0] ?? null;
            });
        },

        exec(query) {
            if (definition.exec === undefined) {
                missingProviderMethod("exec");
            }

            return definition.exec(query);
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
            return backend.query(normalizeQueryArguments("query", placeholderStyle, args));
        },

        queryOne(...args) {
            return backend.queryOne(normalizeQueryArguments("queryOne", placeholderStyle, args));
        },

        exec(...args) {
            return backend.exec(normalizeQueryArguments("exec", placeholderStyle, args));
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

Object.freeze(sql);

export { sql };

export const data = Object.freeze({
    createFakeProvider,
    lowerQueryTemplate: createLoweredQuery,
    isQuery: isLoweredQuery,
    sqlite,
    postgres,
    sqlserver,
});
