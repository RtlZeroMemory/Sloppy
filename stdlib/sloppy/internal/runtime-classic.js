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

    const File = Object.freeze({
        readText(path) {
            return requireFsBridge("readText").readText(validateFsPath(path, "readText"));
        },
        readBytes(path) {
            return requireFsBridge("readBytes").readBytes(validateFsPath(path, "readBytes"));
        },
        async readJson(path) {
            return JSON.parse(await File.readText(path));
        },
        writeText(path, text, options) {
            if (typeof text !== "string") {
                throw new TypeError("Sloppy File.writeText text must be a string.");
            }
            if (fsShouldAtomic(options)) {
                return requireFsBridge("atomicWriteText").atomicWriteText(validateFsPath(path, "writeText"), text);
            }
            return requireFsBridge("writeText").writeText(validateFsPath(path, "writeText"), text);
        },
        writeBytes(path, bytes, options) {
            const checked = validateFsBytes(bytes, "writeBytes");
            if (fsShouldAtomic(options)) {
                return requireFsBridge("atomicWriteBytes").atomicWriteBytes(validateFsPath(path, "writeBytes"), checked);
            }
            return requireFsBridge("writeBytes").writeBytes(
                validateFsPath(path, "writeBytes"),
                checked,
            );
        },
        writeJson(path, value, options) {
            return File.writeText(path, stringifyFsJson(value, options), options);
        },
        appendText(path, text) {
            if (typeof text !== "string") {
                throw new TypeError("Sloppy File.appendText text must be a string.");
            }
            return requireFsBridge("appendText").appendText(validateFsPath(path, "appendText"), text);
        },
        appendBytes(path, bytes) {
            return requireFsBridge("appendBytes").appendBytes(
                validateFsPath(path, "appendBytes"),
                validateFsBytes(bytes, "appendBytes"),
            );
        },
        exists(path) {
            return requireFsBridge("exists").exists(validateFsPath(path, "exists"));
        },
        stat(path) {
            return requireFsBridge("stat").stat(validateFsPath(path, "stat"));
        },
        copy(fromPath, toPath, options) {
            return requireFsBridge("copy").copy(
                validateFsPath(fromPath, "copy"),
                validateFsPath(toPath, "copy"),
                validateFsOverwrite(options),
            );
        },
        move(fromPath, toPath, options) {
            return requireFsBridge("move").move(
                validateFsPath(fromPath, "move"),
                validateFsPath(toPath, "move"),
                validateFsOverwrite(options),
            );
        },
        delete(path) {
            return requireFsBridge("delete").delete(validateFsPath(path, "delete"));
        },
        async open(path, options) {
            const checked = validateFsOpenOptions(options);
            return new FileHandle(await requireFsBridge("openHandle").openHandle(
                validateFsPath(path, "open"),
                checked.access,
                checked.create,
            ));
        },
        async watch(path, options) {
            const checked = validateFsWatchOptions(options, false);
            return new FileWatcher(await requireFsBridge("watch").watch(
                validateFsPath(path, "watch"),
                false,
                checked,
            ));
        },
        createSymlink(targetPath, linkPath, options) {
            const directory = options?.directory ?? false;
            if (typeof directory !== "boolean") {
                throw new TypeError("Sloppy File.createSymlink directory option must be boolean.");
            }
            return requireFsBridge("symlink").symlink(
                validateFsPath(targetPath, "createSymlink"),
                validateFsPath(linkPath, "createSymlink"),
                directory,
            );
        },
        readLink(path) {
            return requireFsBridge("readLink").readLink(validateFsPath(path, "readLink"));
        },
        createTemp(directory, options) {
            const prefix = options?.prefix ?? "sloppy-";
            if (typeof prefix !== "string" || prefix.length === 0) {
                throw new TypeError("Sloppy File.createTemp prefix must be a non-empty string.");
            }
            return requireFsBridge("tempFile").tempFile(validateFsPath(directory, "createTemp"), prefix);
        },
    });

    async function isFsSymlink(path) {
        try {
            await File.readLink(path);
            return true;
        }
        catch {
            return false;
        }
    }

    const Directory = Object.freeze({
        create(path, options) {
            const checked = validateFsRecursive(options);
            return requireFsBridge("directoryCreate").directoryCreate(validateFsPath(path, "create"), checked.recursive);
        },
        list(path) {
            return requireFsBridge("directoryList").directoryList(validateFsPath(path, "list"));
        },
        async *walk(path, options) {
            const followSymlinks = options?.followSymlinks ?? false;
            if (typeof followSymlinks !== "boolean") {
                throw new TypeError("Sloppy Directory.walk followSymlinks option must be boolean.");
            }
            for (const entry of await Directory.list(path)) {
                yield entry;
                if (entry.kind === "directory") {
                    const child = `${path.replace(/[\\/]$/, "")}/${entry.name}`;
                    if (!followSymlinks && await isFsSymlink(child)) {
                        continue;
                    }
                    for await (const nested of Directory.walk(child, { followSymlinks })) {
                        yield { ...nested, name: `${entry.name}/${nested.name}` };
                    }
                }
            }
        },
        delete(path, options) {
            const checked = validateFsRecursive(options);
            return requireFsBridge("directoryDelete").directoryDelete(validateFsPath(path, "delete"), checked.recursive);
        },
        async exists(path) {
            const stat = await File.stat(path);
            return stat.exists && stat.kind === "directory";
        },
        createTemp(directory, options) {
            const prefix = options?.prefix ?? "sloppy-";
            if (typeof prefix !== "string" || prefix.length === 0) {
                throw new TypeError("Sloppy Directory.createTemp prefix must be a non-empty string.");
            }
            return requireFsBridge("tempDirectory").tempDirectory(validateFsPath(directory, "createTemp"), prefix);
        },
        async watch(path, options) {
            const checked = validateFsWatchOptions(options, true);
            return new FileWatcher(await requireFsBridge("watch").watch(
                validateFsPath(path, "watch"),
                true,
                checked,
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
        readBytes(maxBytes = 64 * 1024) {
            if (!Number.isInteger(maxBytes) || maxBytes <= 0 || maxBytes > 1024 * 1024) {
                throw new TypeError("Sloppy FileHandle.readBytes maxBytes must be 1..1048576.");
            }
            return requireFsBridge("handleRead").handleRead(this._id, maxBytes);
        }
        async readText(maxBytes) {
            return new TextDecoder().decode(await this.readBytes(maxBytes));
        }
        writeBytes(bytes) {
            return requireFsBridge("handleWriteBytes").handleWriteBytes(this._id, validateFsBytes(bytes, "writeBytes"));
        }
        writeText(text) {
            if (typeof text !== "string") {
                throw new TypeError("Sloppy FileHandle.writeText text must be a string.");
            }
            return requireFsBridge("handleWriteText").handleWriteText(this._id, text);
        }
        seek(offset, origin = "start") {
            if (!Number.isInteger(offset) || !["start", "current", "end"].includes(origin)) {
                throw new TypeError("Sloppy FileHandle.seek requires an integer offset and valid origin.");
            }
            return requireFsBridge("handleSeek").handleSeek(this._id, offset, origin);
        }
        truncate(size) {
            if (!Number.isInteger(size) || size < 0) {
                throw new TypeError("Sloppy FileHandle.truncate size must be a non-negative integer.");
            }
            return requireFsBridge("handleTruncate").handleTruncate(this._id, size);
        }
        flush() {
            return requireFsBridge("handleFlush").handleFlush(this._id);
        }
        sync() {
            return requireFsBridge("handleSync").handleSync(this._id);
        }
        close() {
            return requireFsBridge("handleClose").handleClose(this._id);
        }
        async *readChunks(options) {
            const chunkSize = options?.chunkSize ?? 64 * 1024;
            for (;;) {
                const chunk = await this.readBytes(chunkSize);
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
            return requireFsBridge("watchNext").watchNext(this._id);
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
        constructor(name, message, options) {
            super(message, options);
            this.name = name;
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

    function unavailableTimeFeature(operation) {
        throw new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature stdlib.time is inactive or unavailable

Feature:
  stdlib.time

Operation:
  ${operation}

Reason:
  CORE-TIME-01.A/B defines the sloppy/time contract. Native timer scheduling lands in the CORE-TIME-01.C/D/G implementation slice.`);
    }

    const Deadline = Object.freeze({
        after() {
            return unavailableTimeFeature("Deadline.after");
        },

        at() {
            return unavailableTimeFeature("Deadline.at");
        },

        never() {
            return unavailableTimeFeature("Deadline.never");
        },
    });

    class CancellationController {
        constructor() {
            unavailableTimeFeature("CancellationController");
        }
    }

    const Time = Object.freeze({
        delay() {
            return unavailableTimeFeature("Time.delay");
        },

        timeout() {
            return unavailableTimeFeature("Time.timeout");
        },

        interval() {
            return unavailableTimeFeature("Time.interval");
        },

        every() {
            return unavailableTimeFeature("Time.every");
        },

        yield() {
            return unavailableTimeFeature("Time.yield");
        },

        systemClock() {
            return unavailableTimeFeature("Time.systemClock");
        },

        fakeClock() {
            return unavailableTimeFeature("Time.fakeClock");
        },
    });

    globalThis.__sloppy_runtime = Object.freeze({
        Results,
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
