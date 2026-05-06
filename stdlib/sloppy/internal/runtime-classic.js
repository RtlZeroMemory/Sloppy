(() => {
    const TEXT_CONTENT_TYPE = "text/plain; charset=utf-8";
    const JSON_CONTENT_TYPE = "application/json; charset=utf-8";
    const HTML_CONTENT_TYPE = "text/html; charset=utf-8";
    const PROBLEM_CONTENT_TYPE = "application/problem+json; charset=utf-8";

    function resolveStatus(options, defaultStatus) {
        const status = options?.status ?? defaultStatus;

        if (!Number.isInteger(status) || status < 100 || status > 999) {
            throw new TypeError("Sloppy Results status must be an integer from 100 to 999.");
        }

        return status;
    }

    function isPlainObject(value) {
        if (value === null || typeof value !== "object" || Array.isArray(value)) {
            return false;
        }

        const prototype = Object.getPrototypeOf(value);
        return prototype === Object.prototype || prototype === null;
    }

    function copyHeaders(options) {
        const headers = options?.headers;

        if (headers === undefined) {
            return undefined;
        }

        if (!isPlainObject(headers)) {
            throw new TypeError("Sloppy Results headers must be a plain object when provided.");
        }

        return Object.freeze({ ...headers });
    }

    function createResult(kind, body, contentType, options, defaultStatus, extra) {
        const descriptor = {
            __sloppyResult: true,
            kind,
            status: resolveStatus(options, defaultStatus),
            contentType,
            headers: copyHeaders(options),
            ...extra,
        };

        if (body !== undefined) {
            descriptor.body = body;
        }

        return Object.freeze(descriptor);
    }

    function requireSqliteBridge() {
        const bridge = globalThis.__sloppy?.data?.sqlite;

        if (bridge === undefined) {
            throw new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature provider.sqlite is inactive or unavailable

Provider:
  sqlite

Feature:
  provider.sqlite

Operation:
  open

Reason:
  The active Sloppy Plan did not enable the __sloppy.data.sqlite V8 intrinsic namespace.`);
        }

        return bridge;
    }

    function normalizeSqliteProviderToken(name) {
        if (typeof name !== "string" || name.length === 0) {
            throw new TypeError("Sloppy data.sqlite provider name must be a non-empty string.");
        }

        return name.includes(".") ? name : `data.${name}`;
    }

    function normalizeSqliteOpenOptions(options) {
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
            throw new TypeError(
                "Sloppy sqlite.open database and path must match when both are supplied.",
            );
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
        });
    }

    function sqliteClosedError(operation) {
        return new Error(`sloppy: sqlite connection is closed

Provider:
  sqlite

Operation:
  ${operation}`);
    }

    function sqliteTransactionClosedError(operation) {
        return new Error(`sloppy: sqlite transaction scope is closed

Provider:
  sqlite

Operation:
  ${operation}`);
    }

    function sqliteNestedTransactionError() {
        return new Error(`sloppy: sqlite nested transactions are not supported

Provider:
  sqlite

Operation:
  transaction`);
    }

    function sqliteTransactionActiveError(operation) {
        return new Error(`sloppy: sqlite transaction is active

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
            transactionActive: false,
        };

        function assertOpen(operation) {
            if (state.closed) {
                throw sqliteClosedError(operation);
            }
        }

        function createTransaction() {
            const txState = {
                closed: false,
            };

            function assertTransactionOpen(operation) {
                assertOpen(operation);
                if (txState.closed) {
                    throw sqliteTransactionClosedError(operation);
                }
            }

            const tx = Object.freeze({
                exec(sql, params) {
                    assertTransactionOpen("transaction.exec");
                    const query = normalizeSqliteQuery("exec", sql, params);
                    return bridge.transactionExec(state.handle, query.text, query.parameters);
                },
                query(sql, params) {
                    assertTransactionOpen("transaction.query");
                    const query = normalizeSqliteQuery("query", sql, params);
                    return bridge.transactionQuery(state.handle, query.text, query.parameters);
                },
                queryOne(sql, params) {
                    assertTransactionOpen("transaction.queryOne");
                    const query = normalizeSqliteQuery("queryOne", sql, params);
                    return bridge.transactionQueryOne(state.handle, query.text, query.parameters);
                },
                transaction() {
                    throw sqliteNestedTransactionError();
                },
            });

            return {
                tx,
                close() {
                    txState.closed = true;
                },
            };
        }

        function rollbackAfterCallbackError(error, transaction) {
            try {
                bridge.transactionRollback(state.handle);
            } catch {
                transaction.close();
                state.closed = true;
                try {
                    bridge.close(state.handle);
                } catch {
                    // Preserve the original callback or thenable error while preventing reuse.
                }
                throw error;
            }
            transaction.close();
            state.transactionActive = false;
            throw error;
        }

        function commitTransaction(transaction) {
            try {
                bridge.transactionCommit(state.handle);
            } catch (error) {
                transaction.close();
                state.closed = true;
                try {
                    bridge.close(state.handle);
                } catch {
                    // Keep the commit failure as the observable error.
                }
                throw error;
            }
            transaction.close();
            state.transactionActive = false;
        }

        function callbackResultThen(callbackResult, transaction) {
            if (
                callbackResult === null
                || typeof callbackResult !== "object" && typeof callbackResult !== "function"
            ) {
                return undefined;
            }

            try {
                return callbackResult.then;
            } catch (error) {
                return rollbackAfterCallbackError(error, transaction);
            }
        }

        function resolveThenable(callbackResult, then) {
            return new Promise((resolve, reject) => {
                try {
                    then.call(callbackResult, resolve, reject);
                } catch (error) {
                    reject(error);
                }
            });
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
            transaction(callback) {
                assertOpen("transaction");
                if (typeof callback !== "function") {
                    throw new TypeError("Sloppy sqlite.transaction callback must be a function.");
                }
                if (state.transactionActive) {
                    throw sqliteNestedTransactionError();
                }

                bridge.transactionBegin(state.handle);
                state.transactionActive = true;

                const transaction = createTransaction();
                let callbackResult;
                try {
                    callbackResult = callback(transaction.tx);
                } catch (error) {
                    return rollbackAfterCallbackError(error, transaction);
                }

                const then = callbackResultThen(callbackResult, transaction);

                if (typeof then !== "function") {
                    commitTransaction(transaction);
                    return callbackResult;
                }

                return resolveThenable(callbackResult, then).then(
                    (value) => {
                        commitTransaction(transaction);
                        return value;
                    },
                    (error) => {
                        return rollbackAfterCallbackError(error, transaction);
                    },
                );
            },
            close() {
                if (state.closed) {
                    return;
                }
                if (state.transactionActive) {
                    throw sqliteTransactionActiveError("close");
                }

                bridge.close(state.handle);
                state.closed = true;
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
        html(body, options) {
            return createResult("html", String(body), HTML_CONTENT_TYPE, options, 200);
        },
        ok(value, options) {
            return createResult("json", value, JSON_CONTENT_TYPE, options, 200);
        },
        created(location, value, options) {
            if (typeof location !== "string" || location.length === 0) {
                throw new TypeError("Sloppy Results.created location must be a non-empty string.");
            }

            return createResult(
                "json",
                value,
                JSON_CONTENT_TYPE,
                { status: 201, ...options },
                201,
                { location },
            );
        },
        accepted(value, options) {
            return createResult("json", value, JSON_CONTENT_TYPE, { status: 202, ...options }, 202);
        },
        noContent() {
            return createResult("empty", undefined, undefined, undefined, 204);
        },
        notFound(valueOrProblem, options) {
            return createResult(
                "json",
                valueOrProblem,
                JSON_CONTENT_TYPE,
                { status: 404, ...options },
                404,
            );
        },
        badRequest(valueOrProblem, options) {
            return createResult(
                "json",
                valueOrProblem,
                JSON_CONTENT_TYPE,
                { status: 400, ...options },
                400,
            );
        },
        status(statusCode, value, options) {
            if (value === undefined) {
                return createResult(
                    "empty",
                    undefined,
                    undefined,
                    { ...options, status: statusCode },
                    statusCode,
                );
            }

            return createResult(
                "json",
                value,
                JSON_CONTENT_TYPE,
                { ...options, status: statusCode },
                statusCode,
            );
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

    function sqlite(name) {
        const bridge = requireSqliteBridge();
        return createSqliteConnection(bridge, bridge.open({
            provider: normalizeSqliteProviderToken(name),
        }));
    }

    sqlite.open = function open(options) {
        const bridge = requireSqliteBridge();
        const safeOptions = normalizeSqliteOpenOptions(options);
        return createSqliteConnection(bridge, bridge.open(safeOptions));
    };

    Object.freeze(sqlite);

    const data = Object.freeze({
        sqlite,
    });

    function requireFsBridge(operation) {
        const bridge = globalThis.__sloppy?.fs;

        if (bridge === undefined) {
            throw new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature stdlib.fs is inactive or unavailable

Feature:
  stdlib.fs

Operation:
  ${operation}

Reason:
  The active Sloppy Plan did not enable the __sloppy.fs V8 intrinsic namespace.`);
        }

        return bridge;
    }

    function validateFsPath(path, operation) {
        if (typeof path !== "string" || path.length === 0) {
            throw new TypeError(`Sloppy File.${operation} path must be a non-empty string.`);
        }
        return path;
    }

    function validateFsBytes(value, operation) {
        if (!(value instanceof Uint8Array)) {
            throw new TypeError(`Sloppy File.${operation} bytes must be a Uint8Array.`);
        }
        return value;
    }

    function validateFsOverwrite(options) {
        if (options === undefined) {
            return Object.freeze({ overwrite: false });
        }
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy File copy/move options must be a plain object.");
        }
        if (options.overwrite !== undefined && typeof options.overwrite !== "boolean") {
            throw new TypeError("Sloppy File overwrite option must be boolean.");
        }
        return Object.freeze({ overwrite: options.overwrite ?? false });
    }

    function validateFsRecursive(options) {
        if (options === undefined) {
            return Object.freeze({ recursive: false });
        }
        if (!isPlainObject(options) || typeof (options.recursive ?? false) !== "boolean") {
            throw new TypeError("Sloppy Directory recursive option must be boolean.");
        }
        return Object.freeze({ recursive: options.recursive ?? false });
    }

    function validateFsOpenOptions(options) {
        if (options === undefined) {
            return Object.freeze({ access: "read", create: false });
        }
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy File.open options must be a plain object.");
        }
        const access = options.access ?? "read";
        const create = options.create ?? access !== "read";
        if (!["read", "write", "readwrite", "append"].includes(access) || typeof create !== "boolean") {
            throw new TypeError("Sloppy File.open options are invalid.");
        }
        return Object.freeze({ access, create });
    }

    function validateFsWatchOptions(options, directory) {
        if (options === undefined) {
            return Object.freeze({ recursive: false, queueCapacity: 16, snapshotCapacity: directory ? 128 : 1 });
        }
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy filesystem watch options must be a plain object.");
        }
        const recursive = options.recursive ?? false;
        const queueCapacity = options.queueCapacity ?? 16;
        const snapshotCapacity = options.snapshotCapacity ?? (directory ? 128 : 1);
        if (typeof recursive !== "boolean") {
            throw new TypeError("Sloppy filesystem watch recursive option must be boolean.");
        }
        if (!Number.isInteger(queueCapacity) || queueCapacity < 1 || queueCapacity > 256) {
            throw new TypeError("Sloppy filesystem watch queueCapacity must be 1..256.");
        }
        if (!Number.isInteger(snapshotCapacity) || snapshotCapacity < 1 || snapshotCapacity > 1024) {
            throw new TypeError("Sloppy filesystem watch snapshotCapacity must be 1..1024.");
        }
        return Object.freeze({ recursive, queueCapacity, snapshotCapacity });
    }

    function stringifyFsJson(value, options) {
        if (options === undefined) {
            return JSON.stringify(value);
        }
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy File.writeJson options must be a plain object.");
        }
        const indent = options.indent ?? undefined;
        if (indent !== undefined && (!Number.isInteger(indent) || indent < 0 || indent > 10)) {
            throw new TypeError("Sloppy File.writeJson indent must be an integer from 0 to 10.");
        }
        return JSON.stringify(value, null, indent);
    }

    function fsShouldAtomic(options) {
        if (options === undefined) {
            return false;
        }
        if (!isPlainObject(options) || typeof (options.atomic ?? false) !== "boolean") {
            throw new TypeError("Sloppy File atomic option must be boolean.");
        }
        return options.atomic === true;
    }

    function validateFsTimeoutMs(value, operation) {
        if (value === undefined) {
            return undefined;
        }
        return validateDelayMs(value, `${operation} timeoutMs`);
    }

    function createFsTimeoutBudgetOptions(options, operation) {
        const timeoutMs = validateFsTimeoutMs(options?.timeoutMs, operation);
        if (timeoutMs === undefined) {
            return () => options;
        }
        const expiresAtMs = Date.now() + timeoutMs;
        return () => ({
            ...options,
            timeoutMs: Math.max(0, expiresAtMs - Date.now()),
        });
    }

    function isFsTimeTerminalError(error) {
        return (
            error instanceof CancelledError ||
            error instanceof TimeoutError ||
            error?.name === "AbortError" ||
            error?.name === "CancelledError" ||
            error?.name === "TimeoutError"
        );
    }

    function applyFsTimeOptions(createPromise, options, operation) {
        if (options !== undefined && !isPlainObject(options)) {
            throw new TypeError(`${operation} options must be a plain object.`);
        }
        const signal = options?.signal;
        const timeoutMs = validateFsTimeoutMs(options?.timeoutMs, operation);
        const deadline = options?.deadline;
        if (isCancellationSignal(signal) && signal.aborted) {
            return Promise.reject(cancelledError(signal.reason));
        }
        const deadlineMs = deadlineDelayMs(deadline);
        if (timeoutMs === 0 || deadlineMs <= 0) {
            return Time.timeout(Promise.resolve(), { afterMs: timeoutMs, deadline });
        }
        const promise = createPromise();
        if (timeoutMs !== undefined || deadlineMs !== Infinity) {
            return Time.timeout(Promise.resolve(promise), { afterMs: timeoutMs, deadline, signal });
        }
        return raceCancellation(Promise.resolve(promise), signal);
    }

    const File = Object.freeze({
        readText(path, options) {
            return applyFsTimeOptions(
                () => requireFsBridge("readText").readText(validateFsPath(path, "readText")),
                options,
                "File.readText",
            );
        },
        readBytes(path, options) {
            return applyFsTimeOptions(
                () => requireFsBridge("readBytes").readBytes(validateFsPath(path, "readBytes")),
                options,
                "File.readBytes",
            );
        },
        async readJson(path, options) {
            return JSON.parse(await File.readText(path, options));
        },
        writeText(path, text, options) {
            if (typeof text !== "string") {
                throw new TypeError("Sloppy File.writeText text must be a string.");
            }
            if (fsShouldAtomic(options)) {
                return applyFsTimeOptions(
                    () => requireFsBridge("atomicWriteText").atomicWriteText(validateFsPath(path, "writeText"), text),
                    options,
                    "File.writeText",
                );
            }
            return applyFsTimeOptions(
                () => requireFsBridge("writeText").writeText(validateFsPath(path, "writeText"), text),
                options,
                "File.writeText",
            );
        },
        writeBytes(path, bytes, options) {
            const checked = validateFsBytes(bytes, "writeBytes");
            if (fsShouldAtomic(options)) {
                return applyFsTimeOptions(
                    () => requireFsBridge("atomicWriteBytes").atomicWriteBytes(validateFsPath(path, "writeBytes"), checked),
                    options,
                    "File.writeBytes",
                );
            }
            return applyFsTimeOptions(
                () => requireFsBridge("writeBytes").writeBytes(
                    validateFsPath(path, "writeBytes"),
                    checked,
                ),
                options,
                "File.writeBytes",
            );
        },
        writeJson(path, value, options) {
            return File.writeText(path, stringifyFsJson(value, options), options);
        },
        appendText(path, text, options) {
            if (typeof text !== "string") {
                throw new TypeError("Sloppy File.appendText text must be a string.");
            }
            return applyFsTimeOptions(
                () => requireFsBridge("appendText").appendText(validateFsPath(path, "appendText"), text),
                options,
                "File.appendText",
            );
        },
        appendBytes(path, bytes, options) {
            return applyFsTimeOptions(
                () => requireFsBridge("appendBytes").appendBytes(
                    validateFsPath(path, "appendBytes"),
                    validateFsBytes(bytes, "appendBytes"),
                ),
                options,
                "File.appendBytes",
            );
        },
        exists(path, options) {
            return applyFsTimeOptions(
                () => requireFsBridge("exists").exists(validateFsPath(path, "exists")),
                options,
                "File.exists",
            );
        },
        stat(path, options) {
            return applyFsTimeOptions(
                () => requireFsBridge("stat").stat(validateFsPath(path, "stat")),
                options,
                "File.stat",
            );
        },
        copy(fromPath, toPath, options) {
            return applyFsTimeOptions(
                () => requireFsBridge("copy").copy(
                    validateFsPath(fromPath, "copy"),
                    validateFsPath(toPath, "copy"),
                    validateFsOverwrite(options),
                ),
                options,
                "File.copy",
            );
        },
        move(fromPath, toPath, options) {
            return applyFsTimeOptions(
                () => requireFsBridge("move").move(
                    validateFsPath(fromPath, "move"),
                    validateFsPath(toPath, "move"),
                    validateFsOverwrite(options),
                ),
                options,
                "File.move",
            );
        },
        delete(path, options) {
            return applyFsTimeOptions(
                () => requireFsBridge("delete").delete(validateFsPath(path, "delete")),
                options,
                "File.delete",
            );
        },
        async open(path, options) {
            const checked = validateFsOpenOptions(options);
            return new FileHandle(await applyFsTimeOptions(
                () => requireFsBridge("openHandle").openHandle(
                    validateFsPath(path, "open"),
                    checked.access,
                    checked.create,
                ),
                options,
                "File.open",
            ));
        },
        async watch(path, options) {
            const checked = validateFsWatchOptions(options, false);
            return new FileWatcher(await applyFsTimeOptions(
                () => requireFsBridge("watch").watch(
                    validateFsPath(path, "watch"),
                    false,
                    checked,
                ),
                options,
                "File.watch",
            ));
        },
        createSymlink(targetPath, linkPath, options) {
            const directory = options?.directory ?? false;
            if (typeof directory !== "boolean") {
                throw new TypeError("Sloppy File.createSymlink directory option must be boolean.");
            }
            return applyFsTimeOptions(
                () => requireFsBridge("symlink").symlink(
                    validateFsPath(targetPath, "createSymlink"),
                    validateFsPath(linkPath, "createSymlink"),
                    directory,
                ),
                options,
                "File.createSymlink",
            );
        },
        readLink(path, options) {
            return applyFsTimeOptions(
                () => requireFsBridge("readLink").readLink(validateFsPath(path, "readLink")),
                options,
                "File.readLink",
            );
        },
        createTemp(directory, options) {
            const prefix = options?.prefix ?? "sloppy-";
            if (typeof prefix !== "string" || prefix.length === 0) {
                throw new TypeError("Sloppy File.createTemp prefix must be a non-empty string.");
            }
            return applyFsTimeOptions(
                () => requireFsBridge("tempFile").tempFile(validateFsPath(directory, "createTemp"), prefix),
                options,
                "File.createTemp",
            );
        },
    });

    async function isFsSymlink(path, options) {
        try {
            await File.readLink(path, options);
            return true;
        }
        catch (error) {
            if (isFsTimeTerminalError(error)) {
                throw error;
            }
            return false;
        }
    }

    const Directory = Object.freeze({
        create(path, options) {
            const checked = validateFsRecursive(options);
            return applyFsTimeOptions(
                () => requireFsBridge("directoryCreate").directoryCreate(
                    validateFsPath(path, "create"),
                    checked.recursive,
                ),
                options,
                "Directory.create",
            );
        },
        list(path, options) {
            return applyFsTimeOptions(
                () => requireFsBridge("directoryList").directoryList(validateFsPath(path, "list")),
                options,
                "Directory.list",
            );
        },
        async *walk(path, options) {
            const followSymlinks = options?.followSymlinks ?? false;
            if (typeof followSymlinks !== "boolean") {
                throw new TypeError("Sloppy Directory.walk followSymlinks option must be boolean.");
            }
            const optionsForStep = createFsTimeoutBudgetOptions(options, "Directory.walk");
            for (const entry of await Directory.list(path, optionsForStep())) {
                yield entry;
                if (entry.kind === "directory") {
                    const child = `${path.replace(/[\\/]$/, "")}/${entry.name}`;
                    if (!followSymlinks && await isFsSymlink(child, optionsForStep())) {
                        continue;
                    }
                    for await (const nested of Directory.walk(child, {
                        ...optionsForStep(),
                        followSymlinks,
                    })) {
                        yield { ...nested, name: `${entry.name}/${nested.name}` };
                    }
                }
            }
        },
        delete(path, options) {
            const checked = validateFsRecursive(options);
            return applyFsTimeOptions(
                () => requireFsBridge("directoryDelete").directoryDelete(
                    validateFsPath(path, "delete"),
                    checked.recursive,
                ),
                options,
                "Directory.delete",
            );
        },
        async exists(path, options) {
            const stat = await File.stat(path, options);
            return stat.exists && stat.kind === "directory";
        },
        createTemp(directory, options) {
            const prefix = options?.prefix ?? "sloppy-";
            if (typeof prefix !== "string" || prefix.length === 0) {
                throw new TypeError("Sloppy Directory.createTemp prefix must be a non-empty string.");
            }
            return applyFsTimeOptions(
                () => requireFsBridge("tempDirectory").tempDirectory(validateFsPath(directory, "createTemp"), prefix),
                options,
                "Directory.createTemp",
            );
        },
        async watch(path, options) {
            const checked = validateFsWatchOptions(options, true);
            return new FileWatcher(await applyFsTimeOptions(
                () => requireFsBridge("watch").watch(
                    validateFsPath(path, "watch"),
                    true,
                    checked,
                ),
                options,
                "Directory.watch",
            ));
        },
    });

    const Path = Object.freeze({
        classify(path) {
            validateFsPath(path, "classify");
            if (/^\.[\\/]/.test(path)) {
                return "project-relative";
            }
            if (/^(?:[A-Za-z]:[\\/]|[\\/])/.test(path)) {
                return "absolute";
            }
            if (/^[A-Za-z][A-Za-z0-9_.-]*:[\\/]/.test(path)) {
                return "named-root";
            }
            return "invalid";
        },
    });

    class FileHandle {
        constructor(id) {
            this._id = Object.freeze({ slot: id.slot, generation: id.generation });
        }
        readBytes(maxBytes = 64 * 1024, options) {
            if (!Number.isInteger(maxBytes) || maxBytes <= 0 || maxBytes > 1024 * 1024) {
                throw new TypeError("Sloppy FileHandle.readBytes maxBytes must be 1..1048576.");
            }
            return applyFsTimeOptions(
                () => requireFsBridge("handleRead").handleRead(this._id, maxBytes),
                options,
                "FileHandle.readBytes",
            );
        }
        async readText(maxBytes, options) {
            return new TextDecoder().decode(await this.readBytes(maxBytes, options));
        }
        writeBytes(bytes, options) {
            return applyFsTimeOptions(
                () => requireFsBridge("handleWriteBytes").handleWriteBytes(
                    this._id,
                    validateFsBytes(bytes, "writeBytes"),
                ),
                options,
                "FileHandle.writeBytes",
            );
        }
        writeText(text, options) {
            if (typeof text !== "string") {
                throw new TypeError("Sloppy FileHandle.writeText text must be a string.");
            }
            return applyFsTimeOptions(
                () => requireFsBridge("handleWriteText").handleWriteText(this._id, text),
                options,
                "FileHandle.writeText",
            );
        }
        seek(offset, origin = "start", options) {
            if (!Number.isInteger(offset) || !["start", "current", "end"].includes(origin)) {
                throw new TypeError("Sloppy FileHandle.seek requires an integer offset and valid origin.");
            }
            return applyFsTimeOptions(
                () => requireFsBridge("handleSeek").handleSeek(this._id, offset, origin),
                options,
                "FileHandle.seek",
            );
        }
        truncate(size, options) {
            if (!Number.isInteger(size) || size < 0) {
                throw new TypeError("Sloppy FileHandle.truncate size must be a non-negative integer.");
            }
            return applyFsTimeOptions(
                () => requireFsBridge("handleTruncate").handleTruncate(this._id, size),
                options,
                "FileHandle.truncate",
            );
        }
        flush(options) {
            return applyFsTimeOptions(
                () => requireFsBridge("handleFlush").handleFlush(this._id),
                options,
                "FileHandle.flush",
            );
        }
        sync(options) {
            return applyFsTimeOptions(
                () => requireFsBridge("handleSync").handleSync(this._id),
                options,
                "FileHandle.sync",
            );
        }
        close() {
            return requireFsBridge("handleClose").handleClose(this._id);
        }
        async *readChunks(options) {
            const chunkSize = options?.chunkSize ?? 64 * 1024;
            const optionsForRead = createFsTimeoutBudgetOptions(options, "FileHandle.readChunks");
            for (;;) {
                const chunk = await this.readBytes(chunkSize, optionsForRead());
                if (chunk.byteLength === 0) {
                    return;
                }
                yield chunk;
            }
        }
        async *readLines(options) {
            const decoder = new TextDecoder();
            const newline = options?.newline ?? "\n";
            const maxLineLength = options?.maxLineLength ?? 1024 * 1024;
            let buffered = "";
            if (typeof newline !== "string" || newline.length === 0) {
                throw new TypeError(
                    "Sloppy FileHandle.readLines newline must be a non-empty string.",
                );
            }
            for await (const chunk of this.readChunks(options)) {
                buffered += decoder.decode(chunk, { stream: true });
                if (buffered.length > maxLineLength) {
                    throw new Error(
                        "SLOPPY_E_LIMIT_EXCEEDED: filesystem line exceeds maxLineLength.",
                    );
                }
                let index = buffered.indexOf(newline);
                while (index !== -1) {
                    yield buffered.slice(0, index).replace(/\r$/, "");
                    buffered = buffered.slice(index + newline.length);
                    index = buffered.indexOf(newline);
                }
            }
            buffered += decoder.decode();
            if (buffered.length !== 0) {
                yield buffered;
            }
        }
    }
    class FileWatcher {
        constructor(id) {
            this._id = Object.freeze({ slot: id.slot, generation: id.generation });
            this._closed = false;
        }
        async nextEvent(options) {
            if (this._closed) {
                return null;
            }
            if (options !== undefined && !isPlainObject(options)) {
                throw new TypeError("Sloppy FileWatcher.nextEvent options must be a plain object.");
            }
            return applyFsTimeOptions(
                () => requireFsBridge("watchNext").watchNext(this._id),
                options,
                "FileWatcher.nextEvent",
            );
        }
        async close() {
            if (this._closed) {
                return;
            }
            this._closed = true;
            await requireFsBridge("watchClose").watchClose(this._id);
        }
        [Symbol.asyncIterator]() {
            return {
                next: async () => {
                    while (!this._closed) {
                        const event = await this.nextEvent();
                        if (event !== null) {
                            return { done: false, value: event };
                        }
                    }
                    return { done: true, value: undefined };
                },
                return: async () => {
                    await this.close();
                    return { done: true, value: undefined };
                },
            };
        }
    }

    class SloppyTimeError extends Error {
        constructor(name, message, options = undefined) {
            super(message, options);
            this.name = name;
            if (options && Object.prototype.hasOwnProperty.call(options, "reason")) {
                this.reason = options.reason;
            }
        }
    }

    class TimeoutError extends SloppyTimeError {
        constructor(message = "Sloppy time operation exceeded its deadline.", options) {
            super("TimeoutError", message, options);
        }
    }

    class CancelledError extends SloppyTimeError {
        constructor(message = "Sloppy time operation was cancelled.", options) {
            super("CancelledError", message, options);
        }
    }

    class InvalidDeadlineError extends SloppyTimeError {
        constructor(message = "Sloppy deadline is invalid.", options) {
            super("InvalidDeadlineError", message, options);
        }
    }

    class TimerDisposedError extends SloppyTimeError {
        constructor(message = "Sloppy timer resource was disposed.", options) {
            super("TimerDisposedError", message, options);
        }
    }

    const MAX_DELAY_MS = 0xffffffff;
    const NATIVE_TIMER_DISPOSED_MESSAGE = "Sloppy timer was disposed before completion";
    const INTERVAL_UNITS_MS = Object.freeze({
        ms: 1,
        s: 1000,
        m: 60 * 1000,
        h: 60 * 60 * 1000,
    });

    function unavailableTimeFeature(operation) {
        throw new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature stdlib.time is inactive or unavailable

Feature:
  stdlib.time

Operation:
  ${operation}

Reason:
  CORE-TIME-01.C/D/G requires the V8 native time bridge for runtime scheduling.`);
    }

    function nativeTime(operation) {
        const bridge = globalThis.__sloppy?.time;
        if (bridge === undefined || bridge === null) {
            unavailableTimeFeature(operation);
        }
        return bridge;
    }

    function validateDelayMs(ms, operation) {
        if (typeof ms !== "number" || !Number.isFinite(ms) || ms < 0 || ms > MAX_DELAY_MS) {
            throw new InvalidDeadlineError(
                `${operation} requires a finite non-negative millisecond delay no greater than ${MAX_DELAY_MS}.`,
            );
        }
        return Math.ceil(ms);
    }

    function monotonicNowMs() {
        const bridge = globalThis.__sloppy?.time;
        return bridge && typeof bridge.monotonicMs === "function" ? bridge.monotonicMs() : Date.now();
    }

    function timeoutError(reason = undefined) {
        return reason instanceof TimeoutError
            ? reason
            : new TimeoutError("Sloppy time operation exceeded its deadline.", { reason });
    }

    function cancelledError(reason = undefined) {
        return reason instanceof CancelledError
            ? reason
            : new CancelledError("Sloppy time operation was cancelled.", { reason });
    }

    function normalizeNativeTimerError(error) {
        if (error instanceof TimerDisposedError) {
            throw error;
        }
        if (error instanceof Error && error.message === NATIVE_TIMER_DISPOSED_MESSAGE) {
            throw new TimerDisposedError(error.message, { reason: error });
        }
        throw error;
    }

    function timerDisposedError(reason = undefined) {
        return reason instanceof TimerDisposedError
            ? reason
            : new TimerDisposedError("Sloppy timer resource was disposed.", { reason });
    }

    class CancellationSignal {
        constructor() {
            this.aborted = false;
            this.reason = undefined;
            this._listeners = new Set();
            Object.seal(this);
        }

        throwIfCancelled() {
            if (this.aborted) {
                throw cancelledError(this.reason);
            }
        }

        addEventListener(type, listener) {
            if (type !== "abort" || typeof listener !== "function") {
                return;
            }
            this._listeners.add(listener);
        }

        removeEventListener(type, listener) {
            if (type === "abort") {
                this._listeners.delete(listener);
            }
        }

        _subscribe(listener) {
            if (typeof listener !== "function") {
                return () => {};
            }
            if (this.aborted) {
                listener(this.reason);
                return () => {};
            }
            this._listeners.add(listener);
            return () => {
                this._listeners.delete(listener);
            };
        }

        _cancel(reason) {
            if (this.aborted) {
                return false;
            }
            this.aborted = true;
            this.reason = reason;
            const listeners = Array.from(this._listeners);
            this._listeners.clear();
            const errors = [];
            for (const listener of listeners) {
                try {
                    listener(reason);
                } catch (error) {
                    errors.push(error);
                }
            }
            if (errors.length > 0) {
                throw new AggregateError(errors, "one or more abort listeners threw");
            }
            return true;
        }
    }

    function isCancellationSignal(value) {
        return (
            value instanceof CancellationSignal ||
            (value !== null &&
                typeof value === "object" &&
                typeof value.aborted === "boolean" &&
                ("reason" in value || typeof value.addEventListener === "function"))
        );
    }

    function subscribeCancellation(signal, listener) {
        if (!isCancellationSignal(signal)) {
            return () => {};
        }
        if (signal.aborted) {
            listener(signal.reason);
            return () => {};
        }
        if (typeof signal._subscribe === "function") {
            return signal._subscribe(listener);
        }
        if (typeof signal.addEventListener === "function") {
            const wrapped = () => listener(signal.reason);
            signal.addEventListener("abort", wrapped);
            return () => signal.removeEventListener?.("abort", wrapped);
        }
        return () => {};
    }

    class DeadlineValue {
        constructor(expiresAtMonotonicMs, kind) {
            this.expiresAtMonotonicMs = expiresAtMonotonicMs;
            this.kind = kind;
            Object.freeze(this);
        }

        remainingMs() {
            if (this.expiresAtMonotonicMs === Infinity) {
                return Infinity;
            }
            return Math.max(0, this.expiresAtMonotonicMs - monotonicNowMs());
        }

        get expired() {
            return this.remainingMs() <= 0;
        }
    }

    const Deadline = Object.freeze({
        after(ms) {
            return new DeadlineValue(
                monotonicNowMs() + validateDelayMs(ms, "Deadline.after"),
                "after",
            );
        },

        at(dateOrUnixMs) {
            const unixMs =
                dateOrUnixMs instanceof Date ? dateOrUnixMs.getTime() : Number(dateOrUnixMs);
            if (!Number.isFinite(unixMs)) {
                throw new InvalidDeadlineError(
                    "Deadline.at requires a finite Date or Unix millisecond value.",
                );
            }
            return Deadline.after(Math.max(0, unixMs - Date.now()));
        },

        never() {
            return new DeadlineValue(Infinity, "never");
        },
    });

    function deadlineDelayMs(deadline) {
        if (deadline === undefined || deadline === null) {
            return Infinity;
        }
        if (typeof deadline.remainingMs !== "function") {
            throw new InvalidDeadlineError(
                "Time operation deadline must come from Deadline.after, Deadline.at, or Deadline.never.",
            );
        }
        return deadline.remainingMs();
    }

    class CancellationController {
        constructor(options = undefined) {
            this.signal = new CancellationSignal();
            this._disposed = false;
            this._cleanups = [];
            Object.seal(this);

            const linked = options?.signal ?? options?.signals;
            const signals = Array.isArray(linked) ? linked : linked === undefined ? [] : [linked];
            for (const signal of signals) {
                this._cleanups.push(subscribeCancellation(signal, (reason) => this.cancel(reason)));
            }

            if (options?.timeoutMs !== undefined) {
                const timeoutMs = validateDelayMs(options.timeoutMs, "CancellationController timeout");
                Time.delay(timeoutMs)
                    .then(() => this.cancel(timeoutError()))
                    .catch(() => {});
            }
            if (options?.deadline !== undefined) {
                const remaining = deadlineDelayMs(options.deadline);
                if (remaining <= 0) {
                    this.cancel(timeoutError(options.deadline));
                } else if (remaining !== Infinity) {
                    Time.delay(remaining)
                        .then(() => this.cancel(timeoutError(options.deadline)))
                        .catch(() => {});
                }
            }
        }

        cancel(reason = "cancelled") {
            if (this._disposed) {
                throw new TimerDisposedError("CancellationController was disposed.");
            }
            return this.signal._cancel(reason);
        }

        dispose() {
            if (this._disposed) {
                return;
            }
            this._disposed = true;
            for (const cleanup of this._cleanups.splice(0)) {
                cleanup();
            }
        }

        static linked(...signals) {
            return new CancellationController({ signals });
        }

        static timeout(timeoutMs, options = undefined) {
            return new CancellationController({ ...options, timeoutMs });
        }
    }

    function raceCancellation(promise, signal) {
        if (!isCancellationSignal(signal)) {
            return promise;
        }
        if (signal.aborted) {
            return Promise.reject(cancelledError(signal.reason));
        }
        return new Promise((resolve, reject) => {
            const cleanup = subscribeCancellation(signal, (reason) => {
                cleanup();
                reject(cancelledError(reason));
            });
            promise.then(
                (value) => {
                    cleanup();
                    resolve(value);
                },
                (error) => {
                    cleanup();
                    reject(error);
                },
            );
        });
    }

    function cancelAndDisposeController(controller, reason) {
        try {
            controller.cancel(reason);
        } catch (_) {
            // Cleanup paths must not be derailed by user abort listeners.
        }
        controller.dispose();
    }

    function parseIntervalMs(value, operation) {
        if (typeof value === "number") {
            const ms = validateDelayMs(value, operation);
            if (ms <= 0) {
                throw new InvalidDeadlineError(`${operation} requires an interval greater than 0.`);
            }
            return ms;
        }
        if (typeof value === "string") {
            const match = /^(\d+(?:\.\d+)?)(ms|s|m|h)$/.exec(value.trim());
            if (match === null) {
                throw new InvalidDeadlineError(
                    `${operation} requires a millisecond number or interval string such as "500ms", "5s", "5m", or "1h".`,
                );
            }
            return parseIntervalMs(Number(match[1]) * INTERVAL_UNITS_MS[match[2]], operation);
        }
        throw new InvalidDeadlineError(`${operation} requires a millisecond number or interval string.`);
    }

    function validateMaxTicks(maxTicks, operation) {
        if (maxTicks === undefined) {
            return Infinity;
        }
        if (!Number.isInteger(maxTicks) || maxTicks < 0) {
            throw new InvalidDeadlineError(`${operation} maxTicks must be a non-negative integer.`);
        }
        return maxTicks;
    }

    function clockNow(clock) {
        return clock && typeof clock.now === "function" ? clock.now() : new Date();
    }

    function clockMonotonicNowMs(clock) {
        return clock && typeof clock.monotonicNowMs === "function"
            ? clock.monotonicNowMs()
            : monotonicNowMs();
    }

    function validateClockDeadlineOptions(options, operation) {
        if (options?.clock !== undefined && options.clock !== null && options?.deadline !== undefined) {
            throw new InvalidDeadlineError(
                `${operation} does not support deadline with an injected clock; use a duration option with that clock.`,
            );
        }
    }

    function delayWithDeadline(ms, options = undefined, operation = "Time.delay") {
        const delayMs = validateDelayMs(ms, operation);
        if (isCancellationSignal(options?.signal) && options.signal.aborted) {
            return Promise.reject(cancelledError(options.signal.reason));
        }
        validateClockDeadlineOptions(options, operation);
        const remaining = deadlineDelayMs(options?.deadline);
        if (remaining <= 0) {
            return Promise.reject(timeoutError(options?.deadline));
        }

        const actualDelay = Math.min(delayMs, remaining);
        if (options?.clock !== undefined && options.clock !== null) {
            if (typeof options.clock.delay !== "function") {
                return Promise.reject(
                    new InvalidDeadlineError("Time clock providers must expose delay(ms, options)."),
                );
            }
            return options.clock.delay(actualDelay, { signal: options?.signal }).then(() => {
                if (actualDelay < delayMs) {
                    throw timeoutError(options?.deadline);
                }
            });
        }

        const promise = nativeTime("Time.delay")
            .delay(actualDelay)
            .then(() => {
                if (actualDelay < delayMs) {
                    throw timeoutError(options?.deadline);
                }
            })
            .catch(normalizeNativeTimerError);
        return raceCancellation(promise, options?.signal);
    }

    function systemDelay(ms, options = undefined) {
        return delayWithDeadline(ms, { ...options, clock: undefined });
    }

    class TimeInterval {
        constructor(ms, options = undefined) {
            this._intervalMs = parseIntervalMs(ms, "Time.interval");
            this._clock = options?.clock;
            this._signal = options?.signal;
            this._immediate = options?.immediate === true;
            this._maxTicks = validateMaxTicks(options?.maxTicks, "Time.interval");
            this._ticks = 0;
            this._stopped = false;
            this._nextInFlight = null;
            this._cleanup = subscribeCancellation(this._signal, () => {
                this._stopped = true;
            });
        }

        [Symbol.asyncIterator]() {
            return this;
        }

        next() {
            if (this._nextInFlight !== null) {
                return Promise.reject(
                    new Error("Time.interval does not support overlapping next() calls."),
                );
            }
            this._nextInFlight = this._nextImpl();
            return this._nextInFlight.finally(() => {
                this._nextInFlight = null;
            });
        }

        async _nextImpl() {
            if (this._stopped || this._ticks >= this._maxTicks) {
                this._cleanup();
                return { done: true, value: undefined };
            }
            if (!(this._immediate && this._ticks === 0)) {
                try {
                    await Time.delay(this._intervalMs, {
                        clock: this._clock,
                        signal: this._signal,
                    });
                } catch (error) {
                    if (error instanceof CancelledError) {
                        this._stopped = true;
                        this._cleanup();
                        return { done: true, value: undefined };
                    }
                    throw error;
                }
            }
            if (this._stopped || this._ticks >= this._maxTicks) {
                this._cleanup();
                return { done: true, value: undefined };
            }

            this._ticks += 1;
            if (this._ticks >= this._maxTicks) {
                this._cleanup();
            }
            return {
                done: false,
                value: Object.freeze({
                    index: this._ticks,
                    at: clockNow(this._clock),
                    scheduledAt: clockNow(this._clock),
                }),
            };
        }

        async return() {
            this.stop();
            return { done: true, value: undefined };
        }

        stop() {
            this._stopped = true;
            this._cleanup();
        }
    }

    class ScheduledJob {
        constructor(interval, handler, options = undefined) {
            if (typeof handler !== "function") {
                throw new TypeError("Time.every requires an async job handler.");
            }
            const missedRunPolicy = options?.missedRunPolicy ?? "skip";
            if (missedRunPolicy !== "skip") {
                throw new InvalidDeadlineError('Time.every only supports missedRunPolicy: "skip".');
            }
            this._intervalMs = parseIntervalMs(interval, "Time.every");
            this._handler = handler;
            this._clock = options?.clock;
            this._controller = new CancellationController({ signal: options?.signal });
            this._noOverlap = options?.noOverlap !== false;
            this._maxRuns = validateMaxTicks(options?.maxRuns, "Time.every");
            this._paused = false;
            this._stopped = false;
            this._running = false;
            this._runCount = 0;
            this._skippedRuns = 0;
            this._lastError = undefined;
            this._nextRunMs =
                clockMonotonicNowMs(this._clock) +
                (options?.immediate === true ? 0 : this._intervalMs);
            this._loopPromise = this._runLoop();
        }

        get running() {
            return this._running;
        }

        get stopped() {
            return this._stopped;
        }

        get skippedRuns() {
            return this._skippedRuns;
        }

        get lastError() {
            return this._lastError;
        }

        get nextRun() {
            if (this._stopped) {
                return null;
            }
            const remaining = Math.max(0, this._nextRunMs - clockMonotonicNowMs(this._clock));
            return new Date(clockNow(this._clock).getTime() + remaining);
        }

        pause() {
            if (this._stopped) {
                throw timerDisposedError();
            }
            this._paused = true;
        }

        resume() {
            if (this._stopped) {
                throw timerDisposedError();
            }
            this._paused = false;
        }

        stop(reason = "scheduled job stopped") {
            if (this._stopped) {
                return this._loopPromise;
            }
            this._finishStopped(reason, true);
            return this._loopPromise;
        }

        async _runLoop() {
            while (!this._stopped) {
                if (this._runCount >= this._maxRuns) {
                    this._finishStopped("scheduled job completed");
                    break;
                }
                const waitMs = Math.max(0, this._nextRunMs - clockMonotonicNowMs(this._clock));
                try {
                    await Time.delay(waitMs, {
                        clock: this._clock,
                        signal: this._controller.signal,
                    });
                } catch (error) {
                    if (error instanceof CancelledError || error instanceof TimerDisposedError) {
                        this._finishStopped(error);
                        break;
                    }
                    this._lastError = error;
                    this._finishStopped(error);
                    break;
                }

                if (this._stopped) {
                    break;
                }
                if (this._paused) {
                    this._nextRunMs += this._intervalMs;
                    continue;
                }
                if (this._running && this._noOverlap) {
                    this._skippedRuns += 1;
                    this._nextRunMs += this._intervalMs;
                    continue;
                }

                this._startRun();
                this._nextRunMs += this._intervalMs;
            }
        }

        _startRun() {
            this._running = true;
            this._runCount += 1;
            const context = Object.freeze({
                signal: this._controller.signal,
                scheduledAt: this.nextRun,
                startedAt: clockNow(this._clock),
                run: this._runCount,
                skippedRuns: this._skippedRuns,
            });
            Promise.resolve()
                .then(() => this._handler(context))
                .catch((error) => {
                    this._lastError = error;
                })
                .finally(() => {
                    this._running = false;
                });
        }

        _finishStopped(reason, cancel = false) {
            if (this._stopped) {
                this._controller.dispose();
                return;
            }
            this._stopped = true;
            if (cancel) {
                cancelAndDisposeController(this._controller, reason);
                return;
            }
            this._controller.dispose();
        }
    }

    class FakeClock {
        constructor(options = undefined) {
            const wallMs =
                options?.now instanceof Date
                    ? options.now.getTime()
                    : options?.now === undefined
                      ? 0
                      : Number(options.now);
            if (!Number.isFinite(wallMs)) {
                throw new InvalidDeadlineError(
                    "Time.fakeClock now must be a finite Date or Unix millisecond value.",
                );
            }
            this.kind = "fake";
            this._wallMs = wallMs;
            this._nowMs = 0;
            this._disposed = false;
            this._timers = [];
            this._timerSeq = 0;
        }

        now() {
            this._throwIfDisposed();
            return new Date(this._wallMs);
        }

        monotonicNowMs() {
            this._throwIfDisposed();
            return this._nowMs;
        }

        delay(ms, options = undefined) {
            const delayMs = validateDelayMs(ms, "FakeClock.delay");
            if (this._disposed) {
                return Promise.reject(timerDisposedError());
            }
            if (isCancellationSignal(options?.signal) && options.signal.aborted) {
                return Promise.reject(cancelledError(options.signal.reason));
            }
            const remaining = deadlineDelayMs(options?.deadline);
            if (remaining <= 0) {
                return Promise.reject(timeoutError(options?.deadline));
            }

            const actualDelay = Math.min(delayMs, remaining);
            if (actualDelay === 0) {
                return actualDelay < delayMs
                    ? Promise.reject(timeoutError(options?.deadline))
                    : Promise.resolve();
            }

            return new Promise((resolve, reject) => {
                const timer = {
                    dueMs: this._nowMs + actualDelay,
                    seq: this._timerSeq += 1,
                    reject,
                    resolve: () => {
                        cleanup();
                        if (actualDelay < delayMs) {
                            reject(timeoutError(options?.deadline));
                            return;
                        }
                        resolve();
                    },
                };
                const cleanup = subscribeCancellation(options?.signal, (reason) => {
                    this._removeTimer(timer);
                    reject(cancelledError(reason));
                });
                timer.cleanup = cleanup;
                this._timers.push(timer);
                this._flushDueTimers();
            });
        }

        set(dateOrUnixMs) {
            this._throwIfDisposed();
            const nextWallMs =
                dateOrUnixMs instanceof Date ? dateOrUnixMs.getTime() : Number(dateOrUnixMs);
            if (!Number.isFinite(nextWallMs)) {
                throw new InvalidDeadlineError(
                    "FakeClock.set requires a finite Date or Unix millisecond value.",
                );
            }
            const delta = nextWallMs - this._wallMs;
            if (delta > 0) {
                this.advanceBy(delta);
                return;
            }
            this._wallMs = nextWallMs;
        }

        advanceBy(ms) {
            this._throwIfDisposed();
            const delta = validateDelayMs(ms, "FakeClock.advanceBy");
            this._nowMs += delta;
            this._wallMs += delta;
            this._flushDueTimers();
        }

        dispose() {
            if (this._disposed) {
                return;
            }
            this._disposed = true;
            const timers = this._timers.splice(0);
            for (const timer of timers) {
                timer.cleanup?.();
                timer.reject(timerDisposedError());
            }
        }

        _throwIfDisposed() {
            if (this._disposed) {
                throw timerDisposedError();
            }
        }

        _removeTimer(timer) {
            const index = this._timers.indexOf(timer);
            if (index >= 0) {
                this._timers.splice(index, 1);
            }
        }

        _flushDueTimers() {
            if (this._disposed) {
                return;
            }
            const due = [];
            this._timers = this._timers.filter((timer) => {
                if (timer.dueMs <= this._nowMs) {
                    due.push(timer);
                    return false;
                }
                return true;
            });
            due.sort((left, right) => left.dueMs - right.dueMs || left.seq - right.seq);
            for (const timer of due) {
                timer.resolve();
            }
        }
    }

    const Time = Object.freeze({
        delay(ms, options = undefined) {
            return delayWithDeadline(ms, options);
        },

        timeout(operationOrPromise, options = undefined) {
            validateClockDeadlineOptions(options, "Time.timeout");
            const afterMs =
                options?.afterMs !== undefined
                    ? validateDelayMs(options.afterMs, "Time.timeout")
                    : Infinity;
            const deadlineMs = deadlineDelayMs(options?.deadline);
            const timeoutMs = Math.min(afterMs, deadlineMs);
            if (timeoutMs === Infinity) {
                throw new InvalidDeadlineError("Time.timeout requires afterMs or deadline.");
            }
            if (isCancellationSignal(options?.signal) && options.signal.aborted) {
                return Promise.reject(cancelledError(options.signal.reason));
            }
            if (timeoutMs <= 0) {
                return Promise.reject(timeoutError(options?.deadline));
            }

            if (typeof operationOrPromise === "function") {
                const controller = new CancellationController({ signal: options?.signal });
                const timeoutController = new CancellationController({ signal: options?.signal });
                const timeoutPromise = Time.delay(timeoutMs, {
                    clock: options?.clock,
                    signal: timeoutController.signal,
                }).then(() => {
                    const error = timeoutError(options?.deadline);
                    try {
                        controller.cancel(error);
                    } catch (_) {
                        // Timeout remains authoritative even when user abort listeners throw.
                    }
                    throw error;
                });
                const operationPromise = Promise.resolve().then(() =>
                    operationOrPromise(controller.signal),
                );
                const race = [operationPromise, timeoutPromise];
                if (isCancellationSignal(options?.signal)) {
                    race.push(raceCancellation(new Promise(() => {}), options.signal));
                }
                return Promise.race(race).finally(() => {
                    cancelAndDisposeController(timeoutController, "timeout race settled");
                    controller.dispose();
                });
            }

            const timeoutController = new CancellationController({ signal: options?.signal });
            return Promise.race([
                raceCancellation(Promise.resolve(operationOrPromise), options?.signal),
                Time.delay(timeoutMs, {
                    clock: options?.clock,
                    signal: timeoutController.signal,
                }).then(() => {
                    throw timeoutError(options?.deadline);
                }),
            ]).finally(() => {
                cancelAndDisposeController(timeoutController, "timeout race settled");
            });
        },

        interval(ms, options = undefined) {
            return new TimeInterval(ms, options);
        },

        every(interval, handler, options = undefined) {
            return new ScheduledJob(interval, handler, options);
        },

        yield(options = undefined) {
            return delayWithDeadline(0, options);
        },

        systemClock() {
            return Object.freeze({
                kind: "system",
                now: () => new Date(),
                monotonicNowMs,
                delay: systemDelay,
            });
        },

        fakeClock(options = undefined) {
            return new FakeClock(options);
        },
    });

    const CRYPTO_MAX_INLINE_BYTES = 1024 * 1024;
    const CRYPTO_MAX_PASSWORD_BYTES = 4096;
    const CRYPTO_PASSWORD_DEFAULT_OPS_LIMIT = 2;
    const CRYPTO_PASSWORD_MIN_OPS_LIMIT = 2;
    const CRYPTO_PASSWORD_MAX_OPS_LIMIT = 4;
    const CRYPTO_PASSWORD_DEFAULT_MEMORY_LIMIT_BYTES = 67108864;
    const CRYPTO_PASSWORD_MIN_MEMORY_LIMIT_BYTES = 67108864;
    const CRYPTO_PASSWORD_MAX_MEMORY_LIMIT_BYTES = 268435456;

    function sloppyCryptoUnavailable(operation) {
        throw new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature stdlib.crypto is inactive or unavailable

Feature:
  stdlib.crypto

Operation:
  ${operation}

Reason:
  The active Sloppy Plan did not enable the __sloppy.crypto V8 intrinsic namespace.`);
    }

    function sloppyNativeCrypto(operation) {
        const bridge = globalThis.__sloppy?.crypto ?? null;
        if (bridge === null) {
            sloppyCryptoUnavailable(operation);
        }
        return bridge;
    }

    function sloppyUtf8ToBytes(value) {
        const text = String(value);
        const bytes = [];
        for (let index = 0; index < text.length; index += 1) {
            let codePoint = text.charCodeAt(index);
            if (codePoint >= 0xd800 && codePoint <= 0xdbff && index + 1 < text.length) {
                const next = text.charCodeAt(index + 1);
                if (next >= 0xdc00 && next <= 0xdfff) {
                    codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (next - 0xdc00);
                    index += 1;
                }
            }
            if (codePoint <= 0x7f) {
                bytes.push(codePoint);
            } else if (codePoint <= 0x7ff) {
                bytes.push(0xc0 | (codePoint >> 6), 0x80 | (codePoint & 0x3f));
            } else if (codePoint <= 0xffff) {
                bytes.push(
                    0xe0 | (codePoint >> 12),
                    0x80 | ((codePoint >> 6) & 0x3f),
                    0x80 | (codePoint & 0x3f),
                );
            } else {
                bytes.push(
                    0xf0 | (codePoint >> 18),
                    0x80 | ((codePoint >> 12) & 0x3f),
                    0x80 | ((codePoint >> 6) & 0x3f),
                    0x80 | (codePoint & 0x3f),
                );
            }
        }
        return new Uint8Array(bytes);
    }

    function sloppyCloneBytes(bytes) {
        return bytes.slice();
    }

    class SloppySecretValue {
        constructor(bytes) {
            this._bytes = sloppyCloneBytes(bytes);
            this._disposed = false;
            Object.seal(this);
        }

        static fromUtf8(value) {
            if (typeof value !== "string") {
                throw new TypeError("Sloppy Secret.fromUtf8 value must be a string.");
            }
            return new SloppySecretValue(sloppyUtf8ToBytes(value));
        }

        static fromBytes(value) {
            if (!(value instanceof Uint8Array)) {
                throw new TypeError("Sloppy Secret.fromBytes value must be a Uint8Array.");
            }
            return new SloppySecretValue(value);
        }

        bytes() {
            if (this._disposed) {
                throw new Error("SLOPPY_E_CRYPTO_SECRET_DISPOSED: secret has been disposed");
            }
            return sloppyCloneBytes(this._bytes);
        }

        dispose() {
            if (!this._disposed) {
                this._bytes.fill(0);
                this._disposed = true;
            }
        }

        toString() {
            return "[Secret redacted]";
        }

        toJSON() {
            return "[Secret redacted]";
        }
    }

    function sloppyCryptoDataToBytes(value, operation) {
        if (value instanceof SloppySecretValue) {
            return value.bytes();
        }
        if (value instanceof Uint8Array) {
            return sloppyCloneBytes(value);
        }
        if (typeof value === "string") {
            return sloppyUtf8ToBytes(value);
        }
        throw new TypeError(`Sloppy crypto ${operation} data must be a string, Uint8Array, or Secret.`);
    }

    function sloppyCryptoBoundedBytes(bytes, operation) {
        if (bytes.byteLength > CRYPTO_MAX_INLINE_BYTES) {
            throw new TypeError(`Sloppy crypto ${operation} input is too large for inline hashing.`);
        }
        return bytes;
    }

    function sloppyPasswordBytes(value, operation) {
        const bytes = sloppyCryptoDataToBytes(value, operation);
        if (bytes.byteLength > CRYPTO_MAX_PASSWORD_BYTES) {
            throw new TypeError(`Sloppy crypto ${operation} password input is too large.`);
        }
        return bytes;
    }

    function sloppyPasswordOptions(options = undefined) {
        if (options === undefined) {
            return {
                opsLimit: CRYPTO_PASSWORD_DEFAULT_OPS_LIMIT,
                memoryLimitBytes: CRYPTO_PASSWORD_DEFAULT_MEMORY_LIMIT_BYTES,
            };
        }
        if (options === null || typeof options !== "object") {
            throw new TypeError("Sloppy Password options must be an object when provided.");
        }

        const opsLimit = options.opsLimit ?? CRYPTO_PASSWORD_DEFAULT_OPS_LIMIT;
        const memoryLimitBytes =
            options.memoryLimitBytes ?? CRYPTO_PASSWORD_DEFAULT_MEMORY_LIMIT_BYTES;
        if (
            !Number.isInteger(opsLimit) ||
            opsLimit < CRYPTO_PASSWORD_MIN_OPS_LIMIT ||
            opsLimit > CRYPTO_PASSWORD_MAX_OPS_LIMIT ||
            !Number.isInteger(memoryLimitBytes) ||
            memoryLimitBytes < CRYPTO_PASSWORD_MIN_MEMORY_LIMIT_BYTES ||
            memoryLimitBytes > CRYPTO_PASSWORD_MAX_MEMORY_LIMIT_BYTES
        ) {
            throw new TypeError("Sloppy Password options are outside the documented safe bounds.");
        }
        return { opsLimit, memoryLimitBytes };
    }

    function sloppyEncodedPasswordHash(value, operation) {
        if (typeof value !== "string") {
            throw new TypeError(`Sloppy crypto ${operation} encoded hash must be a string.`);
        }
        return value;
    }

    function sloppyBytesToHex(bytes) {
        let output = "";
        for (const byte of bytes) {
            output += byte.toString(16).padStart(2, "0");
        }
        return output;
    }

    function sloppyBytesToBase64(bytes) {
        const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        let output = "";
        for (let index = 0; index < bytes.byteLength; index += 3) {
            const a = bytes[index];
            const b = index + 1 < bytes.byteLength ? bytes[index + 1] : 0;
            const c = index + 2 < bytes.byteLength ? bytes[index + 2] : 0;
            const triple = (a << 16) | (b << 8) | c;
            output += alphabet[(triple >> 18) & 0x3f];
            output += alphabet[(triple >> 12) & 0x3f];
            output += index + 1 < bytes.byteLength ? alphabet[(triple >> 6) & 0x3f] : "=";
            output += index + 2 < bytes.byteLength ? alphabet[triple & 0x3f] : "=";
        }
        return output;
    }

    function sloppyDigest(algorithm, value, operation) {
        const bytes = sloppyCryptoBoundedBytes(sloppyCryptoDataToBytes(value, operation), operation);
        return sloppyNativeCrypto(operation).hash(algorithm, bytes);
    }

    function sloppyHmac(algorithm, secret, value, operation) {
        const key = sloppyCryptoBoundedBytes(sloppyCryptoDataToBytes(secret, operation), operation);
        const bytes = sloppyCryptoBoundedBytes(sloppyCryptoDataToBytes(value, operation), operation);
        return sloppyNativeCrypto(operation).hmac(algorithm, key, bytes);
    }

    class SloppyIncrementalHasher {
        constructor(algorithm) {
            if (!["sha256", "sha384", "sha512"].includes(algorithm)) {
                throw new TypeError("Sloppy Hash.create algorithm must be sha256, sha384, or sha512.");
            }
            this._algorithm = algorithm;
            this._chunks = [];
            this._digested = false;
        }

        update(value) {
            if (this._digested) {
                throw new Error("SLOPPY_E_CRYPTO_UNSUPPORTED_ALGORITHM: hasher already digested");
            }
            this._chunks.push(sloppyCryptoDataToBytes(value, "Hash.update"));
            return this;
        }

        async digest(encoding = undefined) {
            if (this._digested) {
                throw new Error("SLOPPY_E_CRYPTO_UNSUPPORTED_ALGORITHM: hasher already digested");
            }
            this._digested = true;
            const length = this._chunks.reduce((sum, chunk) => sum + chunk.byteLength, 0);
            const joined = new Uint8Array(length);
            let offset = 0;
            for (const chunk of this._chunks) {
                joined.set(chunk, offset);
                offset += chunk.byteLength;
            }
            const bytes = sloppyDigest(this._algorithm, joined, "Hash.digest");
            if (encoding === "hex") {
                return sloppyBytesToHex(bytes);
            }
            if (encoding === "base64") {
                return sloppyBytesToBase64(bytes);
            }
            if (encoding !== undefined && encoding !== "bytes") {
                throw new TypeError("Sloppy Hash.digest encoding must be bytes, hex, or base64.");
            }
            return bytes;
        }
    }

    const Random = Object.freeze({
        bytes(length) {
            return sloppyNativeCrypto("Random.bytes").randomBytes(length);
        },
        uuid() {
            return sloppyNativeCrypto("Random.uuid").randomUuid();
        },
        token(length) {
            return sloppyNativeCrypto("Random.token").randomToken(length);
        },
        hex(length) {
            return sloppyNativeCrypto("Random.hex").randomHex(length);
        },
        numericCode(length) {
            return sloppyNativeCrypto("Random.numericCode").randomNumericCode(length);
        },
    });

    const Hash = Object.freeze({
        sha256(value) {
            return Promise.resolve(sloppyDigest("sha256", value, "Hash.sha256"));
        },
        sha384(value) {
            return Promise.resolve(sloppyDigest("sha384", value, "Hash.sha384"));
        },
        sha512(value) {
            return Promise.resolve(sloppyDigest("sha512", value, "Hash.sha512"));
        },
        async sha256Hex(value) {
            return sloppyBytesToHex(sloppyDigest("sha256", value, "Hash.sha256Hex"));
        },
        async sha256Base64(value) {
            return sloppyBytesToBase64(sloppyDigest("sha256", value, "Hash.sha256Base64"));
        },
        create(algorithm) {
            return new SloppyIncrementalHasher(algorithm);
        },
    });

    const ConstantTime = Object.freeze({
        equals(left, right) {
            return sloppyNativeCrypto("ConstantTime.equals").constantTimeEquals(
                sloppyCryptoDataToBytes(left, "ConstantTime.equals"),
                sloppyCryptoDataToBytes(right, "ConstantTime.equals"),
            );
        },
    });

    const Hmac = Object.freeze({
        sha256(secret, value) {
            return Promise.resolve(sloppyHmac("sha256", secret, value, "Hmac.sha256"));
        },
        async verifySha256(secret, value, signature) {
            const actual = sloppyHmac("sha256", secret, value, "Hmac.verifySha256");
            const expected = sloppyCryptoDataToBytes(signature, "Hmac.verifySha256");
            return ConstantTime.equals(actual, expected);
        },
    });

    const Password = Object.freeze({
        hash(password, options = undefined) {
            const normalized = sloppyPasswordOptions(options);
            return sloppyNativeCrypto("Password.hash").passwordHash(
                sloppyPasswordBytes(password, "Password.hash"),
                normalized.opsLimit,
                normalized.memoryLimitBytes,
            );
        },
        verify(password, encodedHash) {
            return sloppyNativeCrypto("Password.verify").passwordVerify(
                sloppyPasswordBytes(password, "Password.verify"),
                sloppyEncodedPasswordHash(encodedHash, "Password.verify"),
            );
        },
        needsRehash(encodedHash, options = undefined) {
            const normalized = sloppyPasswordOptions(options);
            return sloppyNativeCrypto("Password.needsRehash").passwordNeedsRehash(
                sloppyEncodedPasswordHash(encodedHash, "Password.needsRehash"),
                normalized.opsLimit,
                normalized.memoryLimitBytes,
            );
        },
    });

    const Secret = Object.freeze({
        fromUtf8: SloppySecretValue.fromUtf8,
        fromBytes: SloppySecretValue.fromBytes,
    });

    const NonCryptoHash = Object.freeze({
        xxHash64(data) {
            return nativeCrypto("NonCryptoHash.xxHash64").nonCryptoXxHash64(
                dataToBytes(data, "NonCryptoHash.xxHash64"),
            );
        },
    });

    globalThis.__sloppy_runtime = Object.freeze({
        Results,
        Random,
        Hash,
        Hmac,
        Password,
        ConstantTime,
        Secret,
        NonCryptoHash,
        data,
        Time,
        Deadline,
        CancellationController,
        TimeoutError,
        CancelledError,
        InvalidDeadlineError,
        TimerDisposedError,
        File,
        Directory,
        Path,
        FileHandle,
        FileWatcher,
    });
})();
