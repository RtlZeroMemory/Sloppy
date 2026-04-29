(() => {
    const TEXT_CONTENT_TYPE = "text/plain; charset=utf-8";
    const JSON_CONTENT_TYPE = "application/json; charset=utf-8";
    const PROBLEM_CONTENT_TYPE = "application/problem+json; charset=utf-8";

    function resolveStatus(options, defaultStatus) {
        const status = options?.status ?? defaultStatus;

        if (!Number.isInteger(status) || status < 100 || status > 999) {
            throw new TypeError("Sloppy Results status must be an integer from 100 to 999.");
        }

        return status;
    }

    function createResult(kind, body, contentType, options, defaultStatus) {
        const descriptor = {
            __sloppyResult: true,
            kind,
            status: resolveStatus(options, defaultStatus),
            contentType,
        };

        if (body !== undefined) {
            descriptor.body = body;
        }

        return Object.freeze(descriptor);
    }

    function requireSqliteBridge() {
        const bridge = globalThis.__sloppy?.data?.sqlite;

        if (bridge === undefined) {
            throw new Error(`sloppy: sqlite provider native bridge unavailable

Provider:
  sqlite

Operation:
  open

Reason:
  The V8 runtime did not install SQLite intrinsics.`);
        }

        return bridge;
    }

    function sqliteClosedError(operation) {
        return new Error(`sloppy: sqlite connection is closed

Provider:
  sqlite

Operation:
  ${operation}`);
    }

    function normalizeSqliteParams(params, operation) {
        if (params === undefined) {
            return [];
        }

        if (!Array.isArray(params)) {
            throw new TypeError(`Sloppy sqlite.${operation} parameters must be an array.`);
        }

        return params;
    }

    function normalizeSqliteQuery(operation, sql, params) {
        if (typeof sql !== "string" || sql.length === 0) {
            throw new TypeError(`Sloppy sqlite.${operation} SQL must be a non-empty string.`);
        }

        return {
            text: sql,
            parameters: normalizeSqliteParams(params, operation),
        };
    }

    function createSqliteConnection(bridge, handle) {
        const state = {
            closed: false,
            handle,
        };

        function assertOpen(operation) {
            if (state.closed) {
                throw sqliteClosedError(operation);
            }
        }

        return Object.freeze({
            exec(sql, params) {
                assertOpen("exec");
                const query = normalizeSqliteQuery("exec", sql, params);
                return bridge.exec(state.handle, query.text, query.parameters);
            },
            query(sql, params) {
                assertOpen("query");
                const query = normalizeSqliteQuery("query", sql, params);
                return bridge.query(state.handle, query.text, query.parameters);
            },
            queryOne(sql, params) {
                assertOpen("queryOne");
                const query = normalizeSqliteQuery("queryOne", sql, params);
                return bridge.queryOne(state.handle, query.text, query.parameters);
            },
            close() {
                if (state.closed) {
                    return;
                }

                state.closed = true;
                bridge.close(state.handle);
            },
        });
    }

    function normalizeProblem(problemOrMessage, status) {
        if (problemOrMessage === undefined) {
            return Object.freeze({ title: "Sloppy problem", status });
        }

        if (typeof problemOrMessage === "string") {
            return Object.freeze({ title: problemOrMessage, status });
        }

        if (problemOrMessage === null || typeof problemOrMessage !== "object" || Array.isArray(problemOrMessage)) {
            throw new TypeError("Sloppy Results.problem value must be a string or plain problem object.");
        }

        return Object.freeze({ status, ...problemOrMessage });
    }

    const Results = Object.freeze({
        text(body, options) {
            return createResult("text", String(body), TEXT_CONTENT_TYPE, options, 200);
        },
        json(value, options) {
            return createResult("json", value, JSON_CONTENT_TYPE, options, 200);
        },
        ok(value, options) {
            return createResult("json", value, JSON_CONTENT_TYPE, options, 200);
        },
        noContent() {
            return createResult("empty", undefined, undefined, undefined, 204);
        },
        problem(problemOrMessage, options) {
            const status = resolveStatus(options, 500);
            return createResult(
                "problem",
                normalizeProblem(problemOrMessage, status),
                PROBLEM_CONTENT_TYPE,
                { ...options, status },
                status,
            );
        },
    });

    const data = Object.freeze({
        sqlite: Object.freeze({
            open(pathOrOptions) {
                const databasePath =
                    typeof pathOrOptions === "string" ? pathOrOptions : pathOrOptions?.path;
                const access = typeof pathOrOptions === "object" && pathOrOptions !== null
                    ? pathOrOptions.access ?? "readwrite"
                    : "readwrite";

                if (typeof databasePath !== "string" || databasePath.length === 0) {
                    throw new TypeError("Sloppy sqlite.open path must be a non-empty string.");
                }

                if (access !== "read" && access !== "readwrite") {
                    throw new TypeError("Sloppy sqlite.open access must be read or readwrite.");
                }

                const bridge = requireSqliteBridge();
                return createSqliteConnection(bridge, bridge.open({ path: databasePath, access }));
            },
        }),
    });

    globalThis.__sloppy_runtime = Object.freeze({
        Results,
        data,
    });
})();
