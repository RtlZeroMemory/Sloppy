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

    if (typeof options.path !== "string" || options.path.length === 0) {
        throw new TypeError("Sloppy sqlite.open path must be a non-empty string.");
    }

    const access = options.access ?? "readwrite";
    if (access !== "read" && access !== "readwrite") {
        throw new TypeError("Sloppy sqlite.open access must be read or readwrite.");
    }

    return Object.freeze({
        provider: "sqlite",
        path: options.path,
        access,
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
  The native SQLite provider exists for C/runtime tests, but stdlib-to-native database intrinsics are not wired yet.

Fix:
  Register SQLite as a capability/service shape today, and use the native provider tests until the runtime bridge lands.`);
}

function openSqlite(options) {
    validateSqliteOpenOptions(options);
    throw createSqliteUnavailableError("open");
}

const sqlite = Object.freeze({
    provider: "sqlite",
    placeholderStyle: "question",
    supports: Object.freeze({
        memory: true,
        file: true,
        queryTemplates: true,
        parameters: Object.freeze(["null", "string", "integer", "float", "boolean"]),
        transactions: true,
        pooling: false,
        migrations: false,
        orm: false,
        nativeStdlibBridge: false,
    }),
    open: openSqlite,
    __debug() {
        return Object.freeze({
            provider: "sqlite",
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
});
