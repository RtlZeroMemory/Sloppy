(() => {
    const TEXT_CONTENT_TYPE = "text/plain; charset=utf-8";
    const JSON_CONTENT_TYPE = "application/json; charset=utf-8";
    const HTML_CONTENT_TYPE = "text/html; charset=utf-8";
    const BYTES_CONTENT_TYPE = "application/octet-stream";
    const PROBLEM_CONTENT_TYPE = "application/problem+json; charset=utf-8";
    const STREAM_CONTENT_TYPE = "application/octet-stream";
    const STREAM_MAX_CHUNK_BYTES = 65536;
    const STREAM_MAX_TOTAL_BYTES = 131072;
    const FAST_RESULT_KIND = "__sloppyFastResult";
    const FAST_JSON_TEXT = "__sloppyJsonText";
    const FAST_TEXT_OK = 1;
    const FAST_NO_CONTENT = 2;
    const FAST_JSON = 3;
    const FAST_CREATED = 4;
    const FAST_JSON_MAX_LENGTH = 256;
    const POSTGRES_MAX_POOL_CONNECTIONS = 256;
    const SQLSERVER_MAX_POOL_CONNECTIONS = 256;
    const MIGRATIONS_TABLE = "_sloppy_migrations";
    const MIGRATION_HASH_PREFIX = "fnv1a32:";
    const MIGRATION_PROVIDER_KINDS = Object.freeze({
        sqlite: true,
        postgres: true,
        sqlserver: true,
    });

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

    const DEFAULT_JSON_OPTIONS = Object.freeze({
        casing: "preserve",
        includeNulls: true,
        dateFormat: "iso8601",
        bigint: "string",
        bytes: "base64",
    });

    function normalizeJsonOptions(options = undefined) {
        if (options === undefined) {
            return DEFAULT_JSON_OPTIONS;
        }
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy JSON options must be a plain object.");
        }
        const normalized = {
            casing: options.casing !== undefined
                ? options.casing
                : DEFAULT_JSON_OPTIONS.casing,
            includeNulls: options.includeNulls !== undefined
                ? options.includeNulls
                : DEFAULT_JSON_OPTIONS.includeNulls,
            dateFormat: options.dateFormat !== undefined
                ? options.dateFormat
                : DEFAULT_JSON_OPTIONS.dateFormat,
            bigint: options.bigint !== undefined
                ? options.bigint
                : DEFAULT_JSON_OPTIONS.bigint,
            bytes: options.bytes !== undefined
                ? options.bytes
                : DEFAULT_JSON_OPTIONS.bytes,
        };
        if (normalized.casing !== "preserve" && normalized.casing !== "camelCase") {
            throw new TypeError("Sloppy JSON casing must be preserve or camelCase.");
        }
        if (typeof normalized.includeNulls !== "boolean") {
            throw new TypeError("Sloppy JSON includeNulls must be a boolean.");
        }
        if (normalized.dateFormat !== "iso8601") {
            throw new TypeError("Sloppy JSON dateFormat currently supports iso8601.");
        }
        if (normalized.bigint !== "string" && normalized.bigint !== "error") {
            throw new TypeError("Sloppy JSON bigint must be string or error.");
        }
        if (normalized.bytes !== "base64" && normalized.bytes !== "array") {
            throw new TypeError("Sloppy JSON bytes must be base64 or array.");
        }
        return Object.freeze(normalized);
    }

    function jsonKey(key, options) {
        return options.casing === "camelCase"
            ? key.replace(/[-_]+([A-Za-z0-9])/gu, (_, ch) => ch.toUpperCase())
            : key;
    }

    function base64Encode(bytes) {
        const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        let output = "";
        for (let index = 0; index < bytes.byteLength; index += 3) {
            const a = bytes[index];
            const b = index + 1 < bytes.byteLength ? bytes[index + 1] : 0;
            const c = index + 2 < bytes.byteLength ? bytes[index + 2] : 0;
            const combined = (a << 16) | (b << 8) | c;
            output += alphabet[(combined >> 18) & 63];
            output += alphabet[(combined >> 12) & 63];
            output += index + 1 < bytes.byteLength ? alphabet[(combined >> 6) & 63] : "=";
            output += index + 2 < bytes.byteLength ? alphabet[combined & 63] : "=";
        }
        return output;
    }

    function bytesView(value) {
        if (value instanceof Uint8Array) {
            return new Uint8Array(value);
        }
        if (value instanceof ArrayBuffer) {
            return new Uint8Array(value.slice(0));
        }
        if (ArrayBuffer.isView(value)) {
            const storage = value["buf" + "fer"];
            return new Uint8Array(storage.slice(value.byteOffset, value.byteOffset + value.byteLength));
        }
        return undefined;
    }

    function defineJsonProperty(output, key, value) {
        Object.defineProperty(output, key, {
            value,
            enumerable: true,
            configurable: true,
            writable: true,
        });
    }

    function normalizeJsonValue(value, options, seen, path) {
        if (value === undefined) {
            return undefined;
        }
        if (typeof value === "function" || typeof value === "symbol") {
            throw new TypeError(`Sloppy JSON cannot serialize ${typeof value} at ${path}.`);
        }
        if (value === null || typeof value === "string" || typeof value === "boolean") {
            return value;
        }
        if (typeof value === "number") {
            if (!Number.isFinite(value)) {
                throw new TypeError(`Sloppy JSON numbers must be finite at ${path}.`);
            }
            return value;
        }
        if (typeof value === "bigint") {
            if (options.bigint === "error") {
                throw new TypeError(`Sloppy JSON cannot serialize BigInt at ${path}.`);
            }
            return value.toString();
        }
        if (value instanceof Date) {
            if (!Number.isFinite(value.getTime())) {
                throw new TypeError(`Sloppy JSON cannot serialize invalid Date at ${path}.`);
            }
            return value.toISOString();
        }
        const bytes = bytesView(value);
        if (bytes !== undefined) {
            return options.bytes === "array" ? Object.freeze(Array.from(bytes)) : base64Encode(bytes);
        }
        if (value !== null && typeof value === "object") {
            if (seen.has(value)) {
                throw new TypeError(`Sloppy JSON cannot serialize circular reference at ${path}.`);
            }
            seen.add(value);
            try {
                if (Array.isArray(value)) {
                    return Object.freeze(value.map((item, index) => {
                        const nested = normalizeJsonValue(item, options, seen, `${path}[${index}]`);
                        return nested === undefined ? null : nested;
                    }));
                }
                const output = {};
                if (value instanceof Error) {
                    defineJsonProperty(
                        output,
                        "name",
                        typeof value.name === "string" && value.name.length > 0 ? value.name : "Error",
                    );
                    defineJsonProperty(output, "message", String(value.message ?? ""));
                }
                for (const [key, nested] of Object.entries(value)) {
                    if (value instanceof Error && (key === "name" || key === "message" || key === "stack")) {
                        continue;
                    }
                    const normalized = normalizeJsonValue(nested, options, seen, `${path}.${key}`);
                    if (normalized === undefined || (normalized === null && !options.includeNulls)) {
                        continue;
                    }
                    defineJsonProperty(output, jsonKey(key, options), normalized);
                }
                return Object.freeze(output);
            } finally {
                seen.delete(value);
            }
        }
        throw new TypeError(`Sloppy JSON cannot serialize ${typeof value} at ${path}.`);
    }

    function normalizeJsonBody(value, options = undefined) {
        if (value === undefined) {
            return null;
        }
        return normalizeJsonValue(value, normalizeJsonOptions(options), new Set(), "$");
    }

    function normalizeJsonDescriptorBody(value, options = undefined) {
        return value === undefined ? undefined : normalizeJsonBody(value, options);
    }

    function serializeJson(value, options = undefined) {
        return JSON.stringify(normalizeJsonBody(value, options));
    }

    const DB_VALUE_MARKER = Symbol("sloppyDbValue");
    const DB_BRIDGE_VALUE_MARKER = "__sloppyDbValue";
    const DB_RESULT_MODES = Object.freeze({
        object: true,
        raw: true,
    });
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

    function isKnownDbValueKind(kind) {
        return typeof kind === "string" && Object.prototype.hasOwnProperty.call(DB_VALUE_KINDS, kind);
    }

    function createDbValue(kind, value) {
        if (!isKnownDbValueKind(kind)) {
            throw new TypeError("Sloppy DB value wrapper kind is not supported.");
        }
        const storedValue = kind === "bytes" ? new Uint8Array(value) : value;
        const wrapper = {
            kind,
            toString() {
                return kind === "json" ? JSON.stringify(storedValue) : String(storedValue);
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
        return value !== null && typeof value === "object" && Object.isFrozen(value) &&
            isKnownDbValueKind(value.kind) &&
            (value[DB_VALUE_MARKER] === true ||
                (value[DB_BRIDGE_VALUE_MARKER] === true &&
                    Object.prototype.toString.call(value) === "[object String]"));
    }

    function requireDbString(value) {
        if (typeof value !== "string" || value.length === 0) {
            throw new TypeError("Sloppy DB value requires a non-empty string.");
        }
        return value;
    }

    const DB_TEXT_VALUE_SPECS = {
        decimal: ["decimal", /^[+-]?(?:\d+|\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?$/, "finite decimal string"],
        uuid: ["uuid", /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/, "canonical UUID string"],
        date: ["date", /^\d{4}-\d{2}-\d{2}$/, "YYYY-MM-DD"],
        time: ["time", /^\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?$/, "HH:MM:SS"],
        timestamp: ["localDateTime", /^\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?$/, "local date-time"],
        instant: ["instant", /^\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?Z$/, "UTC timestamp"],
        offsetDateTime: ["offsetDateTime", /^\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?[+-]\d{2}:\d{2}$/, "UTC offset"],
    };

    function dbTextValue(method, value) {
        const spec = DB_TEXT_VALUE_SPECS[method];
        const text = (method === "uuid" ? requireDbString(value).toLowerCase() : requireDbString(value));
        if (!spec[1].test(text)) {
            throw new TypeError(`Sloppy DB ${method} requires ${spec[2]}.`);
        }
        return createDbValue(spec[0], text);
    }

    function dbJson(value) {
        if (value === undefined || typeof value === "function" || typeof value === "symbol") {
            throw new TypeError("Sloppy DB json requires JSON-serializable value.");
        }
        try {
            if (JSON.stringify(value) === undefined) {
                throw new TypeError("not serializable");
            }
        } catch {
            throw new TypeError("Sloppy DB json requires JSON-serializable value.");
        }
        return createDbValue("json", value);
    }

    function dbRawJson(value) {
        const text = requireDbString(value);
        try {
            JSON.parse(text);
        } catch {
            throw new TypeError("Sloppy DB rawJson requires valid JSON text.");
        }
        return createDbValue("rawJson", text);
    }

    function dbBytes(value) {
        if (value instanceof Uint8Array) {
            return createDbValue("bytes", value);
        }
        if (value instanceof ArrayBuffer) {
            return createDbValue("bytes", new Uint8Array(value));
        }
        throw new TypeError("Sloppy DB bytes requires Uint8Array or ArrayBuffer.");
    }

    const values = Object.freeze({
        decimal: (value) => dbTextValue("decimal", value),
        uuid: (value) => dbTextValue("uuid", value),
        date: (value) => dbTextValue("date", value),
        time: (value) => dbTextValue("time", value),
        timestamp: (value) => dbTextValue("timestamp", value),
        instant: (value) => dbTextValue("instant", value),
        offsetDateTime: (value) => dbTextValue("offsetDateTime", value),
        json: dbJson,
        rawJson: dbRawJson,
        bytes: dbBytes,
        isDbValue,
    });

    const SQLITE_TEXT_DB_VALUE_KINDS = new Set([
        "decimal",
        "uuid",
        "date",
        "time",
        "localDateTime",
        "instant",
        "offsetDateTime",
    ]);

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

    function copySetCookies(options) {
        const setCookies = options?.setCookies;
        if (setCookies === undefined) {
            return undefined;
        }
        if (!Array.isArray(setCookies)) {
            throw new TypeError("Sloppy Results setCookies must be an array when provided.");
        }
        return Object.freeze(setCookies.map((cookie) => {
            if (typeof cookie !== "string") {
                throw new TypeError("Sloppy Results setCookies entries must be strings.");
            }
            assertCookieHeaderValue(cookie, "setCookies entry");
            return cookie;
        }));
    }

    function copyBytes(value) {
        if (value instanceof ArrayBuffer) {
            return new Uint8Array(value.slice(0));
        }

        if (ArrayBuffer.isView(value)) {
            const storage = value["buf" + "fer"];
            return new Uint8Array(storage.slice(value.byteOffset, value.byteOffset + value.byteLength));
        }

        throw new TypeError("Sloppy Results.bytes body must be binary data or a typed array view.");
    }

    function encodeUtf8Text(value) {
        return encodeUtf8(String(value));
    }

    function assertCookieHeaderValue(value, label) {
        if (typeof value !== "string" || /[\x00-\x1F\x7F]/.test(value)) {
            throw new TypeError(`Sloppy Results ${label} must be a safe HTTP header value.`);
        }
    }

    function assertCookieAttributeValue(value, label) {
        assertCookieHeaderValue(value, label);
        if (value.includes(";")) {
            throw new TypeError(`Sloppy Results ${label} must not contain ';'.`);
        }
    }

    function normalizeSameSite(value) {
        if (value === undefined) {
            return undefined;
        }
        const lowered = String(value).toLowerCase();
        if (lowered === "lax") {
            return "Lax";
        }
        if (lowered === "strict") {
            return "Strict";
        }
        if (lowered === "none") {
            return "None";
        }
        throw new TypeError("Sloppy Results cookie sameSite must be lax, strict, or none.");
    }

    function buildSetCookie(name, value, options = undefined) {
        if (typeof name !== "string" || name.length === 0 || /[^!#$%&'*+\-.^_`|~0-9A-Za-z]/u.test(name)) {
            throw new TypeError("Sloppy Results cookie name must be a safe HTTP token.");
        }
        if (options !== undefined && !isPlainObject(options)) {
            throw new TypeError("Sloppy Results cookie options must be a plain object.");
        }
        let encodedValue;
        try {
            encodedValue = encodeURIComponent(String(value));
        } catch {
            throw new TypeError("Sloppy Results cookie value must be safe.");
        }
        if (/[\x00-\x20\x7F;,]/u.test(encodedValue)) {
            throw new TypeError("Sloppy Results cookie value must be safe.");
        }
        const parts = [`${name}=${encodedValue}`];
        if (options?.path !== undefined) {
            assertCookieAttributeValue(options.path, "cookie path");
            parts.push(`Path=${options.path}`);
        }
        if (options?.domain !== undefined) {
            assertCookieAttributeValue(options.domain, "cookie domain");
            parts.push(`Domain=${options.domain}`);
        }
        const maxAgeSeconds = options?.maxAgeSeconds ?? options?.maxAge;
        if (maxAgeSeconds !== undefined) {
            if (!Number.isInteger(maxAgeSeconds)) {
                throw new TypeError("Sloppy Results cookie maxAgeSeconds must be an integer.");
            }
            parts.push(`Max-Age=${maxAgeSeconds}`);
        }
        if (options?.expires !== undefined) {
            const expires = options.expires instanceof Date ? options.expires.toUTCString() : String(options.expires);
            assertCookieAttributeValue(expires, "cookie expires");
            parts.push(`Expires=${expires}`);
        }
        const sameSite = normalizeSameSite(options?.sameSite);
        if (sameSite !== undefined) {
            parts.push(`SameSite=${sameSite}`);
        }
        if (options?.httpOnly === true) {
            parts.push("HttpOnly");
        }
        if (options?.secure === true) {
            parts.push("Secure");
        }
        return parts.join("; ");
    }

    function withCookie(descriptor, name, value, options) {
        const extra = {};
        if (descriptor.location !== undefined) {
            extra.location = descriptor.location;
        }
        if (descriptor.chunks !== undefined) {
            extra.chunks = Object.freeze([...descriptor.chunks]);
        }
        return createResult(
            descriptor.kind,
            descriptor.body,
            descriptor.contentType,
            {
                status: descriptor.status,
                headers: descriptor.headers,
                setCookies: Object.freeze([...(descriptor.setCookies ?? []), buildSetCookie(name, value, options)]),
            },
            descriptor.status,
            Object.keys(extra).length === 0 ? undefined : extra,
        );
    }

    function resolveContentType(options, defaultContentType) {
        const contentType = options?.contentType ?? defaultContentType;

        if (typeof contentType !== "string" || contentType.length === 0) {
            throw new TypeError("Sloppy Results contentType must be a non-empty string.");
        }

        if (/[\x00-\x1F\x7F]/.test(contentType)) {
            throw new TypeError("Sloppy Results contentType must not contain control characters.");
        }

        return contentType;
    }

    function maybeFastJsonText(body) {
        if (body !== null && typeof body === "object") {
            return undefined;
        }

        let jsonText;

        try {
            jsonText = serializeJson(body);
        } catch {
            return undefined;
        }

        return typeof jsonText === "string" && jsonText.length <= FAST_JSON_MAX_LENGTH
            ? jsonText
            : undefined;
    }

    function createResult(kind, body, contentType, options, defaultStatus, extra, fast) {
        const setCookies = copySetCookies(options);
        const descriptor = {
            __sloppyResult: true,
            kind,
            status: resolveStatus(options, defaultStatus),
            contentType,
            headers: copyHeaders(options),
            ...extra,
        };

        if (setCookies !== undefined) {
            descriptor.setCookies = setCookies;
        }

        if (body !== undefined) {
            descriptor.body = body;
        }

        if (fast !== undefined) {
            Object.defineProperty(descriptor, FAST_RESULT_KIND, {
                value: fast.kind,
            });
            if (fast.jsonText !== undefined) {
                Object.defineProperty(descriptor, FAST_JSON_TEXT, {
                    value: fast.jsonText,
                });
            }
        }

        Object.defineProperty(descriptor, "cookie", {
            value(name, value, options) {
                return withCookie(descriptor, name, value, options);
            },
        });

        return Object.freeze(descriptor);
    }

    const TEXT_OK_RESULT = createResult("text", "ok", TEXT_CONTENT_TYPE, undefined, 200, undefined, {
        kind: FAST_TEXT_OK,
    });
    const NO_CONTENT_RESULT = createResult(
        "empty",
        undefined,
        undefined,
        undefined,
        204,
        undefined,
        { kind: FAST_NO_CONTENT },
    );

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
                if (SQLITE_TEXT_DB_VALUE_KINDS.has(param.kind)) {
                    return param.toString();
                }
            }
            if (param !== null && typeof param === "object" && param[DB_BRIDGE_VALUE_MARKER] === true) {
                throw new TypeError(`Sloppy sqlite.${operation} parameter uses an unsupported DB value wrapper.`);
            }
            return param;
        });
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

    function normalizeResultMode(value, operation) {
        if (value === undefined) {
            return "object";
        }
        if (!Object.prototype.hasOwnProperty.call(DB_RESULT_MODES, value)) {
            throw new TypeError(`Sloppy ${operation} mode must be object or raw.`);
        }
        return value;
    }

    function validateProviderOperationOptions(options, operation, allowResultMode = false) {
        if (options === undefined) {
            return allowResultMode ? "object" : undefined;
        }
        if (!isPlainObject(options)) {
            throw new TypeError(`Sloppy ${operation} options must be a plain object.`);
        }
        if (!allowResultMode && Object.prototype.hasOwnProperty.call(options, "mode")) {
            throw new TypeError(`Sloppy ${operation} option 'mode' is not supported by the current runtime bridge.`);
        }
        return allowResultMode ? normalizeResultMode(options.mode, operation) : undefined;
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
                exec(sql, params, options) {
                    assertTransactionOpen("transaction.exec");
                    validateProviderOperationOptions(options, "sqlite.transaction.exec");
                    const query = normalizeSqliteQuery("exec", sql, params);
                    return bridge.transactionExec(state.handle, query.text, query.parameters);
                },
                query(sql, params, options) {
                    assertTransactionOpen("transaction.query");
                    const mode = validateProviderOperationOptions(options, "sqlite.transaction.query", true);
                    const query = normalizeSqliteQuery("query", sql, params);
                    const method = mode === "raw" ? bridge.transactionQueryRaw : bridge.transactionQuery;
                    return method(state.handle, query.text, query.parameters);
                },
                queryRaw(sql, params, options) {
                    assertTransactionOpen("transaction.queryRaw");
                    validateProviderOperationOptions(options, "sqlite.transaction.queryRaw");
                    const query = normalizeSqliteQuery("queryRaw", sql, params);
                    return bridge.transactionQueryRaw(state.handle, query.text, query.parameters);
                },
                queryOne(sql, params, options) {
                    assertTransactionOpen("transaction.queryOne");
                    validateProviderOperationOptions(options, "sqlite.transaction.queryOne");
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

        async function rollbackAfterCallbackError(error, transaction) {
            try {
                await bridge.transactionRollback(state.handle);
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

        async function commitTransaction(transaction) {
            try {
                await bridge.transactionCommit(state.handle);
            } catch (error) {
                transaction.close();
                state.transactionActive = false;
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

        return Object.freeze({
            exec(sql, params, options) {
                assertOpen("exec");
                validateProviderOperationOptions(options, "sqlite.exec");
                const query = normalizeSqliteQuery("exec", sql, params);
                return bridge.exec(state.handle, query.text, query.parameters);
            },
            query(sql, params, options) {
                assertOpen("query");
                const mode = validateProviderOperationOptions(options, "sqlite.query", true);
                const query = normalizeSqliteQuery("query", sql, params);
                const method = mode === "raw" ? bridge.queryRaw : bridge.query;
                return method(state.handle, query.text, query.parameters);
            },
            queryRaw(sql, params, options) {
                assertOpen("queryRaw");
                validateProviderOperationOptions(options, "sqlite.queryRaw");
                const query = normalizeSqliteQuery("queryRaw", sql, params);
                return bridge.queryRaw(state.handle, query.text, query.parameters);
            },
            queryOne(sql, params, options) {
                assertOpen("queryOne");
                validateProviderOperationOptions(options, "sqlite.queryOne");
                const query = normalizeSqliteQuery("queryOne", sql, params);
                return bridge.queryOne(state.handle, query.text, query.parameters);
            },
            async transaction(callback) {
                assertOpen("transaction");
                if (typeof callback !== "function") {
                    throw new TypeError("Sloppy sqlite.transaction callback must be a function.");
                }
                if (state.transactionActive) {
                    throw sqliteNestedTransactionError();
                }

                state.transactionActive = true;
                try {
                    await bridge.transactionBegin(state.handle);
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
                    throw sqliteTransactionActiveError("close");
                }

                bridge.close(state.handle);
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
            const value = String(body);
            if (options === undefined && value === "ok") {
                return TEXT_OK_RESULT;
            }
            return createResult("text", value, TEXT_CONTENT_TYPE, options, 200);
        },
        json(value, options) {
            const body = normalizeJsonDescriptorBody(value, options?.json);
            const jsonText = options === undefined ? maybeFastJsonText(value) : undefined;
            return createResult(
                "json",
                body,
                JSON_CONTENT_TYPE,
                options,
                200,
                undefined,
                jsonText === undefined ? undefined : { kind: FAST_JSON, jsonText },
            );
        },
        html(body, options) {
            return createResult("html", String(body), HTML_CONTENT_TYPE, options, 200);
        },
        bytes(body, options) {
            return createResult(
                "bytes",
                copyBytes(body),
                resolveContentType(options, BYTES_CONTENT_TYPE),
                options,
                200,
            );
        },
        async stream(callback, options) {
            if (typeof callback !== "function") {
                throw new TypeError("Sloppy Results.stream callback must be a function.");
            }
            const chunks = [];
            let totalBytes = 0;
            let closed = false;
            function appendChunk(chunk) {
                if (chunk.byteLength > STREAM_MAX_CHUNK_BYTES) {
                    throw new TypeError("Sloppy Results.stream chunk exceeds the bounded stream limit.");
                }
                if (totalBytes + chunk.byteLength > STREAM_MAX_TOTAL_BYTES) {
                    throw new TypeError("Sloppy Results.stream body exceeds the bounded stream limit.");
                }
                totalBytes += chunk.byteLength;
                chunks.push(chunk);
            }
            const writer = Object.freeze({
                writeText(text) {
                    if (closed) {
                        throw new TypeError("Sloppy stream writer is closed.");
                    }
                    appendChunk(encodeUtf8Text(text));
                },
                writeBytes(bytes) {
                    if (closed) {
                        throw new TypeError("Sloppy stream writer is closed.");
                    }
                    appendChunk(copyBytes(bytes));
                },
                close() {
                    closed = true;
                },
            });
            try {
                await callback(writer);
            } finally {
                closed = true;
            }
            return createResult(
                "stream",
                Object.freeze(chunks),
                resolveContentType(options, STREAM_CONTENT_TYPE),
                options,
                200,
                { chunks: Object.freeze(chunks) },
            );
        },
        ok(value, options) {
            const body = normalizeJsonDescriptorBody(value, options?.json);
            const jsonText = options === undefined ? maybeFastJsonText(value) : undefined;
            return createResult(
                "json",
                body,
                JSON_CONTENT_TYPE,
                options,
                200,
                undefined,
                jsonText === undefined ? undefined : { kind: FAST_JSON, jsonText },
            );
        },
        created(location, value, options) {
            if (typeof location !== "string" || location.length === 0) {
                throw new TypeError("Sloppy Results.created location must be a non-empty string.");
            }

            const mergedOptions = { status: 201, ...options };
            const body = normalizeJsonDescriptorBody(value, options?.json);
            const jsonText = options === undefined ? maybeFastJsonText(value) : undefined;

            return createResult(
                "json",
                body,
                JSON_CONTENT_TYPE,
                mergedOptions,
                201,
                { location },
                jsonText === undefined ? undefined : { kind: FAST_CREATED, jsonText },
            );
        },
        accepted(value, options) {
            return createResult(
                "json",
                normalizeJsonDescriptorBody(value, options?.json),
                JSON_CONTENT_TYPE,
                { status: 202, ...options },
                202,
            );
        },
        noContent() {
            return NO_CONTENT_RESULT;
        },
        notFound(valueOrProblem, options) {
            return createResult(
                "json",
                normalizeJsonDescriptorBody(valueOrProblem, options?.json),
                JSON_CONTENT_TYPE,
                { status: 404, ...options },
                404,
            );
        },
        badRequest(valueOrProblem, options) {
            return createResult(
                "json",
                normalizeJsonDescriptorBody(valueOrProblem, options?.json),
                JSON_CONTENT_TYPE,
                { status: 400, ...options },
                400,
            );
        },
        unauthorized(valueOrProblem, options) {
            return createResult(
                "json",
                normalizeJsonDescriptorBody(valueOrProblem, options?.json),
                JSON_CONTENT_TYPE,
                { status: 401, ...options },
                401,
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
                normalizeJsonDescriptorBody(value, options?.json),
                JSON_CONTENT_TYPE,
                { ...options, status: statusCode },
                statusCode,
            );
        },
        problem(problemOrMessage, options) {
            const status = resolveStatus(options, 500);
            const body = normalizeJsonBody(normalizeProblem(problemOrMessage, status), options?.json);
            return createResult(
                "problem",
                body,
                PROBLEM_CONTENT_TYPE,
                { ...options, status },
                status,
            );
        },
    });

    const ProblemDetails = Object.freeze({
        defaults(options) {
            if (options !== undefined && !isPlainObject(options)) {
                throw new TypeError("Sloppy ProblemDetails.defaults options must be a plain object.");
            }
            const detail = options?.detail ?? "never";
            if (detail !== "never" && detail !== "development" && detail !== "always") {
                throw new TypeError("Sloppy ProblemDetails detail policy must be never, development, or always.");
            }
            return Object.freeze({
                __sloppyProblemDetails: true,
                detail,
            });
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

    function requirePostgresBridge() {
        const bridge = globalThis.__sloppy?.data?.postgres;

        if (bridge === undefined) {
            throw new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature provider.postgres is inactive or unavailable

Provider:
  postgres

Feature:
  provider.postgres

Operation:
  open

Reason:
  The active Sloppy Plan did not enable the __sloppy.data.postgres V8 intrinsic namespace.`);
        }

        return bridge;
    }

    function normalizePostgresOpenOptions(options) {
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy postgres.open options must be a plain object.");
        }
        if (typeof options.connectionString !== "string" || options.connectionString.length === 0) {
            throw new TypeError("Sloppy postgres.open connectionString must be a non-empty string.");
        }
        const access = options.access ?? "readwrite";
        const maxConnections = options.maxConnections ?? 1;
        if (access !== "read" && access !== "readwrite") {
            throw new TypeError("Sloppy postgres.open access must be read or readwrite.");
        }
        if (!Number.isInteger(maxConnections) || maxConnections < 1 ||
            maxConnections > POSTGRES_MAX_POOL_CONNECTIONS)
        {
            throw new TypeError(
                `Sloppy postgres.open maxConnections must be an integer from 1 to ${POSTGRES_MAX_POOL_CONNECTIONS}.`,
            );
        }
        return Object.freeze({
            provider: "postgres",
            connectionString: options.connectionString,
            capability: options.capability ?? "data.postgres",
            access,
            maxConnections,
        });
    }

    function normalizePostgresParams(params, operation) {
        if (params === undefined) {
            return [];
        }
        if (!Array.isArray(params)) {
            throw new TypeError(`Sloppy postgres.${operation} parameters must be an array.`);
        }
        return params;
    }

    function normalizePostgresQuery(operation, sql, params) {
        if (typeof sql !== "string" || sql.length === 0) {
            throw new TypeError(`Sloppy postgres.${operation} SQL must be a non-empty string.`);
        }
        return {
            text: sql,
            parameters: normalizePostgresParams(params, operation),
        };
    }

    function createPostgresConnection(bridge, handle) {
        const state = {
            closed: false,
            handle,
            transactionActive: false,
        };

        function assertOpen(operation) {
            if (state.closed) {
                throw new Error(`sloppy: postgres connection is closed

Provider:
  postgres

Operation:
  ${operation}`);
            }
        }

        function createTransaction() {
            const txState = {
                closed: false,
            };
            function assertTransactionOpen(operation) {
                assertOpen(operation);
                if (txState.closed) {
                    throw new Error(`sloppy: postgres transaction scope is closed

Provider:
  postgres

Operation:
  ${operation}`);
                }
            }
            const tx = Object.freeze({
                exec(sql, params, options) {
                    assertTransactionOpen("transaction.exec");
                    validateProviderOperationOptions(options, "postgres.transaction.exec");
                    const query = normalizePostgresQuery("exec", sql, params);
                    return bridge.transactionExec(state.handle, query.text, query.parameters);
                },
                query(sql, params, options) {
                    assertTransactionOpen("transaction.query");
                    const mode = validateProviderOperationOptions(options, "postgres.transaction.query", true);
                    const query = normalizePostgresQuery("query", sql, params);
                    const method = mode === "raw" ? bridge.transactionQueryRaw : bridge.transactionQuery;
                    return method(state.handle, query.text, query.parameters);
                },
                queryRaw(sql, params, options) {
                    assertTransactionOpen("transaction.queryRaw");
                    validateProviderOperationOptions(options, "postgres.transaction.queryRaw");
                    const query = normalizePostgresQuery("queryRaw", sql, params);
                    return bridge.transactionQueryRaw(state.handle, query.text, query.parameters);
                },
                queryOne(sql, params, options) {
                    assertTransactionOpen("transaction.queryOne");
                    validateProviderOperationOptions(options, "postgres.transaction.queryOne");
                    const query = normalizePostgresQuery("queryOne", sql, params);
                    return bridge.transactionQueryOne(state.handle, query.text, query.parameters);
                },
                transaction() {
                    throw new Error("sloppy: postgres nested transactions are not supported");
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
                await bridge.transactionRollback(state.handle);
            } catch {
                transaction.close();
                state.closed = true;
                try {
                    bridge.close(state.handle);
                } catch {
                    // Preserve the callback error while preventing resource reuse.
                }
                throw error;
            }
            transaction.close();
            state.transactionActive = false;
            throw error;
        }

        async function commitTransaction(transaction) {
            try {
                await bridge.transactionCommit(state.handle);
            } catch (error) {
                transaction.close();
                state.transactionActive = false;
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

        return Object.freeze({
            exec(sql, params, options) {
                assertOpen("exec");
                validateProviderOperationOptions(options, "postgres.exec");
                const query = normalizePostgresQuery("exec", sql, params);
                return bridge.exec(state.handle, query.text, query.parameters);
            },
            query(sql, params, options) {
                assertOpen("query");
                const mode = validateProviderOperationOptions(options, "postgres.query", true);
                const query = normalizePostgresQuery("query", sql, params);
                const method = mode === "raw" ? bridge.queryRaw : bridge.query;
                return method(state.handle, query.text, query.parameters);
            },
            queryRaw(sql, params, options) {
                assertOpen("queryRaw");
                validateProviderOperationOptions(options, "postgres.queryRaw");
                const query = normalizePostgresQuery("queryRaw", sql, params);
                return bridge.queryRaw(state.handle, query.text, query.parameters);
            },
            queryOne(sql, params, options) {
                assertOpen("queryOne");
                validateProviderOperationOptions(options, "postgres.queryOne");
                const query = normalizePostgresQuery("queryOne", sql, params);
                return bridge.queryOne(state.handle, query.text, query.parameters);
            },
            async transaction(callback) {
                assertOpen("transaction");
                if (typeof callback !== "function") {
                    throw new TypeError("Sloppy postgres.transaction callback must be a function.");
                }
                if (state.transactionActive) {
                    throw new Error("sloppy: postgres nested transactions are not supported");
                }
                state.transactionActive = true;
                try {
                    await bridge.transactionBegin(state.handle);
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
                bridge.close(state.handle);
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

    const postgres = Object.freeze({
        open(options) {
            const bridge = requirePostgresBridge();
            return createPostgresConnection(bridge, bridge.open(normalizePostgresOpenOptions(options)));
        },
    });

    function requireSqlServerBridge() {
        const bridge = globalThis.__sloppy?.data?.sqlserver;

        if (bridge === undefined) {
            throw new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature provider.sqlserver is inactive or unavailable

Provider:
  sqlserver

Feature:
  provider.sqlserver

Operation:
  open

Reason:
  The active Sloppy Plan did not enable the __sloppy.data.sqlserver V8 intrinsic namespace.`);
        }

        return bridge;
    }

    function normalizeSqlServerOpenOptions(options) {
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy sqlserver.open options must be a plain object.");
        }
        if (typeof options.connectionString !== "string" || options.connectionString.length === 0) {
            throw new TypeError("Sloppy sqlserver.open connectionString must be a non-empty string.");
        }
        const access = options.access ?? "readwrite";
        const maxConnections = options.maxConnections ?? 1;
        if (access !== "read" && access !== "readwrite") {
            throw new TypeError("Sloppy sqlserver.open access must be read or readwrite.");
        }
        if (!Number.isInteger(maxConnections) || maxConnections < 1 ||
            maxConnections > SQLSERVER_MAX_POOL_CONNECTIONS)
        {
            throw new TypeError(
                `Sloppy sqlserver.open maxConnections must be an integer from 1 to ${SQLSERVER_MAX_POOL_CONNECTIONS}.`,
            );
        }
        return Object.freeze({
            provider: "sqlserver",
            connectionString: options.connectionString,
            capability: options.capability ?? "data.sqlserver",
            access,
            maxConnections,
        });
    }

    function normalizeSqlServerParams(params, operation) {
        if (params === undefined) {
            return [];
        }
        if (!Array.isArray(params)) {
            throw new TypeError(`Sloppy sqlserver.${operation} parameters must be an array.`);
        }
        return params;
    }

    function normalizeSqlServerQuery(operation, sql, params) {
        if (typeof sql !== "string" || sql.length === 0) {
            throw new TypeError(`Sloppy sqlserver.${operation} SQL must be a non-empty string.`);
        }
        return {
            text: sql,
            parameters: normalizeSqlServerParams(params, operation),
        };
    }

    function createSqlServerConnection(bridge, handle) {
        const state = {
            closed: false,
            handle,
            transactionActive: false,
        };

        function assertOpen(operation) {
            if (state.closed) {
                throw new Error(`sloppy: sqlserver connection is closed

Provider:
  sqlserver

Operation:
  ${operation}`);
            }
        }

        function createTransaction() {
            const txState = {
                closed: false,
            };
            function assertTransactionOpen(operation) {
                assertOpen(operation);
                if (txState.closed) {
                    throw new Error(`sloppy: sqlserver transaction scope is closed

Provider:
  sqlserver

Operation:
  ${operation}`);
                }
            }
            const tx = Object.freeze({
                exec(sql, params, options) {
                    assertTransactionOpen("transaction.exec");
                    validateProviderOperationOptions(options, "sqlserver.transaction.exec");
                    const query = normalizeSqlServerQuery("exec", sql, params);
                    return bridge.transactionExec(state.handle, query.text, query.parameters);
                },
                query(sql, params, options) {
                    assertTransactionOpen("transaction.query");
                    const mode = validateProviderOperationOptions(options, "sqlserver.transaction.query", true);
                    const query = normalizeSqlServerQuery("query", sql, params);
                    const method = mode === "raw" ? bridge.transactionQueryRaw : bridge.transactionQuery;
                    return method(state.handle, query.text, query.parameters);
                },
                queryRaw(sql, params, options) {
                    assertTransactionOpen("transaction.queryRaw");
                    validateProviderOperationOptions(options, "sqlserver.transaction.queryRaw");
                    const query = normalizeSqlServerQuery("queryRaw", sql, params);
                    return bridge.transactionQueryRaw(state.handle, query.text, query.parameters);
                },
                queryOne(sql, params, options) {
                    assertTransactionOpen("transaction.queryOne");
                    validateProviderOperationOptions(options, "sqlserver.transaction.queryOne");
                    const query = normalizeSqlServerQuery("queryOne", sql, params);
                    return bridge.transactionQueryOne(state.handle, query.text, query.parameters);
                },
                transaction() {
                    throw new Error("sloppy: sqlserver nested transactions are not supported");
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
                await bridge.transactionRollback(state.handle);
            } catch {
                transaction.close();
                state.closed = true;
                try {
                    bridge.close(state.handle);
                } catch {
                    // Preserve the callback error while preventing resource reuse.
                }
                throw error;
            }
            transaction.close();
            state.transactionActive = false;
            throw error;
        }

        async function commitTransaction(transaction) {
            try {
                await bridge.transactionCommit(state.handle);
            } catch (error) {
                transaction.close();
                state.transactionActive = false;
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

        return Object.freeze({
            exec(sql, params, options) {
                assertOpen("exec");
                validateProviderOperationOptions(options, "sqlserver.exec");
                const query = normalizeSqlServerQuery("exec", sql, params);
                return bridge.exec(state.handle, query.text, query.parameters);
            },
            query(sql, params, options) {
                assertOpen("query");
                const mode = validateProviderOperationOptions(options, "sqlserver.query", true);
                const query = normalizeSqlServerQuery("query", sql, params);
                const method = mode === "raw" ? bridge.queryRaw : bridge.query;
                return method(state.handle, query.text, query.parameters);
            },
            queryRaw(sql, params, options) {
                assertOpen("queryRaw");
                validateProviderOperationOptions(options, "sqlserver.queryRaw");
                const query = normalizeSqlServerQuery("queryRaw", sql, params);
                return bridge.queryRaw(state.handle, query.text, query.parameters);
            },
            queryOne(sql, params, options) {
                assertOpen("queryOne");
                validateProviderOperationOptions(options, "sqlserver.queryOne");
                const query = normalizeSqlServerQuery("queryOne", sql, params);
                return bridge.queryOne(state.handle, query.text, query.parameters);
            },
            async transaction(callback) {
                assertOpen("transaction");
                if (typeof callback !== "function") {
                    throw new TypeError("Sloppy sqlserver.transaction callback must be a function.");
                }
                if (state.transactionActive) {
                    throw new Error("sloppy: sqlserver nested transactions are not supported");
                }
                state.transactionActive = true;
                try {
                    await bridge.transactionBegin(state.handle);
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
                bridge.close(state.handle);
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

    const sqlserver = Object.freeze({
        open(options) {
            const bridge = requireSqlServerBridge();
            return createSqlServerConnection(bridge, bridge.open(normalizeSqlServerOpenOptions(options)));
        },
    });

    const data = Object.freeze({
        sqlite,
        postgres,
        sqlserver,
        values,
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

    function normalizeMigrationOptions(options) {
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy Migrations options must be a plain object.");
        }
        const provider = options.provider;
        const path = options.path;
        if (typeof provider !== "string" || provider.length === 0) {
            throw new TypeError("Sloppy Migrations provider must be a non-empty string.");
        }
        if (typeof path !== "string" || path.length === 0) {
            throw new TypeError("Sloppy Migrations path must be a non-empty string.");
        }
        if (
            /^(?:[A-Za-z]:[\\/]|[\\/]|[A-Za-z][A-Za-z0-9_.-]*:[\\/])/.test(path) ||
            /(^|[\\/])\.\.(?:[\\/]|$)/.test(path)
        ) {
            throw new Error(`sloppy: migration path is unsupported

Provider:
  ${provider}

Path:
  ${path}

Fix:
  Use a project-relative directory glob ending in *.sql, for example migrations/*.sql.`);
        }
        const slash = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"));
        const directory = slash < 0 ? "." : path.slice(0, slash);
        const pattern = slash < 0 ? path : path.slice(slash + 1);
        if (pattern !== "*.sql") {
            throw new Error(`sloppy: migration path is unsupported

Provider:
  ${provider}

Path:
  ${path}

Fix:
  Use a project-relative directory glob ending in *.sql, for example migrations/*.sql.`);
        }
        return { provider, path, directory };
    }

    function migrationHash(text) {
        let hash = 0x811c9dc5;
        const addByte = (value) => {
            hash ^= value & 0xff;
            hash = Math.imul(hash, 0x01000193) >>> 0;
        };
        for (let index = 0; index < text.length; index += 1) {
            let code = text.codePointAt(index);
            if (code > 0xffff) {
                index += 1;
            }
            if (code <= 0x7f) {
                addByte(code);
            } else if (code <= 0x7ff) {
                addByte(0xc0 | (code >> 6));
                addByte(0x80 | (code & 0x3f));
            } else if (code <= 0xffff) {
                addByte(0xe0 | (code >> 12));
                addByte(0x80 | ((code >> 6) & 0x3f));
                addByte(0x80 | (code & 0x3f));
            } else {
                addByte(0xf0 | (code >> 18));
                addByte(0x80 | ((code >> 12) & 0x3f));
                addByte(0x80 | ((code >> 6) & 0x3f));
                addByte(0x80 | (code & 0x3f));
            }
        }
        return `${MIGRATION_HASH_PREFIX}${hash.toString(16).padStart(8, "0")}`;
    }

    function migrationProviderKind(db) {
        const debug = typeof db?.__debug === "function" ? db.__debug() : db;
        if (debug?.kind === "sqlite-connection") {
            return "sqlite";
        }
        if (debug?.kind === "postgres-connection") {
            return "postgres";
        }
        if (debug?.kind === "sqlserver-connection") {
            return "sqlserver";
        }
        throw new TypeError(
            "Sloppy Migrations only supports sqlite, postgres, and sqlserver connections created by sloppy/data.",
        );
    }

    function resolveMigrationProviderKind(db, options) {
        const providerKind = migrationProviderKind(db);
        if (MIGRATION_PROVIDER_KINDS[options.provider] === true && options.provider !== providerKind) {
            throw new TypeError(
                `Sloppy Migrations provider '${options.provider}' does not match connection provider '${providerKind}'.`,
            );
        }
        return providerKind;
    }

    const MIGRATION_SQL = Object.freeze({
        sqlite: Object.freeze({
            ensure:
                `create table if not exists ${MIGRATIONS_TABLE} (` +
                "id integer primary key autoincrement, " +
                "name text not null unique, " +
                "hash text not null, " +
                "appliedAt text not null)",
            select:
                `select name, hash, appliedAt as "appliedAt" from ${MIGRATIONS_TABLE} order by id`,
            insert: `insert into ${MIGRATIONS_TABLE} (name, hash, appliedAt) values (?, ?, ?)`,
            appliedAt: () => new Date().toISOString(),
        }),
        postgres: Object.freeze({
            ensure:
                `create table if not exists ${MIGRATIONS_TABLE} (` +
                "id bigint generated by default as identity primary key, " +
                "name text not null unique, " +
                "hash text not null, " +
                "appliedAt text not null)",
            select:
                `select name, hash, appliedAt as "appliedAt" from ${MIGRATIONS_TABLE} order by id`,
            insert:
                `insert into ${MIGRATIONS_TABLE} (name, hash, appliedAt) values (` +
                "$1, $2, to_char(clock_timestamp() at time zone 'UTC', " +
                `'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'))`,
            appliedAt: () => undefined,
        }),
        sqlserver: Object.freeze({
            ensure:
                "if object_id(N'dbo._sloppy_migrations', N'U') is null create table " +
                "dbo._sloppy_migrations (id bigint identity(1,1) primary key, " +
                "name nvarchar(450) not null unique, hash nvarchar(64) not null, " +
                "appliedAt nvarchar(64) not null)",
            select: "select name, hash, appliedAt from dbo._sloppy_migrations order by id",
            insert:
                "insert into dbo._sloppy_migrations (name, hash, appliedAt) values " +
                "(?, ?, convert(nvarchar(64), sysutcdatetime(), 127) + N'Z')",
            appliedAt: () => undefined,
        }),
    });

    async function listMigrationFiles(options) {
        let entries;
        try {
            entries = await Directory.list(migrationFilesystemPath(options.directory));
        } catch (error) {
            throw new Error(`sloppy: migration directory is missing or unreadable

Provider:
  ${options.provider}

Path:
  ${options.path}

Fix:
  Create the configured migration directory before applying migrations.`, { cause: error });
        }
        return entries
            .filter((entry) => entry.kind === "file" && entry.name.endsWith(".sql"))
            .map((entry) => entry.name)
            .sort((left, right) => (left === right ? 0 : left < right ? -1 : 1))
            .map((name) => ({
                name,
                path: options.directory === "." ? name : `${options.directory}/${name}`,
            }));
    }

    function migrationFilesystemPath(path) {
        if (/(^|[\\/])\.\.(?:[\\/]|$)/.test(path)) {
            throw new Error(`sloppy: migration path is unsupported

Path:
  ${path}

Fix:
  Use a project-relative directory glob ending in *.sql, for example migrations/*.sql.`);
        }
        if (path.startsWith(".") || path.startsWith("/") || path.startsWith("\\") || path.includes(":/")) {
            return path;
        }
        return `./${path}`;
    }

    async function ensureMigrationsTable(db, providerKind) {
        await db.exec(MIGRATION_SQL[providerKind].ensure, []);
    }

    async function readAppliedMigrations(db, providerKind) {
        await ensureMigrationsTable(db, providerKind);
        const rows = await db.query(MIGRATION_SQL[providerKind].select, []);
        const applied = new Map();
        for (const row of rows) {
            applied.set(row.name, row);
        }
        return applied;
    }

    function migrationStatusFor(files, applied) {
        return files.map((file) => {
            const appliedRow = applied.get(file.name) ?? null;
            if (appliedRow === null) {
                return Object.freeze({ name: file.name, path: file.path, status: "pending" });
            }
            if (appliedRow.hash !== file.hash) {
                return Object.freeze({
                    name: file.name,
                    path: file.path,
                    status: "changed",
                    appliedHash: appliedRow.hash,
                    currentHash: file.hash,
                    appliedAt: appliedRow.appliedAt,
                });
            }
            return Object.freeze({
                name: file.name,
                path: file.path,
                status: "applied",
                hash: file.hash,
                appliedAt: appliedRow.appliedAt,
            });
        });
    }

    async function migrationFilesWithContent(options) {
        const files = await listMigrationFiles(options);
        const withContent = [];
        for (const file of files) {
            const sqlText = await File.readText(migrationFilesystemPath(file.path));
            withContent.push({
                ...file,
                sql: sqlText,
                hash: migrationHash(sqlText),
            });
        }
        return withContent;
    }

    function assertMigrationHashNotChanged(record) {
        if (record.status !== "changed") {
            return;
        }
        throw new Error(`sloppy: applied migration hash changed

Migration:
  ${record.name}

Applied hash:
  ${record.appliedHash}

Current hash:
  ${record.currentHash}

Fix:
  Create a new migration file instead of editing an already-applied migration.`);
    }

    async function migrationStatus(db, options) {
        const checked = normalizeMigrationOptions(options);
        const providerKind = resolveMigrationProviderKind(db, checked);
        const files = await migrationFilesWithContent(checked);
        const applied = await readAppliedMigrations(db, providerKind);
        const migrations = migrationStatusFor(files, applied);
        const changed = migrations.some((migration) => migration.status === "changed");
        const pending = migrations.filter((migration) => migration.status === "pending").length;
        return Object.freeze({
            provider: checked.provider,
            path: checked.path,
            status: changed ? "changed" : pending > 0 ? "pending" : "current",
            pending,
            applied: migrations.filter((migration) => migration.status === "applied").length,
            migrations: Object.freeze(migrations),
        });
    }

    async function applyMigrations(db, options) {
        const checked = normalizeMigrationOptions(options);
        const providerKind = resolveMigrationProviderKind(db, checked);
        const dialect = MIGRATION_SQL[providerKind];
        const files = await migrationFilesWithContent(checked);
        const applied = await readAppliedMigrations(db, providerKind);
        const records = migrationStatusFor(files, applied);
        for (const record of records) {
            assertMigrationHashNotChanged(record);
        }

        let appliedCount = 0;
        for (const file of files) {
            if (applied.has(file.name)) {
                continue;
            }
            await db.transaction(async (tx) => {
                await tx.exec(file.sql, []);
                const appliedAt = dialect.appliedAt();
                const params = appliedAt === undefined
                    ? [file.name, file.hash]
                    : [file.name, file.hash, appliedAt];
                await tx.exec(dialect.insert, params);
            });
            appliedCount += 1;
        }

        return Object.freeze({
            provider: checked.provider,
            path: checked.path,
            applied: appliedCount,
            skipped: files.length - appliedCount,
        });
    }

    async function checkProviderHealth(db, options = {}) {
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy ProviderHealth options must be a plain object.");
        }
        const providerKind = migrationProviderKind(db);
        const provider = options.provider ?? providerKind;
        if (typeof provider !== "string" || provider.length === 0) {
            throw new TypeError("Sloppy ProviderHealth provider must be a non-empty string.");
        }
        if (MIGRATION_PROVIDER_KINDS[provider] === true && provider !== providerKind) {
            throw new TypeError(
                `Sloppy ProviderHealth provider '${provider}' does not match connection provider '${providerKind}'.`,
            );
        }
        await db.queryOne("select 1 as ok", []);
        return Object.freeze({ provider, ok: true });
    }

    const Migrations = Object.freeze({
        apply: applyMigrations,
        status: migrationStatus,
    });

    const ProviderHealth = Object.freeze({
        check: checkProviderHealth,
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
  Runtime scheduling requires the native time bridge to be registered in the active V8 lane.`);
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
            return sloppyNativeCrypto("NonCryptoHash.xxHash64").nonCryptoXxHash64(
                sloppyCryptoDataToBytes(data, "NonCryptoHash.xxHash64"),
            );
        },
    });

    const BASE64_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const BASE64URL_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const HEX_ALPHABET = "0123456789abcdef";

    class CodecError extends Error {
        constructor(code, message) {
            super(`${code}: ${message}`);
            this.name = "CodecError";
            this.code = code;
        }
    }

    function codecError(code, message) {
        return new CodecError(code, message);
    }

    function requireBytes(value, operation) {
        if (!(value instanceof Uint8Array)) {
            throw new TypeError(`${operation} requires Uint8Array bytes.`);
        }
        return value;
    }

    function requireString(value, operation) {
        if (typeof value !== "string") {
            throw new TypeError(`${operation} requires a string.`);
        }
        return value;
    }

    function validateOptionsObject(options, operation) {
        if (options === undefined) {
            return {};
        }
        if (!isPlainObject(options)) {
            throw new TypeError(`${operation} options must be an object when provided.`);
        }
        return options;
    }

    function rejectUnknownOptions(options, allowed, operation) {
        for (const key of Object.keys(options)) {
            if (!allowed.has(key)) {
                throw new TypeError(`${operation} does not support option ${key}.`);
            }
        }
    }

    function encodeBase64(bytes, alphabet, padding) {
        let output = "";
        for (let offset = 0; offset < bytes.length; offset += 3) {
            const b0 = bytes[offset];
            const b1 = offset + 1 < bytes.length ? bytes[offset + 1] : 0;
            const b2 = offset + 2 < bytes.length ? bytes[offset + 2] : 0;
            const triple = (b0 << 16) | (b1 << 8) | b2;
            output += alphabet[(triple >>> 18) & 0x3f];
            output += alphabet[(triple >>> 12) & 0x3f];
            output += offset + 1 < bytes.length ? alphabet[(triple >>> 6) & 0x3f] : "=";
            output += offset + 2 < bytes.length ? alphabet[triple & 0x3f] : "=";
        }
        return padding ? output : output.replace(/=+$/u, "");
    }

    function makeAlphabetMap(alphabet) {
        const map = new Map();
        for (let index = 0; index < alphabet.length; index += 1) {
            map.set(alphabet[index], index);
        }
        return map;
    }

    const BASE64_MAP = makeAlphabetMap(BASE64_ALPHABET);
    const BASE64URL_MAP = makeAlphabetMap(BASE64URL_ALPHABET);

    function normalizeBase64Input(text, kind, paddingMode) {
        const code =
            kind === "base64url" ? "SLOPPY_E_CODEC_INVALID_BASE64URL" : "SLOPPY_E_CODEC_INVALID_BASE64";
        if (text.length === 0) {
            return text;
        }
        if (/\s/u.test(text)) {
            throw codecError(code, `${kind} input must not contain whitespace.`);
        }
        const firstPadding = text.indexOf("=");
        if (firstPadding !== -1 && !/^=+$/u.test(text.slice(firstPadding))) {
            throw codecError(code, `${kind} padding must appear only at the end.`);
        }
        if (kind === "base64url") {
            if (/[+/]/u.test(text)) {
                throw codecError(code, "Base64Url input must use the URL-safe alphabet.");
            }
            if (paddingMode === "forbidden" && firstPadding !== -1) {
                throw codecError(code, "Base64Url padding is forbidden for this decode.");
            }
            if (paddingMode === "required" && text.length % 4 !== 0) {
                throw codecError(code, "Base64Url padding is required for this decode.");
            }
        } else if (/[-_]/u.test(text)) {
            throw codecError(code, "Base64 input must use the standard alphabet.");
        }
        const paddingCount = firstPadding === -1 ? 0 : text.length - firstPadding;
        if (paddingCount > 2) {
            throw codecError(code, `${kind} input has invalid padding.`);
        }
        if (text.length % 4 === 1) {
            throw codecError(code, `${kind} input length is impossible.`);
        }
        if (text.length % 4 !== 0) {
            if (kind !== "base64url" || paddingMode === "required" || paddingCount !== 0) {
                throw codecError(code, `${kind} input length is invalid.`);
            }
            return text.padEnd(text.length + (4 - (text.length % 4)), "=");
        }
        return text;
    }

    function decodeBase64(text, alphabetMap, kind, paddingMode) {
        text = normalizeBase64Input(requireString(text, `${kind}.decode`), kind, paddingMode);
        const code =
            kind === "base64url" ? "SLOPPY_E_CODEC_INVALID_BASE64URL" : "SLOPPY_E_CODEC_INVALID_BASE64";
        const bytes = [];
        for (let offset = 0; offset < text.length; offset += 4) {
            const c0 = text[offset];
            const c1 = text[offset + 1];
            const c2 = text[offset + 2];
            const c3 = text[offset + 3];
            const p2 = c2 === "=";
            const p3 = c3 === "=";
            if (c0 === "=" || c1 === "=" || (p2 && !p3)) {
                throw codecError(code, `${kind} input has invalid padding placement.`);
            }
            const v0 = alphabetMap.get(c0);
            const v1 = alphabetMap.get(c1);
            const v2 = p2 ? 0 : alphabetMap.get(c2);
            const v3 = p3 ? 0 : alphabetMap.get(c3);
            if (v0 === undefined || v1 === undefined || v2 === undefined || v3 === undefined) {
                throw codecError(code, `${kind} input contains a non-alphabet character.`);
            }
            if (p2 && (v1 & 0x0f) !== 0) {
                throw codecError(code, `${kind} input has non-canonical trailing bits.`);
            }
            if (!p2 && p3 && (v2 & 0x03) !== 0) {
                throw codecError(code, `${kind} input has non-canonical trailing bits.`);
            }
            const triple = (v0 << 18) | (v1 << 12) | (v2 << 6) | v3;
            bytes.push((triple >>> 16) & 0xff);
            if (!p2) {
                bytes.push((triple >>> 8) & 0xff);
            }
            if (!p3) {
                bytes.push(triple & 0xff);
            }
        }
        return new Uint8Array(bytes);
    }

    function parseBase64UrlEncodeOptions(options) {
        options = validateOptionsObject(options, "Base64Url.encode");
        rejectUnknownOptions(options, new Set(["padding"]), "Base64Url.encode");
        if (options.padding === undefined) {
            return false;
        }
        if (typeof options.padding !== "boolean") {
            throw new TypeError("Base64Url.encode padding must be boolean.");
        }
        return options.padding;
    }

    function parseBase64UrlDecodeOptions(options) {
        options = validateOptionsObject(options, "Base64Url.decode");
        rejectUnknownOptions(options, new Set(["padding"]), "Base64Url.decode");
        const padding = options.padding ?? "optional";
        if (padding !== "optional" && padding !== "required" && padding !== "forbidden") {
            throw new TypeError('Base64Url.decode padding must be "optional", "required", or "forbidden".');
        }
        return padding;
    }

    function decodeHexNibble(code) {
        if (code >= 48 && code <= 57) {
            return code - 48;
        }
        if (code >= 65 && code <= 70) {
            return code - 55;
        }
        if (code >= 97 && code <= 102) {
            return code - 87;
        }
        return -1;
    }

    function encodeUtf8(text) {
        text = requireString(text, "Text.utf8.encode");
        const bytes = [];
        for (const char of text) {
            let codePoint = char.codePointAt(0);
            if (codePoint >= 0xd800 && codePoint <= 0xdfff) {
                codePoint = 0xfffd;
            }
            if (codePoint <= 0x7f) {
                bytes.push(codePoint);
            } else if (codePoint <= 0x7ff) {
                bytes.push(0xc0 | (codePoint >>> 6), 0x80 | (codePoint & 0x3f));
            } else if (codePoint <= 0xffff) {
                bytes.push(0xe0 | (codePoint >>> 12), 0x80 | ((codePoint >>> 6) & 0x3f), 0x80 | (codePoint & 0x3f));
            } else {
                bytes.push(
                    0xf0 | (codePoint >>> 18),
                    0x80 | ((codePoint >>> 12) & 0x3f),
                    0x80 | ((codePoint >>> 6) & 0x3f),
                    0x80 | (codePoint & 0x3f),
                );
            }
        }
        return new Uint8Array(bytes);
    }

    function utf8Malformed(fatal, message) {
        if (fatal) {
            throw codecError("SLOPPY_E_CODEC_MALFORMED_UTF8", message);
        }
        return "\uFFFD";
    }

    function isContinuation(byte) {
        return byte >= 0x80 && byte <= 0xbf;
    }

    function decodeUtf8Bytes(bytes, options) {
        const fatal = options.fatal;
        const stream = options.stream;
        let output = "";
        let offset = 0;
        while (offset < bytes.length) {
            const b0 = bytes[offset];
            if (b0 <= 0x7f) {
                output += String.fromCodePoint(b0);
                offset += 1;
                continue;
            }
            let needed = 0;
            let codePoint = 0;
            let minSecond = 0x80;
            let maxSecond = 0xbf;
            if (b0 >= 0xc2 && b0 <= 0xdf) {
                needed = 2;
                codePoint = b0 & 0x1f;
            } else if (b0 >= 0xe0 && b0 <= 0xef) {
                needed = 3;
                codePoint = b0 & 0x0f;
                if (b0 === 0xe0) {
                    minSecond = 0xa0;
                } else if (b0 === 0xed) {
                    maxSecond = 0x9f;
                }
            } else if (b0 >= 0xf0 && b0 <= 0xf4) {
                needed = 4;
                codePoint = b0 & 0x07;
                if (b0 === 0xf0) {
                    minSecond = 0x90;
                } else if (b0 === 0xf4) {
                    maxSecond = 0x8f;
                }
            } else {
                output += utf8Malformed(fatal, "UTF-8 input contains an invalid leading byte.");
                offset += 1;
                continue;
            }
            if (offset + 1 >= bytes.length) {
                if (stream) {
                    break;
                }
                output += utf8Malformed(fatal, "UTF-8 input ended with an incomplete sequence.");
                offset = bytes.length;
                break;
            }
            const b1 = bytes[offset + 1];
            if (b1 < minSecond || b1 > maxSecond) {
                output += utf8Malformed(fatal, "UTF-8 input contains an invalid continuation byte.");
                offset += 1;
                continue;
            }
            if (offset + needed > bytes.length) {
                if (stream) {
                    break;
                }
                output += utf8Malformed(fatal, "UTF-8 input ended with an incomplete sequence.");
                offset = bytes.length;
                break;
            }
            codePoint = (codePoint << 6) | (b1 & 0x3f);
            let valid = true;
            let invalidIndex = 0;
            for (let index = 2; index < needed; index += 1) {
                const next = bytes[offset + index];
                if (!isContinuation(next)) {
                    valid = false;
                    invalidIndex = index;
                    break;
                }
                codePoint = (codePoint << 6) | (next & 0x3f);
            }
            if (!valid) {
                output += utf8Malformed(fatal, "UTF-8 input contains an invalid continuation byte.");
                offset += invalidIndex;
                continue;
            }
            output += String.fromCodePoint(codePoint);
            offset += needed;
        }
        return { output, remaining: bytes.slice(offset) };
    }

    function parseUtf8Options(options, operation) {
        options = validateOptionsObject(options, operation);
        rejectUnknownOptions(options, new Set(["fatal"]), operation);
        if (options.fatal !== undefined && typeof options.fatal !== "boolean") {
            throw new TypeError(`${operation} fatal must be boolean.`);
        }
        return { fatal: options.fatal === true };
    }

    function parseDecodeChunkOptions(options) {
        options = validateOptionsObject(options, "Text.utf8.decoder.decode");
        rejectUnknownOptions(options, new Set(["stream"]), "Text.utf8.decoder.decode");
        if (options.stream !== undefined && typeof options.stream !== "boolean") {
            throw new TypeError("Text.utf8.decoder.decode stream must be boolean.");
        }
        return { stream: options.stream === true };
    }

    class Utf8StreamingDecoder {
        constructor(options = undefined) {
            this._fatal = parseUtf8Options(options, "Text.utf8.decoder").fatal;
            this._pending = new Uint8Array(0);
        }

        decode(chunk, options = undefined) {
            chunk = requireBytes(chunk, "Text.utf8.decoder.decode");
            const { stream } = parseDecodeChunkOptions(options);
            const input = new Uint8Array(this._pending.length + chunk.length);
            input.set(this._pending, 0);
            input.set(chunk, this._pending.length);
            const decoded = decodeUtf8Bytes(input, { fatal: this._fatal, stream });
            this._pending = decoded.remaining;
            return decoded.output;
        }

        finish() {
            const decoded = decodeUtf8Bytes(this._pending, { fatal: this._fatal, stream: false });
            this._pending = new Uint8Array(0);
            return decoded.output;
        }
    }

    const Base64 = Object.freeze({
        encode(bytes) {
            return encodeBase64(requireBytes(bytes, "Base64.encode"), BASE64_ALPHABET, true);
        },
        decode(text) {
            return decodeBase64(text, BASE64_MAP, "base64", "required");
        },
    });

    const Base64Url = Object.freeze({
        encode(bytes, options = undefined) {
            return encodeBase64(
                requireBytes(bytes, "Base64Url.encode"),
                BASE64URL_ALPHABET,
                parseBase64UrlEncodeOptions(options),
            );
        },
        decode(text, options = undefined) {
            return decodeBase64(text, BASE64URL_MAP, "base64url", parseBase64UrlDecodeOptions(options));
        },
    });

    const Hex = Object.freeze({
        encode(bytes) {
            bytes = requireBytes(bytes, "Hex.encode");
            let output = "";
            for (const byte of bytes) {
                output += HEX_ALPHABET[byte >>> 4] + HEX_ALPHABET[byte & 0x0f];
            }
            return output;
        },
        decode(text) {
            text = requireString(text, "Hex.decode");
            if (text.length % 2 !== 0) {
                throw codecError("SLOPPY_E_CODEC_INVALID_HEX", "Hex input must have an even digit count.");
            }
            const bytes = new Uint8Array(text.length / 2);
            for (let offset = 0; offset < text.length; offset += 2) {
                const hi = decodeHexNibble(text.charCodeAt(offset));
                const lo = decodeHexNibble(text.charCodeAt(offset + 1));
                if (hi < 0 || lo < 0) {
                    throw codecError("SLOPPY_E_CODEC_INVALID_HEX", "Hex input contains a non-hex digit.");
                }
                bytes[offset / 2] = (hi << 4) | lo;
            }
            return bytes;
        },
    });

    const Text = Object.freeze({
        utf8: Object.freeze({
            encode: encodeUtf8,
            decode(bytes, options = undefined) {
                bytes = requireBytes(bytes, "Text.utf8.decode");
                return decodeUtf8Bytes(bytes, { ...parseUtf8Options(options, "Text.utf8.decode"), stream: false })
                    .output;
            },
            decoder(options = undefined) {
                return new Utf8StreamingDecoder(options);
            },
        }),
    });

    const DEFAULT_BINARY_WRITER_CAPACITY = 64;
    const DEFAULT_BINARY_WRITER_MAX_CAPACITY = 64 * 1024 * 1024;
    const UINT64_MAX = (1n << 64n) - 1n;
    const INT64_MIN = -(1n << 63n);
    const INT64_MAX = (1n << 63n) - 1n;

    function binaryBoundsError(message) {
        return codecError("SLOPPY_E_CODEC_BINARY_READ_OUT_OF_BOUNDS", message);
    }

    function binaryFieldError(message) {
        return codecError("SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE", message);
    }

    function requireNonNegativeInteger(value, operation) {
        if (!Number.isSafeInteger(value) || value < 0) {
            throw binaryFieldError(`${operation} requires a non-negative safe integer.`);
        }
        return value;
    }

    function requireBinaryCapacity(value, operation) {
        value = requireNonNegativeInteger(value, operation);
        if (value > DEFAULT_BINARY_WRITER_MAX_CAPACITY) {
            throw binaryFieldError(`${operation} must not exceed the Binary.writer runtime maximum.`);
        }
        return value;
    }

    function requireIntegerInRange(value, min, max, operation) {
        if (!Number.isInteger(value) || value < min || value > max) {
            throw binaryFieldError(`${operation} value is outside the supported field range.`);
        }
        return value;
    }

    function requireBigIntInRange(value, min, max, operation) {
        if (typeof value !== "bigint" || value < min || value > max) {
            throw binaryFieldError(`${operation} value is outside the supported field range.`);
        }
        return value;
    }

    function readUnsignedNumber(bytes, offset, width, littleEndian) {
        let value = 0;
        for (let index = 0; index < width; index += 1) {
            const byte = littleEndian ? bytes[offset + index] : bytes[offset + width - 1 - index];
            value += byte * 2 ** (8 * index);
        }
        return value;
    }

    function readUnsignedBigInt(bytes, offset, width, littleEndian) {
        let value = 0n;
        if (littleEndian) {
            for (let index = width - 1; index >= 0; index -= 1) {
                value = (value << 8n) | BigInt(bytes[offset + index]);
            }
        } else {
            for (let index = 0; index < width; index += 1) {
                value = (value << 8n) | BigInt(bytes[offset + index]);
            }
        }
        return value;
    }

    function writeUnsignedNumber(bytes, offset, width, value, littleEndian) {
        for (let index = 0; index < width; index += 1) {
            const byte = Math.floor(value / 2 ** (8 * index)) & 0xff;
            bytes[offset + (littleEndian ? index : width - 1 - index)] = byte;
        }
    }

    function writeUnsignedBigInt(bytes, offset, width, value, littleEndian) {
        for (let index = 0; index < width; index += 1) {
            const byte = Number((value >> (8n * BigInt(index))) & 0xffn);
            bytes[offset + (littleEndian ? index : width - 1 - index)] = byte;
        }
    }

    class BinaryReader {
        #bytes;
        #offset = 0;

        constructor(bytes) {
            this.#bytes = new Uint8Array(requireBytes(bytes, "Binary.reader"));
        }

        position() {
            return this.#offset;
        }

        remaining() {
            return this.#bytes.length - this.#offset;
        }

        seek(position) {
            position = requireNonNegativeInteger(position, "BinaryReader.seek");
            if (position > this.#bytes.length) {
                throw binaryBoundsError("BinaryReader.seek moved beyond the input length.");
            }
            this.#offset = position;
            return this;
        }

        bytes(length) {
            length = requireNonNegativeInteger(length, "BinaryReader.bytes");
            const offset = this.#reserve(length, "BinaryReader.bytes");
            return this.#bytes.slice(offset, offset + length);
        }

        u8() {
            return this.#readNumber(1, false, false, "BinaryReader.u8");
        }

        i8() {
            return this.#readNumber(1, true, false, "BinaryReader.i8");
        }

        u16le() {
            return this.#readNumber(2, false, true, "BinaryReader.u16le");
        }

        u16be() {
            return this.#readNumber(2, false, false, "BinaryReader.u16be");
        }

        i16le() {
            return this.#readNumber(2, true, true, "BinaryReader.i16le");
        }

        i16be() {
            return this.#readNumber(2, true, false, "BinaryReader.i16be");
        }

        u32le() {
            return this.#readNumber(4, false, true, "BinaryReader.u32le");
        }

        u32be() {
            return this.#readNumber(4, false, false, "BinaryReader.u32be");
        }

        i32le() {
            return this.#readNumber(4, true, true, "BinaryReader.i32le");
        }

        i32be() {
            return this.#readNumber(4, true, false, "BinaryReader.i32be");
        }

        u64le() {
            return this.#readBigInt(false, true, "BinaryReader.u64le");
        }

        u64be() {
            return this.#readBigInt(false, false, "BinaryReader.u64be");
        }

        i64le() {
            return this.#readBigInt(true, true, "BinaryReader.i64le");
        }

        i64be() {
            return this.#readBigInt(true, false, "BinaryReader.i64be");
        }

        #reserve(length, operation) {
            if (length > this.remaining()) {
                throw binaryBoundsError(`${operation} requires ${length} byte(s), but only ${this.remaining()} remain.`);
            }
            const offset = this.#offset;
            this.#offset += length;
            return offset;
        }

        #readNumber(width, signed, littleEndian, operation) {
            const offset = this.#reserve(width, operation);
            const unsigned = readUnsignedNumber(this.#bytes, offset, width, littleEndian);
            if (!signed) {
                return unsigned;
            }
            const signBoundary = 2 ** (width * 8 - 1);
            const fullRange = 2 ** (width * 8);
            return unsigned >= signBoundary ? unsigned - fullRange : unsigned;
        }

        #readBigInt(signed, littleEndian, operation) {
            const offset = this.#reserve(8, operation);
            const unsigned = readUnsignedBigInt(this.#bytes, offset, 8, littleEndian);
            if (!signed) {
                return unsigned;
            }
            return unsigned > INT64_MAX ? unsigned - (1n << 64n) : unsigned;
        }
    }

    class BinaryWriter {
        #bytes;
        #length = 0;
        #maxCapacity;

        constructor(options = {}) {
            options = validateOptionsObject(options, "Binary.writer");
            rejectUnknownOptions(options, new Set(["initialCapacity", "maxCapacity"]), "Binary.writer");
            const initialCapacity =
                options.initialCapacity === undefined
                    ? DEFAULT_BINARY_WRITER_CAPACITY
                    : requireBinaryCapacity(options.initialCapacity, "Binary.writer initialCapacity");
            this.#maxCapacity =
                options.maxCapacity === undefined
                    ? DEFAULT_BINARY_WRITER_MAX_CAPACITY
                    : requireBinaryCapacity(options.maxCapacity, "Binary.writer maxCapacity");
            if (initialCapacity > this.#maxCapacity) {
                throw binaryFieldError("Binary.writer initialCapacity must not exceed maxCapacity.");
            }
            try {
                this.#bytes = new Uint8Array(initialCapacity);
            } catch {
                throw binaryFieldError("Binary.writer initialCapacity could not be allocated.");
            }
        }

        position() {
            return this.#length;
        }

        toBytes() {
            return this.#bytes.slice(0, this.#length);
        }

        bytes(bytes) {
            bytes = requireBytes(bytes, "BinaryWriter.bytes");
            const offset = this.#reserve(bytes.length, "BinaryWriter.bytes");
            this.#bytes.set(bytes, offset);
            return this;
        }

        u8(value) {
            return this.#writeNumber(1, requireIntegerInRange(value, 0, 0xff, "BinaryWriter.u8"), true, "BinaryWriter.u8");
        }

        i8(value) {
            return this.#writeNumber(1, requireIntegerInRange(value, -0x80, 0x7f, "BinaryWriter.i8"), true, "BinaryWriter.i8");
        }

        u16le(value) {
            return this.#writeNumber(2, requireIntegerInRange(value, 0, 0xffff, "BinaryWriter.u16le"), true, "BinaryWriter.u16le");
        }

        u16be(value) {
            return this.#writeNumber(2, requireIntegerInRange(value, 0, 0xffff, "BinaryWriter.u16be"), false, "BinaryWriter.u16be");
        }

        i16le(value) {
            return this.#writeNumber(2, requireIntegerInRange(value, -0x8000, 0x7fff, "BinaryWriter.i16le"), true, "BinaryWriter.i16le");
        }

        i16be(value) {
            return this.#writeNumber(2, requireIntegerInRange(value, -0x8000, 0x7fff, "BinaryWriter.i16be"), false, "BinaryWriter.i16be");
        }

        u32le(value) {
            return this.#writeNumber(4, requireIntegerInRange(value, 0, 0xffffffff, "BinaryWriter.u32le"), true, "BinaryWriter.u32le");
        }

        u32be(value) {
            return this.#writeNumber(4, requireIntegerInRange(value, 0, 0xffffffff, "BinaryWriter.u32be"), false, "BinaryWriter.u32be");
        }

        i32le(value) {
            return this.#writeNumber(4, requireIntegerInRange(value, -0x80000000, 0x7fffffff, "BinaryWriter.i32le"), true, "BinaryWriter.i32le");
        }

        i32be(value) {
            return this.#writeNumber(4, requireIntegerInRange(value, -0x80000000, 0x7fffffff, "BinaryWriter.i32be"), false, "BinaryWriter.i32be");
        }

        u64le(value) {
            return this.#writeBigInt(requireBigIntInRange(value, 0n, UINT64_MAX, "BinaryWriter.u64le"), true, "BinaryWriter.u64le");
        }

        u64be(value) {
            return this.#writeBigInt(requireBigIntInRange(value, 0n, UINT64_MAX, "BinaryWriter.u64be"), false, "BinaryWriter.u64be");
        }

        i64le(value) {
            value = requireBigIntInRange(value, INT64_MIN, INT64_MAX, "BinaryWriter.i64le");
            return this.#writeBigInt(value < 0n ? value + (1n << 64n) : value, true, "BinaryWriter.i64le");
        }

        i64be(value) {
            value = requireBigIntInRange(value, INT64_MIN, INT64_MAX, "BinaryWriter.i64be");
            return this.#writeBigInt(value < 0n ? value + (1n << 64n) : value, false, "BinaryWriter.i64be");
        }

        #reserve(length, operation) {
            if (length > this.#maxCapacity - this.#length) {
                throw binaryFieldError(`${operation} would exceed Binary.writer maxCapacity.`);
            }
            const offset = this.#length;
            const required = offset + length;
            this.#ensureCapacity(required, operation);
            this.#length = required;
            return offset;
        }

        #ensureCapacity(required, operation) {
            if (required <= this.#bytes.length) {
                return;
            }
            let next = Math.max(1, this.#bytes.length);
            while (next < required) {
                next *= 2;
                if (next > this.#maxCapacity) {
                    next = this.#maxCapacity;
                    break;
                }
            }
            let grown;
            try {
                grown = new Uint8Array(next);
            } catch {
                throw binaryFieldError(`${operation} could not grow Binary.writer capacity.`);
            }
            grown.set(this.#bytes.subarray(0, this.#length));
            this.#bytes = grown;
        }

        #writeNumber(width, value, littleEndian, operation) {
            const bits = width * 8;
            const unsigned = value < 0 ? value + 2 ** bits : value;
            const offset = this.#reserve(width, operation);
            writeUnsignedNumber(this.#bytes, offset, width, unsigned, littleEndian);
            return this;
        }

        #writeBigInt(value, littleEndian, operation) {
            const offset = this.#reserve(8, operation);
            writeUnsignedBigInt(this.#bytes, offset, 8, value, littleEndian);
            return this;
        }
    }

    const Binary = Object.freeze({
        reader(bytes) {
            return new BinaryReader(bytes);
        },
        writer(options) {
            return new BinaryWriter(options);
        },
    });

    const DEFAULT_COMPRESSION_LEVEL = 6;
    const MAX_COMPRESSION_INPUT_BYTES = 1024 * 1024;
    const DEFAULT_DECOMPRESSION_MAX_OUTPUT_BYTES = 64 * 1024 * 1024;

    function compressionBackendUnavailable(operation) {
        throw codecError(
            "SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE",
            `${operation} requires the zlib-backed __sloppy.codec V8 bridge.`,
        );
    }

    function sloppyNativeCodec(operation) {
        const bridge = globalThis.__sloppy?.codec ?? null;
        if (bridge === null) {
            compressionBackendUnavailable(operation);
        }
        return bridge;
    }

    function sloppyNativeCompressionFunction(operation, name) {
        const method = sloppyNativeCodec(operation)[name];
        if (typeof method !== "function") {
            compressionBackendUnavailable(operation);
        }
        return method;
    }

    function requireCompressionLevel(value, operation) {
        if (!Number.isInteger(value) || value < 0 || value > 9) {
            throw new TypeError(`${operation} level must be an integer from 0 to 9.`);
        }
        return value;
    }

    function requireCompressionLimit(value, operation, maximum) {
        if (!Number.isSafeInteger(value) || value < 0 || value > maximum) {
            throw new TypeError(`${operation} must be a non-negative safe integer no greater than ${maximum}.`);
        }
        return value;
    }

    function parseGzipOptions(options, operation) {
        options = validateOptionsObject(options, operation);
        rejectUnknownOptions(options, new Set(["level"]), operation);
        return {
            level: options.level === undefined ? DEFAULT_COMPRESSION_LEVEL : requireCompressionLevel(options.level, operation),
        };
    }

    function parseGunzipOptions(options, operation) {
        options = validateOptionsObject(options, operation);
        rejectUnknownOptions(options, new Set(["maxOutputBytes"]), operation);
        return {
            maxOutputBytes:
                options.maxOutputBytes === undefined
                    ? DEFAULT_DECOMPRESSION_MAX_OUTPUT_BYTES
                    : requireCompressionLimit(options.maxOutputBytes, `${operation} maxOutputBytes`, DEFAULT_DECOMPRESSION_MAX_OUTPUT_BYTES),
        };
    }

    function parseCompressionStreamOptions(options, operation, allowedCodecOptions) {
        options = validateOptionsObject(options, operation);
        rejectUnknownOptions(options, new Set([...allowedCodecOptions, "signal", "deadline", "maxInputBytes"]), operation);
        const maxInputBytes =
            options.maxInputBytes === undefined
                ? MAX_COMPRESSION_INPUT_BYTES
                : requireCompressionLimit(options.maxInputBytes, `${operation} maxInputBytes`, MAX_COMPRESSION_INPUT_BYTES);
        const codecOptions = {};
        for (const key of allowedCodecOptions) {
            if (Object.prototype.hasOwnProperty.call(options, key)) {
                if (key === "level") {
                    codecOptions.level = requireCompressionLevel(options.level, operation);
                } else if (key === "maxOutputBytes") {
                    codecOptions.maxOutputBytes = requireCompressionLimit(
                        options.maxOutputBytes,
                        `${operation} maxOutputBytes`,
                        DEFAULT_DECOMPRESSION_MAX_OUTPUT_BYTES,
                    );
                }
            }
        }
        return {
            codecOptions,
            signal: options.signal,
            deadline: options.deadline,
            maxInputBytes,
        };
    }

    function normalizeCompressionResult(value, operation) {
        if (!(value instanceof Uint8Array)) {
            throw new TypeError(`${operation} native backend must return Uint8Array bytes.`);
        }
        return value.slice();
    }

    function codecIsCancellationSignal(value) {
        return (
            value !== null &&
            typeof value === "object" &&
            typeof value.aborted === "boolean" &&
            ("reason" in value || typeof value.addEventListener === "function")
        );
    }

    function codecSubscribeCancellation(signal, listener) {
        if (!codecIsCancellationSignal(signal)) {
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

    function compressionCancelledError(reason = undefined) {
        return cancelledError(reason);
    }

    function compressionTimeoutError(reason = undefined) {
        return timeoutError(reason);
    }

    function codecDeadlineRemainingMs(deadline, operation) {
        if (deadline === undefined || deadline === null) {
            return Infinity;
        }
        if (typeof deadline.remainingMs !== "function") {
            throw new TypeError(`${operation} deadline must come from Deadline.after, Deadline.at, or Deadline.never.`);
        }
        return deadline.remainingMs();
    }

    function checkCompressionTerminalOptions(options, operation) {
        if (codecIsCancellationSignal(options.signal) && options.signal.aborted) {
            throw compressionCancelledError(options.signal.reason);
        }
        if (codecDeadlineRemainingMs(options.deadline, operation) <= 0) {
            throw compressionTimeoutError(options.deadline);
        }
    }

    function raceCompressionTerminal(promise, options, operation) {
        checkCompressionTerminalOptions(options, operation);
        const signal = options.signal;
        const remainingMs = codecDeadlineRemainingMs(options.deadline, operation);
        if (!codecIsCancellationSignal(signal) && remainingMs === Infinity) {
            return promise;
        }
        return new Promise((resolve, reject) => {
            let finished = false;
            let timeoutId;
            let cleanupSignal = () => {};
            const finish = (callback, value) => {
                if (finished) {
                    return;
                }
                finished = true;
                cleanupAll();
                callback(value);
            };
            cleanupSignal = codecSubscribeCancellation(signal, (reason) => {
                finish(reject, compressionCancelledError(reason));
            });
            if (remainingMs !== Infinity) {
                const setTimer = globalThis["setTimeout"];
                const clearTimer = globalThis["clearTimeout"];
                if (typeof setTimer !== "function" || typeof clearTimer !== "function") {
                    finish(reject, compressionTimeoutError(options.deadline));
                    return;
                }
                timeoutId = setTimer(
                    () => finish(reject, compressionTimeoutError(options.deadline)),
                    Math.min(Math.ceil(remainingMs), 0x7fffffff),
                );
            }
            promise.then(
                (value) => {
                    finish(resolve, value);
                },
                (error) => {
                    finish(reject, error);
                },
            );
            function cleanupAll() {
                cleanupSignal();
                if (timeoutId !== undefined) {
                    const clearTimer = globalThis["clearTimeout"];
                    clearTimer(timeoutId);
                }
            }
        });
    }

    function runCompression(operation, bytes, options, invoke) {
        try {
            bytes = requireBytes(bytes, operation);
            checkCompressionTerminalOptions({ signal: options?.signal, deadline: options?.deadline }, operation);
            if (bytes.byteLength > MAX_COMPRESSION_INPUT_BYTES) {
                throw new TypeError(`${operation} input exceeds the ${MAX_COMPRESSION_INPUT_BYTES} byte inline compression limit.`);
            }
            const promise = Promise.resolve(invoke(new Uint8Array(bytes))).then((result) =>
                normalizeCompressionResult(result, operation),
            );
            return raceCompressionTerminal(promise, options ?? {}, operation);
        } catch (error) {
            return Promise.reject(error);
        }
    }

    function codecIsIterable(value) {
        return value !== null && typeof value === "object" && (Symbol.asyncIterator in value || Symbol.iterator in value);
    }

    async function* compressionStream(input, options, operation, parseOptions, invoke) {
        const parsed = parseOptions(options, operation);
        checkCompressionTerminalOptions(parsed, operation);
        let total = 0;
        const chunks = [];
        let terminal = false;
        const iterator =
            typeof input[Symbol.asyncIterator] === "function"
                ? input[Symbol.asyncIterator]()
                : input[Symbol.iterator]();
        try {
            while (true) {
                checkCompressionTerminalOptions(parsed, operation);
                const next = await raceCompressionTerminal(Promise.resolve(iterator.next()), parsed, operation);
                if (next.done === true) {
                    break;
                }
                const chunk = next.value;
                const bytes = requireBytes(chunk, operation);
                if (bytes.byteLength > parsed.maxInputBytes - total) {
                    throw new TypeError(`${operation} buffered input exceeds maxInputBytes.`);
                }
                chunks.push(new Uint8Array(bytes));
                total += bytes.byteLength;
            }
            checkCompressionTerminalOptions(parsed, operation);
            const inputBytes = new Uint8Array(total);
            let offset = 0;
            for (const chunk of chunks) {
                inputBytes.set(chunk, offset);
                offset += chunk.byteLength;
            }
            const output = await raceCompressionTerminal(Promise.resolve(invoke(inputBytes, parsed.codecOptions)), parsed, operation);
            checkCompressionTerminalOptions(parsed, operation);
            terminal = true;
            yield output;
        } finally {
            if (!terminal && typeof iterator.return === "function") {
                try {
                    await raceCompressionTerminal(Promise.resolve(iterator.return()), parsed, operation);
                } catch {
                }
            }
            chunks.length = 0;
            if (!terminal) {
                total = 0;
            }
        }
    }

    const Compression = Object.freeze({
        gzip(bytes, options = undefined) {
            try {
                const parsed = parseGzipOptions(options, "Compression.gzip");
                return runCompression("Compression.gzip", bytes, {}, (input) =>
                    sloppyNativeCompressionFunction("Compression.gzip", "gzip").call(undefined, input, parsed.level),
                );
            } catch (error) {
                return Promise.reject(error);
            }
        },
        gunzip(bytes, options = undefined) {
            try {
                const parsed = parseGunzipOptions(options, "Compression.gunzip");
                return runCompression("Compression.gunzip", bytes, {}, (input) =>
                    sloppyNativeCompressionFunction("Compression.gunzip", "gunzip").call(undefined, input, parsed.maxOutputBytes),
                );
            } catch (error) {
                return Promise.reject(error);
            }
        },
        gzipStream(input, options = undefined) {
            if (!codecIsIterable(input)) {
                throw new TypeError("Compression.gzipStream input must be an iterable or async iterable of Uint8Array chunks.");
            }
            return compressionStream(
                input,
                options,
                "Compression.gzipStream",
                (streamOptions, operation) => parseCompressionStreamOptions(streamOptions, operation, ["level"]),
                (bytes, codecOptions) => Compression.gzip(bytes, codecOptions),
            );
        },
        gunzipStream(input, options = undefined) {
            if (!codecIsIterable(input)) {
                throw new TypeError("Compression.gunzipStream input must be an iterable or async iterable of Uint8Array chunks.");
            }
            return compressionStream(
                input,
                options,
                "Compression.gunzipStream",
                (streamOptions, operation) => parseCompressionStreamOptions(streamOptions, operation, ["maxOutputBytes"]),
                (bytes, codecOptions) => Compression.gunzip(bytes, codecOptions),
            );
        },
    });

    function makeCrc32Table() {
        const table = new Uint32Array(256);
        for (let index = 0; index < table.length; index += 1) {
            let value = index;
            for (let bit = 0; bit < 8; bit += 1) {
                value = (value >>> 1) ^ (value & 1 ? CRC32_POLYNOMIAL_REFLECTED : 0);
            }
            table[index] = value >>> 0;
        }
        return table;
    }

    // CRC-32/ISO-HDLC uses the reflected IEEE 802.3 polynomial, all-ones init, and final xor.
    const CRC32_POLYNOMIAL_REFLECTED = 0xedb88320;
    const CRC32_INITIAL = 0xffffffff;
    const CRC32_FINAL_XOR = 0xffffffff;
    const CRC32_TABLE = makeCrc32Table();
    const CHECKSUM_UNSUPPORTED_ALGORITHM_DIAGNOSTIC = "SLOPPY_E_CODEC_CHECKSUM_UNSUPPORTED_ALGORITHM";

    function crc32(bytes) {
        bytes = requireBytes(bytes, "Checksums.crc32");
        let crc = CRC32_INITIAL;
        for (let index = 0; index < bytes.byteLength; index += 1) {
            crc = (crc >>> 8) ^ CRC32_TABLE[(crc ^ bytes[index]) & 0xff];
        }
        return (crc ^ CRC32_FINAL_XOR) >>> 0;
    }

    const Checksums = Object.freeze({
        crc32,
    });

    class NetworkAddress {
        constructor(host, port) {
            if (typeof host !== "string" || host.length === 0) {
                throw new TypeError("NetworkAddress host must be a non-empty string.");
            }
            if (!Number.isInteger(port) || port < 0 || port > 65535) {
                throw new TypeError("NetworkAddress port must be an integer from 0 to 65535.");
            }
            this.host = host;
            this.port = port;
            Object.freeze(this);
        }

        static parse(value) {
            if (value instanceof NetworkAddress) {
                return value;
            }
            if (typeof value === "string") {
                if (value.startsWith("[")) {
                    const end = value.indexOf("]");
                    if (end < 0 || value[end + 1] !== ":") {
                        throw new TypeError("NetworkAddress IPv6 text must be [host]:port.");
                    }
                    return new NetworkAddress(
                        value.slice(1, end),
                        sloppyNetPortText(value.slice(end + 2)),
                    );
                }
                const firstColon = value.indexOf(":");
                const lastColon = value.lastIndexOf(":");
                if (firstColon <= 0 || firstColon !== lastColon || lastColon === value.length - 1) {
                    throw new TypeError("NetworkAddress text must be host:port or [ipv6]:port.");
                }
                return new NetworkAddress(
                    value.slice(0, lastColon),
                    sloppyNetPortText(value.slice(lastColon + 1)),
                );
            }
            if (value === null || typeof value !== "object") {
                throw new TypeError("NetworkAddress.parse requires an address object.");
            }
            return new NetworkAddress(value.host, value.port);
        }

        toString() {
            return this.host.includes(":")
                ? `[${this.host}]:${this.port}`
                : `${this.host}:${this.port}`;
        }
    }

    function sloppyNativeNet(operation) {
        const bridge = globalThis.__sloppy?.net;
        if (bridge === undefined) {
            throw new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature stdlib.net is inactive or unavailable

Feature:
  stdlib.net

Operation:
  ${operation}

Reason:
  The active Sloppy Plan did not enable the __sloppy.net V8 intrinsic namespace.`);
        }
        return bridge;
    }

    function sloppyNetPort(port, allowZero) {
        const minimum = allowZero ? 0 : 1;
        if (!Number.isInteger(port) || port < minimum || port > 65535) {
            throw new TypeError(`TCP port must be an integer from ${minimum} to 65535.`);
        }
        return port;
    }

    function sloppyNetPortText(text) {
        if (typeof text !== "string" || text.length === 0 || !/^[0-9]+$/.test(text)) {
            throw new TypeError("TCP port text must contain decimal digits.");
        }
        return sloppyNetPort(Number(text), true);
    }

    function sloppyNetConnectOptions(options) {
        if (!isPlainObject(options)) {
            throw new TypeError("TcpClient.connect options must be a plain object.");
        }
        if (typeof options.host !== "string" || options.host.length === 0) {
            throw new TypeError("TcpClient.connect host must be a non-empty string.");
        }
        const normalized = {
            host: options.host,
            port: sloppyNetPort(options.port, false),
            noDelay: options.noDelay === true,
        };
        if (options.timeoutMs !== undefined) {
            if (!Number.isFinite(options.timeoutMs) || options.timeoutMs < 0) {
                throw new TypeError("TcpClient.connect timeoutMs must be a non-negative number.");
            }
            normalized.timeoutMs = Math.ceil(options.timeoutMs);
        }
        if (options.keepAlive !== undefined) {
            if (!isPlainObject(options.keepAlive) || typeof options.keepAlive.enabled !== "boolean") {
                throw new TypeError("TcpClient.connect keepAlive must be { enabled, delayMs? }.");
            }
            normalized.keepAlive = { enabled: options.keepAlive.enabled };
            if (options.keepAlive.delayMs !== undefined) {
                if (!Number.isFinite(options.keepAlive.delayMs) || options.keepAlive.delayMs < 0) {
                    throw new TypeError("TcpClient.connect keepAlive.delayMs must be non-negative.");
                }
                normalized.keepAlive.delayMs = Math.ceil(options.keepAlive.delayMs);
            }
        }
        return normalized;
    }

    function sloppyNetListenOptions(options) {
        if (!isPlainObject(options)) {
            throw new TypeError("TcpListener.listen options must be a plain object.");
        }
        if (typeof options.host !== "string" || options.host.length === 0) {
            throw new TypeError("TcpListener.listen host must be a non-empty string.");
        }
        const normalized = {
            host: options.host,
            port: sloppyNetPort(options.port, true),
        };
        if (options.backlog !== undefined) {
            if (!Number.isInteger(options.backlog) || options.backlog < 1) {
                throw new TypeError("TcpListener.listen backlog must be a positive integer.");
            }
            normalized.backlog = options.backlog;
        }
        return normalized;
    }

    function sloppyNetTimeoutOption(options, operation) {
        if (options === undefined) {
            return undefined;
        }
        if (!isPlainObject(options)) {
            throw new TypeError(`${operation} options must be a plain object.`);
        }
        if (options.timeoutMs === undefined) {
            return undefined;
        }
        if (!Number.isFinite(options.timeoutMs) || options.timeoutMs < 0) {
            throw new TypeError(`${operation} timeoutMs must be a non-negative number.`);
        }
        return Math.ceil(options.timeoutMs);
    }

    class TcpConnection {
        constructor(bridge, handle) {
            this._bridge = bridge;
            this._handle = handle;
            this._closed = false;
        }

        async write(bytes) {
            if (this._closed) {
                throw new Error("SLOPPY_E_NET_CONNECTION_CLOSED: TCP connection is closed");
            }
            if (!(bytes instanceof Uint8Array)) {
                throw new TypeError("TcpConnection.write requires a Uint8Array.");
            }
            await this._bridge.write(this._handle, bytes);
        }

        async writeText(text) {
            if (typeof text !== "string") {
                throw new TypeError("TcpConnection.writeText requires a string.");
            }
            await this.write(sloppyUtf8ToBytes(text));
        }

        read(options = undefined) {
            return this._bridge.read(this._handle, options?.maxBytes ?? 8192);
        }

        readUntil(delimiter, options = undefined) {
            const delimiterBytes =
                typeof delimiter === "string" ? sloppyUtf8ToBytes(delimiter) : delimiter;
            if (!(delimiterBytes instanceof Uint8Array) || delimiterBytes.byteLength === 0) {
                throw new TypeError("TcpConnection.readUntil delimiter must be non-empty bytes.");
            }
            return this._bridge.readUntil(this._handle, delimiterBytes, options?.maxBytes ?? 8192);
        }

        readLine(options = undefined) {
            return this._bridge.readLine(this._handle, options?.maxBytes ?? 8192);
        }

        async close() {
            if (this._closed) {
                return;
            }
            this._closed = true;
            await this._bridge.close(this._handle);
        }

        async abort() {
            if (this._closed) {
                return;
            }
            this._closed = true;
            await this._bridge.abort(this._handle);
        }
    }

    const TcpClient = Object.freeze({
        async connect(options) {
            const bridge = sloppyNativeNet("connect");
            const handle = await bridge.connect(sloppyNetConnectOptions(options));
            return new TcpConnection(bridge, handle);
        },
    });

    class TcpListenerResource {
        constructor(bridge, handle) {
            this._bridge = bridge;
            this._handle = handle;
            this._closed = false;
        }

        get closed() {
            return this._closed;
        }

        async accept(options = undefined) {
            if (this._closed) {
                throw new Error("SLOPPY_E_NET_CONNECTION_CLOSED: TCP listener is closed");
            }
            const timeoutMs = sloppyNetTimeoutOption(options, "TcpListener.accept");
            const handle = await this._bridge.accept(this._handle, timeoutMs);
            return new TcpConnection(this._bridge, handle);
        }

        async *[Symbol.asyncIterator]() {
            while (!this._closed) {
                yield await this.accept();
            }
        }

        acceptLoop(options = undefined) {
            const listener = this;
            return {
                async *[Symbol.asyncIterator]() {
                    while (!listener.closed) {
                        yield await listener.accept(options);
                    }
                },
            };
        }

        async close() {
            if (this._closed) {
                return;
            }
            this._closed = true;
            await this._bridge.closeListener(this._handle);
        }

        async abort() {
            if (this._closed) {
                return;
            }
            this._closed = true;
            await this._bridge.abortListener(this._handle);
        }
    }

    const TcpListener = Object.freeze({
        async listen(options) {
            const bridge = sloppyNativeNet("listen");
            const handle = await bridge.listen(sloppyNetListenOptions(options));
            return new TcpListenerResource(bridge, handle);
        },
    });

    const HTTP_CLIENT_DEFAULT_MAX_HEADER_BYTES = 16 * 1024;
    const HTTP_CLIENT_DEFAULT_MAX_RESPONSE_BYTES = 1024 * 1024;
    const HTTP_CLIENT_DEFAULT_MAX_REQUEST_BYTES = 1024 * 1024;
    const HTTP_CLIENT_DEFAULT_MAX_REDIRECTS = 5;
    const HTTP_CLIENT_DEFAULT_POOL_IDLE_TIMEOUT_MS = 30000;
    const HTTP_CLIENT_DEFAULT_MAX_CONNECTIONS_PER_ORIGIN = 8;
    const HTTP_CLIENT_PROTOCOLS = new Set(["auto", "http/1.1", "h2", "h2c"]);
    const HTTP_CLIENT_TLS_OPTION_KEYS = new Set([
        "caPath",
        "caBundlePath",
        "trustStorePath",
        "clientCertificatePath",
        "clientPrivateKeyPath",
        "clientPrivateKeyPassphrase",
        "insecureSkipVerify",
    ]);
    const HTTP_CLIENT_TLS_STRING_OPTION_KEYS = new Set([
        "caPath",
        "caBundlePath",
        "trustStorePath",
        "clientCertificatePath",
        "clientPrivateKeyPath",
        "clientPrivateKeyPassphrase",
    ]);
    const HTTP_CLIENT_SENSITIVE_HEADERS = new Set([
        "authorization",
        "cookie",
        "proxy-authorization",
        "x-api-key",
        "api-key",
    ]);
    const HTTP_CLIENT_SIZE_UNITS = Object.freeze({
        b: 1,
        kb: 1024,
        mb: 1024 * 1024,
        gb: 1024 * 1024 * 1024,
    });

    function httpClientError(code, message, options = undefined) {
        return new Error(`${code}: ${message}`, options);
    }

    function httpClientUnavailable(operation) {
        return Promise.reject(
            httpClientError(
                "SLOPPY_E_HTTP_CLIENT_FEATURE_UNAVAILABLE",
                `HttpClient.${operation} requires the outbound HTTP client transport lane.`,
            ),
        );
    }

    function parseHttpSize(value, operation) {
        if (value === undefined) {
            return undefined;
        }
        if (Number.isInteger(value) && value >= 0) {
            return value;
        }
        if (typeof value === "string") {
            const match = /^([0-9]+)\s*(b|kb|mb|gb)$/i.exec(value.trim());
            if (match !== null) {
                const size = Number(match[1]) * HTTP_CLIENT_SIZE_UNITS[match[2].toLowerCase()];
                if (Number.isSafeInteger(size)) {
                    return size;
                }
            }
        }
        throw httpClientError(
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} size option must be a non-negative integer byte count or size string like "4mb".`,
        );
    }

    function hasHttpControlChars(value) {
        for (let index = 0; index < value.length; index += 1) {
            const code = value.charCodeAt(index);
            if (code < 0x20 || code === 0x7f) {
                return true;
            }
        }
        return false;
    }

    function setHttpDefaultHeader(headers, name, value) {
        const normalizedName = name.toLowerCase();
        if (!headers.has(normalizedName)) {
            headers.set(normalizedName, { name, value });
        }
    }

    function normalizeHttpHostHeaderForOrigin(hostHeader) {
        if (hostHeader.startsWith("[")) {
            const close = hostHeader.indexOf("]");
            if (close > 0) {
                return `[${hostHeader.slice(1, close).toLowerCase()}]${hostHeader.slice(close + 1)}`;
            }
        }
        const colon = hostHeader.lastIndexOf(":");
        if (colon > 0) {
            return `${hostHeader.slice(0, colon).toLowerCase()}${hostHeader.slice(colon)}`;
        }
        return hostHeader.toLowerCase();
    }

    function httpOriginKey(url) {
        return `${url.scheme}://${normalizeHttpHostHeaderForOrigin(url.hostHeader)}`;
    }

    function httpUrlToString(url) {
        return `${url.scheme}://${url.hostHeader}${url.target}`;
    }

    function cloneHttpHeaders(headers) {
        const cloned = new Map();
        for (const [name, entry] of headers.entries()) {
            cloned.set(name, { name: entry.name, value: entry.value });
        }
        return cloned;
    }

    function parseHttpSecretHeaders(value, operation) {
        if (value === undefined) {
            return [];
        }
        if (!Array.isArray(value)) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} secretHeaders must be an array of header names.`);
        }
        return value.map((name) => {
            if (typeof name !== "string" || !/^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/.test(name)) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    `${operation} secretHeaders entries must be valid header names.`,
                );
            }
            return name.toLowerCase();
        });
    }

    function isHttpSensitiveHeader(name, value, secretHeaders) {
        const normalized = name.toLowerCase();
        return (
            HTTP_CLIENT_SENSITIVE_HEADERS.has(normalized) ||
            secretHeaders.has(normalized) ||
            /token|secret|api[-_]?key/i.test(name) ||
            /bearer\s+/i.test(value)
        );
    }

    function stripHttpSensitiveHeaders(headers, policy) {
        const stripped = [];
        for (const [name, entry] of Array.from(headers.entries())) {
            if (isHttpSensitiveHeader(name, entry.value, policy.secretHeaders)) {
                stripped.push(entry.name);
                headers.delete(name);
            }
        }
        if (stripped.length > 0 && policy.crossOriginSensitiveHeaders === "deny") {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_SENSITIVE_HEADER_STRIPPED",
                "HTTP client cross-origin redirect refused sensitive headers.",
            );
        }
        return stripped;
    }

    function normalizeHttpRedirectPolicy(baseOptions, requestObject, operation) {
        const raw = requestObject.redirects ?? baseOptions?.redirects;
        let enabled = true;
        let max = HTTP_CLIENT_DEFAULT_MAX_REDIRECTS;
        let allowPost = false;
        let crossOriginSensitiveHeaders = "strip";
        const secretHeaders = [
            ...parseHttpSecretHeaders(baseOptions?.secretHeaders, operation),
            ...parseHttpSecretHeaders(requestObject.secretHeaders, operation),
        ];
        if (raw === false) {
            enabled = false;
        } else if (raw === true || raw === undefined) {
            enabled = true;
        } else if (isPlainObject(raw)) {
            if (raw.enabled !== undefined) {
                if (typeof raw.enabled !== "boolean") {
                    throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} redirects.enabled must be a boolean.`);
                }
                enabled = raw.enabled;
            }
            if (raw.max !== undefined) {
                if (!Number.isInteger(raw.max) || raw.max < 0 || raw.max > 20) {
                    throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} redirects.max must be an integer from 0 to 20.`);
                }
                max = raw.max;
            }
            if (raw.allowPost !== undefined) {
                if (typeof raw.allowPost !== "boolean") {
                    throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} redirects.allowPost must be a boolean.`);
                }
                allowPost = raw.allowPost;
            }
            if (raw.crossOriginSensitiveHeaders !== undefined) {
                if (raw.crossOriginSensitiveHeaders !== "strip" && raw.crossOriginSensitiveHeaders !== "deny") {
                    throw httpClientError(
                        "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                        `${operation} redirects.crossOriginSensitiveHeaders must be "strip" or "deny".`,
                    );
                }
                crossOriginSensitiveHeaders = raw.crossOriginSensitiveHeaders;
            }
            secretHeaders.push(...parseHttpSecretHeaders(raw.secretHeaders, operation));
        } else {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} redirects must be a boolean or policy object.`);
        }
        return Object.freeze({
            enabled,
            max,
            allowPost,
            crossOriginSensitiveHeaders,
            secretHeaders: new Set(secretHeaders),
        });
    }

    function normalizeHttpPoolOptions(value, operation) {
        if (value === undefined || value === null || value === false) {
            return undefined;
        }
        if (value !== true && !isPlainObject(value)) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} pool must be a policy object.`);
        }
        const raw = value === true ? {} : value;
        const maxConnectionsPerOrigin = raw.maxConnectionsPerOrigin ?? HTTP_CLIENT_DEFAULT_MAX_CONNECTIONS_PER_ORIGIN;
        const idleTimeoutMs = raw.idleTimeoutMs ?? HTTP_CLIENT_DEFAULT_POOL_IDLE_TIMEOUT_MS;
        if (!Number.isInteger(maxConnectionsPerOrigin) || maxConnectionsPerOrigin < 1 || maxConnectionsPerOrigin > 256) {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} pool.maxConnectionsPerOrigin must be an integer from 1 to 256.`,
            );
        }
        if (!Number.isInteger(idleTimeoutMs) || idleTimeoutMs < 0) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} pool.idleTimeoutMs must be a non-negative integer.`);
        }
        return Object.freeze({ maxConnectionsPerOrigin, idleTimeoutMs });
    }

    function normalizeHttpTlsOptions(value, operation) {
        if (value === undefined) {
            return Object.freeze({});
        }
        if (!isPlainObject(value)) {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} tls must be a plain object when provided.`,
            );
        }
        for (const key of Object.keys(value)) {
            if (!HTTP_CLIENT_TLS_OPTION_KEYS.has(key)) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    `${operation} tls option ${key} is not supported.`,
                );
            }
            if (HTTP_CLIENT_TLS_STRING_OPTION_KEYS.has(key) && typeof value[key] !== "string") {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    `${operation} tls option ${key} must be a string.`,
                );
            }
            if (key === "insecureSkipVerify" && typeof value[key] !== "boolean") {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    `${operation} tls option insecureSkipVerify must be a boolean.`,
                );
            }
        }
        return Object.freeze({ ...value });
    }

    function hasHttpTlsOption(tls, key) {
        return Object.prototype.hasOwnProperty.call(tls, key);
    }

    function hasHttpTlsOptions(tls) {
        return tls !== undefined && tls !== null && Object.keys(tls).length > 0;
    }

    function assertHttpTlsAllowedForScheme(url, tls, operation) {
        if (url.scheme === "https" || !hasHttpTlsOptions(tls)) {
            return;
        }
        throw httpClientError(
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} tls options are only valid for https:// URLs.`,
        );
    }

    function sanitizeHttpTlsDescriptor(tls) {
        if (!hasHttpTlsOptions(tls)) {
            return Object.freeze({ enabled: false });
        }
        return Object.freeze({
            enabled: true,
            hasCaPath: hasHttpTlsOption(tls, "caPath"),
            hasCaBundlePath: hasHttpTlsOption(tls, "caBundlePath"),
            hasTrustStorePath: hasHttpTlsOption(tls, "trustStorePath"),
            hasClientCertificate:
                hasHttpTlsOption(tls, "clientCertificatePath") ||
                hasHttpTlsOption(tls, "clientPrivateKeyPath"),
            hasClientPrivateKeyPassphrase: hasHttpTlsOption(tls, "clientPrivateKeyPassphrase"),
            insecureSkipVerify: tls.insecureSkipVerify === true,
        });
    }

    function sanitizeHttpClientOptionsDescriptor(baseOptions, normalizedTls, poolOptions) {
        if (baseOptions === undefined) {
            return undefined;
        }
        const descriptor = { ...baseOptions };
        if (Object.prototype.hasOwnProperty.call(baseOptions, "tls")) {
            descriptor.tls = sanitizeHttpTlsDescriptor(normalizedTls);
        } else {
            delete descriptor.tls;
        }
        if (poolOptions !== undefined) {
            descriptor.pool = poolOptions;
        }
        return Object.freeze(descriptor);
    }

    function createHttpClientBaseOptions(baseOptions, normalizedTls, poolOptions) {
        if (baseOptions === undefined) {
            return undefined;
        }
        const internal = { ...baseOptions, tls: normalizedTls };
        if (poolOptions !== undefined) {
            internal.pool = poolOptions;
        }
        return Object.freeze(internal);
    }

    function assertHttpTlsBridgeCapability(bridge, tls, key, capability, operation) {
        if (!hasHttpTlsOption(tls, key) || bridge[capability] === true) {
            return;
        }
        throw httpClientError(
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} tls option ${key} is not supported by the active TLS bridge.`,
        );
    }

    function assertHttpTlsBridgeCapabilities(bridge, tls, operation) {
        if (!hasHttpTlsOptions(tls)) {
            return;
        }
        assertHttpTlsBridgeCapability(bridge, tls, "caPath", "tlsCaPath", operation);
        assertHttpTlsBridgeCapability(bridge, tls, "caBundlePath", "tlsCaBundlePath", operation);
        assertHttpTlsBridgeCapability(bridge, tls, "trustStorePath", "tlsTrustStorePath", operation);
        assertHttpTlsBridgeCapability(
            bridge,
            tls,
            "clientCertificatePath",
            "tlsClientCertificate",
            operation,
        );
        assertHttpTlsBridgeCapability(
            bridge,
            tls,
            "clientPrivateKeyPath",
            "tlsClientCertificate",
            operation,
        );
        assertHttpTlsBridgeCapability(
            bridge,
            tls,
            "clientPrivateKeyPassphrase",
            "tlsClientCertificate",
            operation,
        );
        assertHttpTlsBridgeCapability(
            bridge,
            tls,
            "insecureSkipVerify",
            "tlsInsecureSkipVerify",
            operation,
        );
    }

    function normalizeHttpProtocol(baseOptions, requestObject, operation) {
        const raw = requestObject.protocol ?? baseOptions?.protocol ?? "auto";
        if (typeof raw === "string" && HTTP_CLIENT_PROTOCOLS.has(raw)) {
            return raw;
        }
        throw httpClientError(
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} protocol must be "auto", "http/1.1", "h2", or "h2c".`,
        );
    }
    function normalizeHttpNetworkPolicy(baseOptions, requestObject, operation) {
        const raw = requestObject.network ?? baseOptions?.network;
        if (raw === undefined || raw === null || raw === false) {
            return Object.freeze({ strict: false, allowedOrigins: new Set() });
        }
        if (!isPlainObject(raw)) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} network must be a policy object.`);
        }
        const strict = raw.strict === true;
        const allowed = raw.allow ?? raw.allowedOrigins ?? [];
        if (!Array.isArray(allowed)) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} network.allow must be an array of origins.`);
        }
        const allowedOrigins = new Set();
        for (const origin of allowed) {
            if (origin === "*") {
                allowedOrigins.add("*");
                continue;
            }
            const parsed = parseAbsoluteHttpUrl(origin, operation);
            allowedOrigins.add(httpOriginKey(parsed));
        }
        return Object.freeze({ strict, allowedOrigins });
    }

    function assertHttpNetworkAllowed(policy, url) {
        if (!policy.strict) {
            return;
        }
        const origin = httpOriginKey(url);
        if (!policy.allowedOrigins.has("*") && !policy.allowedOrigins.has(origin)) {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_STRICT_NETWORK_DENIED",
                "HTTP client strict network policy denied the outbound origin.",
            );
        }
    }

    function isHttpRedirectStatus(status) {
        return status === 301 || status === 302 || status === 303 || status === 307 || status === 308;
    }

    function httpConnectionHeaderHas(value, token) {
        return value.split(",").some((part) => part.trim().toLowerCase() === token);
    }

    function isHttpBodyForbiddenStatus(status) {
        return (status >= 100 && status <= 199) || status === 204 || status === 304;
    }

    function resolveHttpRedirectUrl(currentUrl, location, operation) {
        if (typeof location !== "string" || location.length === 0 || hasHttpControlChars(location)) {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                "HTTP redirect Location must be a non-empty URL without control characters.",
            );
        }
        if (location.includes("://")) {
            return parseAbsoluteHttpUrl(location, operation);
        }
        if (location.startsWith("//")) {
            return parseAbsoluteHttpUrl(`${currentUrl.scheme}:${location}`, operation);
        }
        return Object.freeze({ ...currentUrl, target: joinHttpTarget(currentUrl.target, location, operation) });
    }

    class HttpConnectionPool {
        constructor(options) {
            this._options = options;
            this._entries = new Map();
            this._http2Entries = new Map();
        }

        _entry(originKey) {
            let entry = this._entries.get(originKey);
            if (entry === undefined) {
                entry = { idle: [], total: 0, inUse: 0 };
                this._entries.set(originKey, entry);
            }
            return entry;
        }

        _prune(originKey, entry) {
            if (entry.total === 0 && entry.inUse === 0 && entry.idle.length === 0) {
                this._entries.delete(originKey);
            }
        }

        async acquire(originKey, connect) {
            const entry = this._entry(originKey);
            const idle = entry.idle.pop();
            if (idle !== undefined) {
                clearTimeout(idle.timer);
                entry.inUse += 1;
                return { connection: idle.connection, reused: true };
            }
            if (entry.total >= this._options.maxConnectionsPerOrigin) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_POOL_EXHAUSTED", "HTTP client connection pool exhausted for origin.");
            }
            entry.total += 1;
            entry.inUse += 1;
            try {
                return { connection: await connect(), reused: false };
            } catch (error) {
                entry.total -= 1;
                entry.inUse -= 1;
                this._prune(originKey, entry);
                throw error;
            }
        }

        async release(originKey, connection, reusable) {
            const entry = this._entries.get(originKey);
            if (entry === undefined) {
                await connection.close().catch(() => {});
                return;
            }
            entry.inUse -= 1;
            if (reusable && this._options.idleTimeoutMs > 0) {
                const timer = setTimeout(() => {
                    const index = entry.idle.findIndex((idle) => idle.connection === connection);
                    if (index >= 0) {
                        entry.idle.splice(index, 1);
                        entry.total -= 1;
                        connection.close().catch(() => {});
                        this._prune(originKey, entry);
                    }
                }, this._options.idleTimeoutMs);
                entry.idle.push({ connection, timer });
                return;
            }
            entry.total -= 1;
            await connection.close().catch(() => {});
            this._prune(originKey, entry);
        }

        _http2Entry(originKey) {
            let entry = this._http2Entries.get(originKey);
            if (entry === undefined) {
                entry = { sessions: [], total: 0, pending: undefined };
                this._http2Entries.set(originKey, entry);
            }
            return entry;
        }

        _pruneHttp2(originKey, entry) {
            entry.sessions = entry.sessions.filter((record) => !record.session.closed);
            if (entry.total === 0 && entry.sessions.length === 0 && entry.pending === undefined) {
                this._http2Entries.delete(originKey);
            }
        }

        _dropHttp2Record(originKey, entry, record) {
            if (record.timer !== undefined) {
                clearTimeout(record.timer);
                record.timer = undefined;
            }
            const index = entry.sessions.indexOf(record);
            if (index >= 0) {
                entry.sessions.splice(index, 1);
                entry.total = Math.max(0, entry.total - 1);
            }
            this._pruneHttp2(originKey, entry);
        }

        _findReusableHttp2Session(entry) {
            for (const record of entry.sessions) {
                if (!record.session.closed && record.session.acceptsStreams) {
                    if (record.timer !== undefined) {
                        clearTimeout(record.timer);
                        record.timer = undefined;
                    }
                    return record.session;
                }
            }
            return undefined;
        }

        async acquireHttp2(originKey, connect) {
            const entry = this._http2Entry(originKey);
            const reusable = this._findReusableHttp2Session(entry);
            if (reusable !== undefined) {
                return { session: reusable, reused: true };
            }
            if (entry.pending !== undefined) {
                const session = await entry.pending;
                if (!session.closed && session.acceptsStreams) {
                    return { session, reused: true };
                }
            }
            if (entry.total >= this._options.maxConnectionsPerOrigin) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_POOL_EXHAUSTED",
                    "HTTP client connection pool exhausted for origin.",
                );
            }
            entry.total += 1;
            entry.pending = connect().then((session) => {
                const record = { session, timer: undefined };
                entry.sessions.push(record);
                session.onClose(() => this._dropHttp2Record(originKey, entry, record));
                return session;
            });
            try {
                return { session: await entry.pending, reused: false };
            } catch (error) {
                entry.total = Math.max(0, entry.total - 1);
                this._pruneHttp2(originKey, entry);
                throw error;
            } finally {
                entry.pending = undefined;
                this._pruneHttp2(originKey, entry);
            }
        }

        peekHttp2(originKey) {
            const entry = this._http2Entries.get(originKey);
            if (entry === undefined) {
                return undefined;
            }
            return this._findReusableHttp2Session(entry);
        }

        adoptHttp2(originKey, session) {
            const entry = this._http2Entry(originKey);
            if (entry.total >= this._options.maxConnectionsPerOrigin) {
                session.close().catch(() => {});
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_POOL_EXHAUSTED",
                    "HTTP client connection pool exhausted for origin.",
                );
            }
            const record = { session, timer: undefined };
            entry.total += 1;
            entry.sessions.push(record);
            session.onClose(() => this._dropHttp2Record(originKey, entry, record));
            return session;
        }

        releaseHttp2(originKey, session) {
            const entry = this._http2Entries.get(originKey);
            if (entry === undefined) {
                session.close().catch(() => {});
                return;
            }
            const record = entry.sessions.find((candidate) => candidate.session === session);
            if (record === undefined) {
                session.close().catch(() => {});
                return;
            }
            if (session.closed) {
                this._dropHttp2Record(originKey, entry, record);
                return;
            }
            if (session.activeStreamCount > 0) {
                return;
            }
            if (this._options.idleTimeoutMs === 0) {
                this._dropHttp2Record(originKey, entry, record);
                session.close().catch(() => {});
                return;
            }
            if (record.timer !== undefined) {
                clearTimeout(record.timer);
            }
            record.timer = setTimeout(() => {
                this._dropHttp2Record(originKey, entry, record);
                session.close().catch(() => {});
            }, this._options.idleTimeoutMs);
        }
    }

    function isHttpCancellationSignal(value) {
        return (
            value !== undefined &&
            value !== null &&
            typeof value === "object" &&
            typeof value.aborted === "boolean"
        );
    }

    function subscribeHttpCancellation(signal, listener) {
        if (!isHttpCancellationSignal(signal)) {
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

    function httpDeadlineRemainingMs(deadline, operation) {
        if (deadline === undefined) {
            return Infinity;
        }
        if (typeof deadline !== "object" || deadline === null || typeof deadline.remainingMs !== "function") {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} deadline must come from sloppy/time Deadline.`,
            );
        }
        const remainingMs = deadline.remainingMs();
        if (!Number.isFinite(remainingMs) && remainingMs !== Infinity) {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} deadline remaining time must be finite or Infinity.`,
            );
        }
        return Math.max(0, remainingMs);
    }

    function httpRequestTimeoutError() {
        return httpClientError("SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT", "HTTP client request timed out.");
    }

    function httpRequestCancelledError() {
        return httpClientError("SLOPPY_E_HTTP_CLIENT_REQUEST_CANCELLED", "HTTP client request was cancelled.");
    }

    function httpRemainingMs(expiresAtMs) {
        return expiresAtMs === Infinity ? Infinity : Math.max(0, expiresAtMs - Date.now());
    }

    function parseHttpPort(text, operation) {
        if (text === undefined || text === "") {
            return undefined;
        }
        if (!/^[0-9]+$/.test(text)) {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                `${operation} URL port must contain decimal digits.`,
            );
        }
        const port = Number(text);
        if (!Number.isInteger(port) || port < 1 || port > 65535) {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                `${operation} URL port must be from 1 to 65535.`,
            );
        }
        return port;
    }

    function parseAbsoluteHttpUrl(url, operation) {
        if (typeof url !== "string" || url.length === 0) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_URL", `${operation} URL must be a non-empty string.`);
        }
        const schemeEnd = url.indexOf("://");
        if (schemeEnd <= 0) {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                `${operation} requires an absolute http:// or https:// URL or a baseUrl-relative path.`,
            );
        }
        const scheme = url.slice(0, schemeEnd).toLowerCase();
        if (scheme !== "http" && scheme !== "https") {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_URL", `${operation} supports http:// and https:// URLs only.`);
        }

        const rest = url.slice(schemeEnd + 3);
        let pathStart = rest.length;
        for (const marker of ["/", "?", "#"]) {
            const index = rest.indexOf(marker);
            if (index >= 0 && index < pathStart) {
                pathStart = index;
            }
        }
        const authority = rest.slice(0, pathStart);
        let target = rest.slice(pathStart);
        if (authority.length === 0 || authority.includes("@")) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_URL", `${operation} URL authority is invalid.`);
        }

        const fragment = target.indexOf("#");
        if (fragment >= 0) {
            target = target.slice(0, fragment);
        }
        if (target.length === 0) {
            target = "/";
        } else if (target.startsWith("?")) {
            target = `/${target}`;
        } else if (!target.startsWith("/")) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_URL", `${operation} URL target must be an origin-form path.`);
        }

        let host;
        let portText;
        let hostHeader;
        if (authority.startsWith("[")) {
            const close = authority.indexOf("]");
            if (close <= 1 || (authority.length > close + 1 && authority[close + 1] !== ":")) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                    `${operation} IPv6 URL host must be bracketed as [host]:port.`,
                );
            }
            host = authority.slice(1, close);
            portText = authority.length > close + 1 ? authority.slice(close + 2) : undefined;
            hostHeader = `[${host}]`;
        } else {
            const firstColon = authority.indexOf(":");
            const lastColon = authority.lastIndexOf(":");
            if (firstColon !== lastColon) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                    `${operation} unbracketed IPv6 URL hosts are ambiguous.`,
                );
            }
            host = firstColon < 0 ? authority : authority.slice(0, firstColon);
            portText = firstColon < 0 ? undefined : authority.slice(firstColon + 1);
            hostHeader = host;
        }
        if (host.length === 0 || hasHttpControlChars(host)) {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                `${operation} URL contains invalid control characters.`,
            );
        }

        const defaultPort = scheme === "https" ? 443 : 80;
        const port = parseHttpPort(portText, operation) ?? defaultPort;
        const headerPort = port === defaultPort ? "" : `:${port}`;
        const resolvedHostHeader = `${hostHeader}${headerPort}`;
        if (hasHttpControlChars(resolvedHostHeader) || hasHttpControlChars(target)) {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                `${operation} URL contains invalid control characters.`,
            );
        }
        return Object.freeze({
            scheme,
            host,
            port,
            hostHeader: resolvedHostHeader,
            target,
        });
    }

    function splitHttpTarget(target) {
        const queryStart = target.indexOf("?");
        if (queryStart < 0) {
            return { path: target, suffix: "" };
        }
        return { path: target.slice(0, queryStart), suffix: target.slice(queryStart) };
    }

    function normalizeHttpTargetPath(path) {
        const segments = [];
        for (const segment of path.split("/")) {
            if (segment === "" || segment === ".") {
                continue;
            }
            if (segment === "..") {
                segments.pop();
                continue;
            }
            segments.push(segment);
        }
        return `/${segments.join("/")}`;
    }

    function joinHttpTarget(baseTarget, requestUrl, operation) {
        if (hasHttpControlChars(requestUrl)) {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                `${operation} URL contains invalid control characters.`,
            );
        }
        if (requestUrl.length === 0) {
            const { path, suffix } = splitHttpTarget(baseTarget);
            return `${normalizeHttpTargetPath(path)}${suffix}`;
        }
        if (requestUrl.startsWith("?")) {
            const { path } = splitHttpTarget(baseTarget);
            return `${normalizeHttpTargetPath(path)}${requestUrl}`;
        }
        if (requestUrl.startsWith("/")) {
            const { path, suffix } = splitHttpTarget(requestUrl);
            return `${normalizeHttpTargetPath(path)}${suffix}`;
        }

        const { path: basePath } = splitHttpTarget(baseTarget);
        const baseDirectory = basePath.endsWith("/") ? basePath : basePath.slice(0, basePath.lastIndexOf("/") + 1);
        const { path, suffix } = splitHttpTarget(`${baseDirectory}${requestUrl}`);
        return `${normalizeHttpTargetPath(path)}${suffix}`;
    }

    function resolveHttpUrl(baseOptions, requestUrl, operation) {
        if (typeof requestUrl === "string" && hasHttpControlChars(requestUrl)) {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                `${operation} URL contains invalid control characters.`,
            );
        }
        if (typeof requestUrl === "string" && requestUrl.includes("://")) {
            return parseAbsoluteHttpUrl(requestUrl, operation);
        }
        const baseUrl = baseOptions?.baseUrl;
        if (typeof requestUrl !== "string" || typeof baseUrl !== "string") {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                `${operation} requires an absolute URL or a path with client baseUrl.`,
            );
        }
        const base = parseAbsoluteHttpUrl(baseUrl, operation);
        return Object.freeze({ ...base, target: joinHttpTarget(base.target, requestUrl, operation) });
    }

    function normalizeHttpMethod(method, operation) {
        const resolved = method === undefined ? "GET" : method;
        if (typeof resolved !== "string" || !/^[A-Za-z]+$/.test(resolved)) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} method must be a token string.`);
        }
        return resolved.toUpperCase();
    }

    function appendHttpHeaders(target, headers, operation) {
        if (headers === undefined) {
            return;
        }
        if (!isPlainObject(headers)) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} headers must be a plain object.`);
        }
        for (const [name, value] of Object.entries(headers)) {
            const normalizedName = String(name).toLowerCase();
            if (!/^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/.test(name)) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} header name is invalid.`);
            }
            if (normalizedName === "host" || normalizedName === "connection" || normalizedName === "content-length") {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    `${operation} manages Host, Connection, and Content-Length headers.`,
                );
            }
            if (typeof value !== "string" || hasHttpControlChars(value)) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    `${operation} header value must be a string without control characters.`,
                );
            }
            target.set(normalizedName, { name, value });
        }
    }

    function enforceHttpRequestBodyLimit(bytes, maxRequestBytes) {
        if (bytes.byteLength > maxRequestBytes) {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_REQUEST_BODY_LIMIT",
                "HTTP request body exceeded the configured limit.",
            );
        }
        return bytes;
    }

    function isHttpAsyncIterable(value) {
        return value !== undefined && value !== null && typeof value[Symbol.asyncIterator] === "function";
    }

    async function readHttpStreamChunk(iterator, signal, expiresAtMs) {
        if (isHttpCancellationSignal(signal) && signal.aborted) {
            throw httpRequestCancelledError();
        }
        const remainingMs = httpRemainingMs(expiresAtMs);
        if (remainingMs <= 0) {
            throw httpRequestTimeoutError();
        }
        if (remainingMs === Infinity && signal === undefined) {
            return await iterator.next();
        }
        let timeoutId;
        let cleanupCancellation = () => {};
        try {
            return await Promise.race([
                iterator.next(),
                new Promise((_, reject) => {
                    cleanupCancellation = subscribeHttpCancellation(signal, () => reject(httpRequestCancelledError()));
                    if (remainingMs !== Infinity) {
                        timeoutId = setTimeout(() => reject(httpRequestTimeoutError()), remainingMs);
                    }
                }),
            ]);
        } finally {
            cleanupCancellation();
            if (timeoutId !== undefined) {
                clearTimeout(timeoutId);
            }
        }
    }

    async function consumeHttpRequestStream(stream, maxRequestBytes, headers, operation, timing) {
        if (!isHttpAsyncIterable(stream)) {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} stream body must be an async iterable of Uint8Array chunks.`,
            );
        }
        const chunks = [];
        let totalLength = 0;
        const iterator = stream[Symbol.asyncIterator]();
        while (true) {
            const result = await readHttpStreamChunk(iterator, timing.signal, timing.expiresAtMs);
            if (result.done) {
                break;
            }
            const chunk = result.value;
            if (!(chunk instanceof Uint8Array)) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    `${operation} stream body chunks must be Uint8Array values.`,
                );
            }
            totalLength += chunk.byteLength;
            if (totalLength > maxRequestBytes) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_REQUEST_BODY_LIMIT",
                    "HTTP request body exceeded the configured limit.",
                );
            }
            chunks.push(chunk.slice());
        }
        setHttpDefaultHeader(headers, "Content-Type", "application/octet-stream");
        return concatHttpBytes(chunks, totalLength);
    }

    async function normalizeHttpBody(options, headers, operation, maxRequestBytes, timing) {
        const sources = ["json", "text", "bytes", "stream"].filter((key) => options?.[key] !== undefined);
        if (sources.length > 1) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_AMBIGUOUS_BODY", `${operation} accepts only one request body source.`);
        }
        if (sources.length === 0) {
            return new Uint8Array(0);
        }
        if (sources[0] === "json") {
            let text;
            try {
                text = JSON.stringify(options.json);
            } catch (error) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_JSON", `${operation} JSON body could not be serialized.`, {
                    cause: error,
                });
            }
            if (text === undefined) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_JSON", `${operation} JSON body must serialize to a JSON value.`);
            }
            setHttpDefaultHeader(headers, "Content-Type", "application/json");
            return enforceHttpRequestBodyLimit(sloppyUtf8ToBytes(text), maxRequestBytes);
        }
        if (sources[0] === "text") {
            if (typeof options.text !== "string") {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} text body must be a string.`);
            }
            setHttpDefaultHeader(headers, "Content-Type", "text/plain; charset=utf-8");
            return enforceHttpRequestBodyLimit(sloppyUtf8ToBytes(options.text), maxRequestBytes);
        }
        if (sources[0] === "stream") {
            return await consumeHttpRequestStream(options.stream, maxRequestBytes, headers, operation, timing);
        }
        if (!(options.bytes instanceof Uint8Array)) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} bytes body must be a Uint8Array.`);
        }
        return enforceHttpRequestBodyLimit(options.bytes.slice(), maxRequestBytes);
    }

    function concatHttpBytes(chunks, totalLength) {
        const out = new Uint8Array(totalLength);
        let offset = 0;
        for (const chunk of chunks) {
            out.set(chunk, offset);
            offset += chunk.byteLength;
        }
        return out;
    }

    function httpBytesToAscii(bytes) {
        let text = "";
        for (const byte of bytes) {
            text += String.fromCharCode(byte);
        }
        return text;
    }

    function findHttpHeaderEnd(bytes) {
        for (let index = 3; index < bytes.byteLength; index += 1) {
            if (bytes[index - 3] === 13 && bytes[index - 2] === 10 && bytes[index - 1] === 13 && bytes[index] === 10) {
                return index + 1;
            }
        }
        return -1;
    }

    function findHttpCrlf(bytes, start) {
        for (let index = start + 1; index < bytes.byteLength; index += 1) {
            if (bytes[index - 1] === 13 && bytes[index] === 10) {
                return index - 1;
            }
        }
        return -1;
    }

    function incompleteHttpChunk(complete, message) {
        if (!complete) {
            return undefined;
        }
        throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE", message);
    }

    function parseHttpChunkedBody(bodyBytes, maxResponseBytes, complete) {
        const chunks = [];
        let totalLength = 0;
        let offset = 0;
        while (true) {
            const sizeLineEnd = findHttpCrlf(bodyBytes, offset);
            if (sizeLineEnd < 0) {
                return incompleteHttpChunk(complete, "HTTP chunked response ended before a chunk size.");
            }
            const sizeLine = httpBytesToAscii(bodyBytes.slice(offset, sizeLineEnd));
            const sizeText = sizeLine.split(";", 1)[0].trim();
            if (!/^[0-9A-Fa-f]+$/.test(sizeText)) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE", "HTTP chunked response chunk size is malformed.");
            }
            const size = Number.parseInt(sizeText, 16);
            if (!Number.isSafeInteger(size)) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT", "HTTP response body exceeded the configured limit.");
            }
            offset = sizeLineEnd + 2;
            if (size === 0) {
                if (offset + 2 > bodyBytes.byteLength) {
                    return incompleteHttpChunk(complete, "HTTP chunked response ended before trailers.");
                }
                if (bodyBytes[offset] === 13 && bodyBytes[offset + 1] === 10) {
                    return concatHttpBytes(chunks, totalLength);
                }
                const trailerEnd = findHttpHeaderEnd(bodyBytes.slice(offset));
                if (trailerEnd < 0) {
                    return incompleteHttpChunk(complete, "HTTP chunked response trailers are incomplete.");
                }
                return concatHttpBytes(chunks, totalLength);
            }
            if (totalLength + size > maxResponseBytes) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT", "HTTP response body exceeded the configured limit.");
            }
            if (offset + size + 2 > bodyBytes.byteLength) {
                return incompleteHttpChunk(complete, "HTTP chunked response ended before a complete chunk.");
            }
            if (bodyBytes[offset + size] !== 13 || bodyBytes[offset + size + 1] !== 10) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE", "HTTP chunked response chunk terminator is malformed.");
            }
            chunks.push(bodyBytes.slice(offset, offset + size));
            totalLength += size;
            offset += size + 2;
        }
    }

    class HttpHeaderBag {
        constructor(entries) {
            this._entries = entries;
            this._map = new Map(entries.map(([name, value]) => [name.toLowerCase(), value]));
            Object.freeze(this);
        }

        get(name) {
            if (typeof name !== "string") {
                throw new TypeError("HttpResponse.headers.get name must be a string.");
            }
            return this._map.get(name.toLowerCase()) ?? null;
        }

        entries() {
            return this._entries.map(([name, value]) => [name, value]);
        }
    }

    class HttpClientResponse {
        constructor(status, statusText, headers, body, connectionReusable = false) {
            this.status = status;
            this.statusText = statusText;
            this.headers = headers;
            this._body = body;
            this._consumed = false;
            this._connectionReusable = connectionReusable;
        }

        _consume() {
            if (this._consumed) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_BODY_CONSUMED", "HTTP response body was already consumed.");
            }
            this._consumed = true;
            return this._body;
        }

        async bytes() {
            return this._consume().slice();
        }

        async text() {
            return new TextDecoder().decode(this._consume());
        }

        async json() {
            try {
                return JSON.parse(new TextDecoder().decode(this._consume()));
            } catch (error) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_JSON", "HTTP response body is not valid JSON.", { cause: error });
            }
        }

        stream(options = undefined) {
            const chunkSize = options?.chunkSize ?? 8192;
            if (!Number.isInteger(chunkSize) || chunkSize <= 0) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    "HTTP response stream chunkSize must be a positive integer.",
                );
            }
            const body = this._consume();
            return (async function* streamHttpResponseBody() {
                for (let offset = 0; offset < body.byteLength; offset += chunkSize) {
                    yield body.slice(offset, Math.min(body.byteLength, offset + chunkSize));
                }
            })();
        }
    }

    function parseHttpResponse(headBytes, bodyBytes, maxResponseBytes, complete = false, requestMethod = "GET") {
        const head = httpBytesToAscii(headBytes);
        const lines = head.split("\r\n");
        const statusLine = lines.shift();
        const match = /^HTTP\/1\.([01]) ([0-9]{3})(?: (.*))?$/.exec(statusLine ?? "");
        const headers = [];
        let contentLength = undefined;
        let transferEncoding = undefined;
        let connectionHeader = "";
        if (match === null) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE", "HTTP response status line is malformed.");
        }
        const status = Number(match[2]);
        for (const line of lines) {
            if (line.length === 0) {
                continue;
            }
            const colon = line.indexOf(":");
            if (colon <= 0) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE", "HTTP response header is malformed.");
            }
            const name = line.slice(0, colon);
            const value = line.slice(colon + 1).trimStart();
            headers.push([name, value]);
            if (name.toLowerCase() === "content-length") {
                if (!/^[0-9]+$/.test(value)) {
                    throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE", "HTTP response Content-Length is malformed.");
                }
                contentLength = Number(value);
            }
            if (name.toLowerCase() === "transfer-encoding") {
                transferEncoding = value.toLowerCase();
            }
            if (name.toLowerCase() === "connection") {
                connectionHeader = value.toLowerCase();
            }
        }
        const httpMinorVersion = match === null ? 1 : Number(match[1]);
        let connectionReusable =
            complete === false &&
            (httpMinorVersion === 1
                ? !httpConnectionHeaderHas(connectionHeader, "close")
                : httpConnectionHeaderHas(connectionHeader, "keep-alive"));
        const methodForbidsBody = requestMethod === "HEAD";
        const statusForbidsBody = isHttpBodyForbiddenStatus(status);
        const bodyForbidden = methodForbidsBody || statusForbidsBody;
        if (bodyForbidden) {
            if (statusForbidsBody && contentLength !== undefined && contentLength > 0) {
                connectionReusable = false;
            }
            if (statusForbidsBody && transferEncoding !== undefined) {
                connectionReusable = false;
            }
            if (bodyBytes.byteLength > 0) {
                connectionReusable = false;
            }
            bodyBytes = bodyBytes.slice(0, 0);
        } else if (transferEncoding === "chunked") {
            const decoded = parseHttpChunkedBody(bodyBytes, maxResponseBytes, complete);
            if (decoded === undefined) {
                return undefined;
            }
            bodyBytes = decoded;
        } else if (transferEncoding !== undefined && transferEncoding !== "identity") {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP response Transfer-Encoding is not supported by this HTTP client.",
            );
        } else if (contentLength !== undefined) {
            if (contentLength > maxResponseBytes) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT", "HTTP response body exceeded the configured limit.");
            }
            if (bodyBytes.byteLength < contentLength) {
                if (complete) {
                    throw httpClientError(
                        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP response ended before the declared Content-Length body was fully received.",
                    );
                }
                return undefined;
            }
            bodyBytes = bodyBytes.slice(0, contentLength);
        } else {
            if (bodyBytes.byteLength > maxResponseBytes) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT", "HTTP response body exceeded the configured limit.");
            }
            if (!complete) {
                return undefined;
            }
            connectionReusable = false;
        }
        return new HttpClientResponse(status, match[3] ?? "", new HttpHeaderBag(headers), bodyBytes, connectionReusable);
    }

    async function readHttpResponse(connection, limits) {
        const chunks = [];
        let totalLength = 0;
        let headerEnd = -1;
        let parsed = undefined;
        while (parsed === undefined) {
            let chunk;
            try {
                chunk = await connection.read({ maxBytes: 8192 });
            } catch (error) {
                if (headerEnd >= 0) {
                    break;
                }
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP response ended before a complete head was received.",
                    { cause: error },
                );
            }
            chunks.push(chunk);
            totalLength += chunk.byteLength;
            if (totalLength > limits.maxHeaderBytes + limits.maxResponseBytes) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT", "HTTP response exceeded the configured limit.");
            }
            const received = concatHttpBytes(chunks, totalLength);
            headerEnd = findHttpHeaderEnd(received);
            if (headerEnd < 0) {
                if (totalLength > limits.maxHeaderBytes) {
                    throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE", "HTTP response headers exceeded the configured limit.");
                }
                continue;
            }
            if (headerEnd > limits.maxHeaderBytes) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE", "HTTP response headers exceeded the configured limit.");
            }
            parsed = parseHttpResponse(
                received.slice(0, headerEnd),
                received.slice(headerEnd),
                limits.maxResponseBytes,
                false,
                limits.method,
            );
        }
        if (parsed === undefined) {
            const received = concatHttpBytes(chunks, totalLength);
            parsed = parseHttpResponse(received.slice(0, headerEnd), received.slice(headerEnd), limits.maxResponseBytes, true, limits.method);
        }
        return parsed;
    }

    const HTTP2_CLIENT_PREFACE = sloppyUtf8ToBytes("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n");
    const HTTP2_FRAME_DATA = 0x0;
    const HTTP2_FRAME_HEADERS = 0x1;
    const HTTP2_FRAME_RST_STREAM = 0x3;
    const HTTP2_FRAME_SETTINGS = 0x4;
    const HTTP2_FRAME_PING = 0x6;
    const HTTP2_FRAME_GOAWAY = 0x7;
    const HTTP2_FRAME_WINDOW_UPDATE = 0x8;
    const HTTP2_FRAME_CONTINUATION = 0x9;
    const HTTP2_FLAG_END_STREAM = 0x1;
    const HTTP2_FLAG_ACK = 0x1;
    const HTTP2_FLAG_END_HEADERS = 0x4;
    const HTTP2_DEFAULT_MAX_FRAME_SIZE = 16384;
    const HTTP2_DEFAULT_DYNAMIC_TABLE_BYTES = 4096;
    const HTTP2_SETTING_ENABLE_PUSH = 0x2;
    const HTTP2_SETTING_MAX_FRAME_SIZE = 0x5;
    const HTTP2_SETTING_MAX_HEADER_LIST_SIZE = 0x6;
    const HTTP2_ERROR_CANCEL = 0x8;

    const HTTP2_HPACK_STATIC = [
        undefined,
        [":authority", ""],
        [":method", "GET"],
        [":method", "POST"],
        [":path", "/"],
        [":path", "/index.html"],
        [":scheme", "http"],
        [":scheme", "https"],
        [":status", "200"],
        [":status", "204"],
        [":status", "206"],
        [":status", "304"],
        [":status", "400"],
        [":status", "404"],
        [":status", "500"],
        ["accept-charset", ""],
        ["accept-encoding", "gzip, deflate"],
        ["accept-language", ""],
        ["accept-ranges", ""],
        ["accept", ""],
        ["access-control-allow-origin", ""],
        ["age", ""],
        ["allow", ""],
        ["authorization", ""],
        ["cache-control", ""],
        ["content-disposition", ""],
        ["content-encoding", ""],
        ["content-language", ""],
        ["content-length", ""],
        ["content-location", ""],
        ["content-range", ""],
        ["content-type", ""],
        ["cookie", ""],
        ["date", ""],
        ["etag", ""],
        ["expect", ""],
        ["expires", ""],
        ["from", ""],
        ["host", ""],
        ["if-match", ""],
        ["if-modified-since", ""],
        ["if-none-match", ""],
        ["if-range", ""],
        ["if-unmodified-since", ""],
        ["last-modified", ""],
        ["link", ""],
        ["location", ""],
        ["max-forwards", ""],
        ["proxy-authenticate", ""],
        ["proxy-authorization", ""],
        ["range", ""],
        ["referer", ""],
        ["refresh", ""],
        ["retry-after", ""],
        ["server", ""],
        ["set-cookie", ""],
        ["strict-transport-security", ""],
        ["transfer-encoding", ""],
        ["user-agent", ""],
        ["vary", ""],
        ["via", ""],
        ["www-authenticate", ""],
    ];

    const HTTP2_HPACK_NAME_INDEX = new Map();
    for (let index = 1; index < HTTP2_HPACK_STATIC.length; index += 1) {
        const name = HTTP2_HPACK_STATIC[index][0];
        if (!HTTP2_HPACK_NAME_INDEX.has(name)) {
            HTTP2_HPACK_NAME_INDEX.set(name, index);
        }
    }

    const HTTP2_HPACK_HUFFMAN_EOS = 256;
    const HTTP2_HPACK_HUFFMAN_CODES = [
        [0x1ff8, 13],
        [0x7fffd8, 23],
        [0xfffffe2, 28],
        [0xfffffe3, 28],
        [0xfffffe4, 28],
        [0xfffffe5, 28],
        [0xfffffe6, 28],
        [0xfffffe7, 28],
        [0xfffffe8, 28],
        [0xffffea, 24],
        [0x3ffffffc, 30],
        [0xfffffe9, 28],
        [0xfffffea, 28],
        [0x3ffffffd, 30],
        [0xfffffeb, 28],
        [0xfffffec, 28],
        [0xfffffed, 28],
        [0xfffffee, 28],
        [0xfffffef, 28],
        [0xffffff0, 28],
        [0xffffff1, 28],
        [0xffffff2, 28],
        [0x3ffffffe, 30],
        [0xffffff3, 28],
        [0xffffff4, 28],
        [0xffffff5, 28],
        [0xffffff6, 28],
        [0xffffff7, 28],
        [0xffffff8, 28],
        [0xffffff9, 28],
        [0xffffffa, 28],
        [0xffffffb, 28],
        [0x14, 6],
        [0x3f8, 10],
        [0x3f9, 10],
        [0xffa, 12],
        [0x1ff9, 13],
        [0x15, 6],
        [0xf8, 8],
        [0x7fa, 11],
        [0x3fa, 10],
        [0x3fb, 10],
        [0xf9, 8],
        [0x7fb, 11],
        [0xfa, 8],
        [0x16, 6],
        [0x17, 6],
        [0x18, 6],
        [0x0, 5],
        [0x1, 5],
        [0x2, 5],
        [0x19, 6],
        [0x1a, 6],
        [0x1b, 6],
        [0x1c, 6],
        [0x1d, 6],
        [0x1e, 6],
        [0x1f, 6],
        [0x5c, 7],
        [0xfb, 8],
        [0x7ffc, 15],
        [0x20, 6],
        [0xffb, 12],
        [0x3fc, 10],
        [0x1ffa, 13],
        [0x21, 6],
        [0x5d, 7],
        [0x5e, 7],
        [0x5f, 7],
        [0x60, 7],
        [0x61, 7],
        [0x62, 7],
        [0x63, 7],
        [0x64, 7],
        [0x65, 7],
        [0x66, 7],
        [0x67, 7],
        [0x68, 7],
        [0x69, 7],
        [0x6a, 7],
        [0x6b, 7],
        [0x6c, 7],
        [0x6d, 7],
        [0x6e, 7],
        [0x6f, 7],
        [0x70, 7],
        [0x71, 7],
        [0x72, 7],
        [0xfc, 8],
        [0x73, 7],
        [0xfd, 8],
        [0x1ffb, 13],
        [0x7fff0, 19],
        [0x1ffc, 13],
        [0x3ffc, 14],
        [0x22, 6],
        [0x7ffd, 15],
        [0x3, 5],
        [0x23, 6],
        [0x4, 5],
        [0x24, 6],
        [0x5, 5],
        [0x25, 6],
        [0x26, 6],
        [0x27, 6],
        [0x6, 5],
        [0x74, 7],
        [0x75, 7],
        [0x28, 6],
        [0x29, 6],
        [0x2a, 6],
        [0x7, 5],
        [0x2b, 6],
        [0x76, 7],
        [0x2c, 6],
        [0x8, 5],
        [0x9, 5],
        [0x2d, 6],
        [0x77, 7],
        [0x78, 7],
        [0x79, 7],
        [0x7a, 7],
        [0x7b, 7],
        [0x7ffe, 15],
        [0x7fc, 11],
        [0x3ffd, 14],
        [0x1ffd, 13],
        [0xffffffc, 28],
        [0xfffe6, 20],
        [0x3fffd2, 22],
        [0xfffe7, 20],
        [0xfffe8, 20],
        [0x3fffd3, 22],
        [0x3fffd4, 22],
        [0x3fffd5, 22],
        [0x7fffd9, 23],
        [0x3fffd6, 22],
        [0x7fffda, 23],
        [0x7fffdb, 23],
        [0x7fffdc, 23],
        [0x7fffdd, 23],
        [0x7fffde, 23],
        [0xffffeb, 24],
        [0x7fffdf, 23],
        [0xffffec, 24],
        [0xffffed, 24],
        [0x3fffd7, 22],
        [0x7fffe0, 23],
        [0xffffee, 24],
        [0x7fffe1, 23],
        [0x7fffe2, 23],
        [0x7fffe3, 23],
        [0x7fffe4, 23],
        [0x1fffdc, 21],
        [0x3fffd8, 22],
        [0x7fffe5, 23],
        [0x3fffd9, 22],
        [0x7fffe6, 23],
        [0x7fffe7, 23],
        [0xffffef, 24],
        [0x3fffda, 22],
        [0x1fffdd, 21],
        [0xfffe9, 20],
        [0x3fffdb, 22],
        [0x3fffdc, 22],
        [0x7fffe8, 23],
        [0x7fffe9, 23],
        [0x1fffde, 21],
        [0x7fffea, 23],
        [0x3fffdd, 22],
        [0x3fffde, 22],
        [0xfffff0, 24],
        [0x1fffdf, 21],
        [0x3fffdf, 22],
        [0x7fffeb, 23],
        [0x7fffec, 23],
        [0x1fffe0, 21],
        [0x1fffe1, 21],
        [0x3fffe0, 22],
        [0x1fffe2, 21],
        [0x7fffed, 23],
        [0x3fffe1, 22],
        [0x7fffee, 23],
        [0x7fffef, 23],
        [0xfffea, 20],
        [0x3fffe2, 22],
        [0x3fffe3, 22],
        [0x3fffe4, 22],
        [0x7ffff0, 23],
        [0x3fffe5, 22],
        [0x3fffe6, 22],
        [0x7ffff1, 23],
        [0x3ffffe0, 26],
        [0x3ffffe1, 26],
        [0xfffeb, 20],
        [0x7fff1, 19],
        [0x3fffe7, 22],
        [0x7ffff2, 23],
        [0x3fffe8, 22],
        [0x1ffffec, 25],
        [0x3ffffe2, 26],
        [0x3ffffe3, 26],
        [0x3ffffe4, 26],
        [0x7ffffde, 27],
        [0x7ffffdf, 27],
        [0x3ffffe5, 26],
        [0xfffff1, 24],
        [0x1ffffed, 25],
        [0x7fff2, 19],
        [0x1fffe3, 21],
        [0x3ffffe6, 26],
        [0x7ffffe0, 27],
        [0x7ffffe1, 27],
        [0x3ffffe7, 26],
        [0x7ffffe2, 27],
        [0xfffff2, 24],
        [0x1fffe4, 21],
        [0x1fffe5, 21],
        [0x3ffffe8, 26],
        [0x3ffffe9, 26],
        [0xffffffd, 28],
        [0x7ffffe3, 27],
        [0x7ffffe4, 27],
        [0x7ffffe5, 27],
        [0xfffec, 20],
        [0xfffff3, 24],
        [0xfffed, 20],
        [0x1fffe6, 21],
        [0x3fffe9, 22],
        [0x1fffe7, 21],
        [0x1fffe8, 21],
        [0x7ffff3, 23],
        [0x3fffea, 22],
        [0x3fffeb, 22],
        [0x1ffffee, 25],
        [0x1ffffef, 25],
        [0xfffff4, 24],
        [0xfffff5, 24],
        [0x3ffffea, 26],
        [0x7ffff4, 23],
        [0x3ffffeb, 26],
        [0x7ffffe6, 27],
        [0x3ffffec, 26],
        [0x3ffffed, 26],
        [0x7ffffe7, 27],
        [0x7ffffe8, 27],
        [0x7ffffe9, 27],
        [0x7ffffea, 27],
        [0x7ffffeb, 27],
        [0xffffffe, 28],
        [0x7ffffec, 27],
        [0x7ffffed, 27],
        [0x7ffffee, 27],
        [0x7ffffef, 27],
        [0x7fffff0, 27],
        [0x3ffffee, 26],
        [0x3fffffff, 30],
    ];

    function buildHpackHuffmanTree() {
        const root = {};
        for (let symbol = 0; symbol < HTTP2_HPACK_HUFFMAN_CODES.length; symbol += 1) {
            const [code, length] = HTTP2_HPACK_HUFFMAN_CODES[symbol];
            let node = root;
            for (let bitIndex = length - 1; bitIndex >= 0; bitIndex -= 1) {
                const bit = (code >>> bitIndex) & 1;
                node[bit] ??= {};
                node = node[bit];
            }
            node.symbol = symbol;
        }
        return root;
    }

    const HTTP2_HPACK_HUFFMAN_TREE = buildHpackHuffmanTree();

    function http2Frame(type, flags, streamId, payload = new Uint8Array(0)) {
        if (!(payload instanceof Uint8Array) || payload.byteLength > 0xffffff) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_REQUEST_BODY_LIMIT",
                "HTTP/2 frame payload exceeded the supported frame size.",
            );
        }
        const frame = new Uint8Array(9 + payload.byteLength);
        frame[0] = (payload.byteLength >>> 16) & 0xff;
        frame[1] = (payload.byteLength >>> 8) & 0xff;
        frame[2] = payload.byteLength & 0xff;
        frame[3] = type;
        frame[4] = flags;
        frame[5] = (streamId >>> 24) & 0x7f;
        frame[6] = (streamId >>> 16) & 0xff;
        frame[7] = (streamId >>> 8) & 0xff;
        frame[8] = streamId & 0xff;
        frame.set(payload, 9);
        return frame;
    }

    function http2Concat(parts) {
        let total = 0;
        for (const part of parts) {
            total += part.byteLength;
        }
        const bytes = new Uint8Array(total);
        let offset = 0;
        for (const part of parts) {
            bytes.set(part, offset);
            offset += part.byteLength;
        }
        return bytes;
    }

    function http2Uint32(value) {
        return new Uint8Array([
            (value >>> 24) & 0xff,
            (value >>> 16) & 0xff,
            (value >>> 8) & 0xff,
            value & 0xff,
        ]);
    }

    function http2Setting(id, value) {
        return new Uint8Array([
            (id >>> 8) & 0xff,
            id & 0xff,
            (value >>> 24) & 0xff,
            (value >>> 16) & 0xff,
            (value >>> 8) & 0xff,
            value & 0xff,
        ]);
    }

    function http2DataFrames(streamId, body) {
        if (body.byteLength === 0) {
            return [];
        }
        const frames = [];
        for (let offset = 0; offset < body.byteLength; offset += HTTP2_DEFAULT_MAX_FRAME_SIZE) {
            const end = Math.min(body.byteLength, offset + HTTP2_DEFAULT_MAX_FRAME_SIZE);
            const flags = end === body.byteLength ? HTTP2_FLAG_END_STREAM : 0;
            frames.push(http2Frame(HTTP2_FRAME_DATA, flags, streamId, body.slice(offset, end)));
        }
        return frames;
    }

    function http2HeaderFrames(streamId, headerBlock, endStream, maxFrameSize = HTTP2_DEFAULT_MAX_FRAME_SIZE) {
        const frames = [];
        let offset = 0;
        while (offset < headerBlock.byteLength || frames.length === 0) {
            const end = Math.min(headerBlock.byteLength, offset + maxFrameSize);
            const final = end === headerBlock.byteLength;
            const type = frames.length === 0 ? HTTP2_FRAME_HEADERS : HTTP2_FRAME_CONTINUATION;
            let flags = final ? HTTP2_FLAG_END_HEADERS : 0;
            if (frames.length === 0 && endStream) {
                flags |= HTTP2_FLAG_END_STREAM;
            }
            frames.push(http2Frame(type, flags, streamId, headerBlock.slice(offset, end)));
            offset = end;
            if (headerBlock.byteLength === 0) {
                break;
            }
        }
        return frames;
    }

    function http2UnpadPayload(payload) {
        if (payload.byteLength === 0) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 padded frame is missing the Pad Length field.",
            );
        }
        const padLength = payload[0];
        if (padLength >= payload.byteLength) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 padded frame declares padding beyond the payload.",
            );
        }
        return payload.slice(1, payload.byteLength - padLength);
    }

    function http2WindowUpdateFrame(streamId, increment) {
        if (!Number.isInteger(increment) || increment <= 0 || increment > 0x7fffffff) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 flow-control increment is invalid.",
            );
        }
        return http2Frame(HTTP2_FRAME_WINDOW_UPDATE, 0, streamId, http2Uint32(increment));
    }

    function http2RstStreamFrame(streamId, errorCode = HTTP2_ERROR_CANCEL) {
        return http2Frame(HTTP2_FRAME_RST_STREAM, 0, streamId, http2Uint32(errorCode));
    }

    function hpackInteger(value, prefixBits, prefixMask) {
        const maxPrefix = (1 << prefixBits) - 1;
        const bytes = [];
        if (value < maxPrefix) {
            bytes.push(prefixMask | value);
        } else {
            bytes.push(prefixMask | maxPrefix);
            value -= maxPrefix;
            while (value >= 128) {
                bytes.push((value % 128) + 128);
                value = Math.floor(value / 128);
            }
            bytes.push(value);
        }
        return new Uint8Array(bytes);
    }

    function hpackReadInteger(bytes, offset, prefixBits) {
        const maxPrefix = (1 << prefixBits) - 1;
        let value = bytes[offset] & maxPrefix;
        offset += 1;
        if (value !== maxPrefix) {
            return { value, offset };
        }
        let shift = 0;
        while (offset < bytes.byteLength) {
            const byte = bytes[offset];
            offset += 1;
            value += (byte & 0x7f) * (2 ** shift);
            if ((byte & 0x80) === 0) {
                return { value, offset };
            }
            shift += 7;
        }
        throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
            "HTTP/2 HPACK integer is incomplete.",
        );
    }

    function hpackString(value) {
        const bytes = sloppyUtf8ToBytes(value);
        return http2Concat([hpackInteger(bytes.byteLength, 7, 0x00), bytes]);
    }

    function hpackDecodeHuffman(bytes) {
        const output = [];
        let node = HTTP2_HPACK_HUFFMAN_TREE;
        let pendingBits = 0;
        let pendingValue = 0;

        for (const byte of bytes) {
            for (let bitIndex = 7; bitIndex >= 0; bitIndex -= 1) {
                const bit = (byte >>> bitIndex) & 1;
                node = node[bit];
                pendingBits += 1;
                pendingValue = (pendingValue << 1) | bit;
                if (node === undefined) {
                    throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 HPACK Huffman string contains an invalid code.",
                    );
                }
                if (node.symbol !== undefined) {
                    if (node.symbol === HTTP2_HPACK_HUFFMAN_EOS) {
                        throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                            "HTTP/2 HPACK Huffman string contains the EOS symbol.",
                        );
                    }
                    output.push(node.symbol);
                    node = HTTP2_HPACK_HUFFMAN_TREE;
                    pendingBits = 0;
                    pendingValue = 0;
                }
            }
        }

        if (node !== HTTP2_HPACK_HUFFMAN_TREE) {
            if (pendingBits > 7 || pendingValue !== (1 << pendingBits) - 1) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 HPACK Huffman string has invalid padding.",
                );
            }
        }

        return new Uint8Array(output);
    }

    function hpackReadString(bytes, offset) {
        if (offset >= bytes.byteLength) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 HPACK string is incomplete.",
            );
        }
        const huffman = (bytes[offset] & 0x80) !== 0;
        const length = hpackReadInteger(bytes, offset, 7);
        offset = length.offset;
        if (offset + length.value > bytes.byteLength) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 HPACK string length exceeds the header block.",
            );
        }
        const encoded = bytes.slice(offset, offset + length.value);
        const decoded = huffman ? hpackDecodeHuffman(encoded) : encoded;
        return {
            value: new TextDecoder().decode(decoded),
            offset: offset + length.value,
        };
    }

    function hpackHeader(name, value, sensitive = false) {
        const normalized = name.toLowerCase();
        if (normalized === ":method" && value === "GET") {
            return hpackInteger(2, 7, 0x80);
        }
        if (normalized === ":method" && value === "POST") {
            return hpackInteger(3, 7, 0x80);
        }
        if (normalized === ":path" && value === "/") {
            return hpackInteger(4, 7, 0x80);
        }
        if (normalized === ":scheme" && value === "http") {
            return hpackInteger(6, 7, 0x80);
        }
        if (normalized === ":scheme" && value === "https") {
            return hpackInteger(7, 7, 0x80);
        }
        const nameIndex = HTTP2_HPACK_NAME_INDEX.get(normalized);
        const prefix = sensitive ? 0x10 : 0x00;
        const nameBytes = nameIndex === undefined ? hpackString(normalized) : new Uint8Array(0);
        const indexedName = nameIndex === undefined ? hpackInteger(0, 4, prefix) : hpackInteger(nameIndex, 4, prefix);
        return http2Concat([indexedName, nameBytes, hpackString(value)]);
    }

    function hpackEncodeRequestHeaders(request) {
        const parts = [
            hpackHeader(":method", request.method),
            hpackHeader(":scheme", request.url.scheme),
            hpackHeader(":authority", request.url.hostHeader),
            hpackHeader(":path", request.url.target),
        ];
        if (!request.headers.has("accept")) {
            parts.push(hpackHeader("accept", "*/*"));
        }
        for (const { name, value } of request.headers.values()) {
            const normalized = name.toLowerCase();
            if (
                normalized === "connection" ||
                normalized === "upgrade" ||
                normalized === "keep-alive" ||
                normalized === "proxy-connection" ||
                normalized === "transfer-encoding" ||
                (normalized === "te" && value.toLowerCase() !== "trailers")
            ) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `HTTP/2 request header "${name}" is not allowed.`);
            }
            parts.push(hpackHeader(name, value, isHttpSensitiveHeader(name, value, new Set())));
        }
        if (request.body.byteLength > 0) {
            parts.push(hpackHeader("content-length", String(request.body.byteLength)));
        }
        return http2Concat(parts);
    }

    function hpackIndexedHeader(index, dynamicTable) {
        if (index > 0 && index < HTTP2_HPACK_STATIC.length) {
            return HTTP2_HPACK_STATIC[index];
        }
        const dynamicIndex = index - HTTP2_HPACK_STATIC.length;
        if (dynamicIndex >= 0 && dynamicIndex < dynamicTable.length) {
            return dynamicTable[dynamicIndex];
        }
        throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
            "HTTP/2 HPACK header index is invalid.",
        );
    }

    function hpackDynamicEntryBytes(header) {
        return sloppyUtf8ToBytes(header[0]).byteLength + sloppyUtf8ToBytes(header[1]).byteLength + 32;
    }

    function hpackTrimDynamicTable(dynamicTable, maxBytes) {
        let total = 0;
        for (let index = 0; index < dynamicTable.length; index += 1) {
            total += hpackDynamicEntryBytes(dynamicTable[index]);
            if (total > maxBytes) {
                dynamicTable.length = index;
                return;
            }
        }
    }

    function hpackDecodeHeaders(block, dynamicTable, maxDynamicTableBytes = HTTP2_DEFAULT_DYNAMIC_TABLE_BYTES) {
        const headers = [];
        let offset = 0;
        while (offset < block.byteLength) {
            const byte = block[offset];
            if ((byte & 0x80) !== 0) {
                const indexed = hpackReadInteger(block, offset, 7);
                offset = indexed.offset;
                headers.push(hpackIndexedHeader(indexed.value, dynamicTable));
                continue;
            }
            if ((byte & 0xe0) === 0x20) {
                const update = hpackReadInteger(block, offset, 5);
                offset = update.offset;
                if (update.value > maxDynamicTableBytes) {
                    throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 HPACK dynamic table size update exceeds the configured limit.",
                    );
                }
                hpackTrimDynamicTable(dynamicTable, update.value);
                continue;
            }
            const incremental = (byte & 0x40) !== 0;
            const prefixBits = incremental ? 6 : 4;
            const nameRef = hpackReadInteger(block, offset, prefixBits);
            offset = nameRef.offset;
            let name;
            if (nameRef.value === 0) {
                const decodedName = hpackReadString(block, offset);
                name = decodedName.value;
                offset = decodedName.offset;
            } else {
                name = hpackIndexedHeader(nameRef.value, dynamicTable)[0];
            }
            const decodedValue = hpackReadString(block, offset);
            offset = decodedValue.offset;
            const header = [name, decodedValue.value];
            headers.push(header);
            if (incremental) {
                if (hpackDynamicEntryBytes(header) <= maxDynamicTableBytes) {
                    dynamicTable.unshift(header);
                    hpackTrimDynamicTable(dynamicTable, maxDynamicTableBytes);
                } else {
                    dynamicTable.length = 0;
                }
            }
        }
        return headers;
    }

    function parseHttp2FrameHeader(bytes, offset) {
        const length = (bytes[offset] << 16) | (bytes[offset + 1] << 8) | bytes[offset + 2];
        return {
            length,
            type: bytes[offset + 3],
            flags: bytes[offset + 4],
            streamId:
                ((bytes[offset + 5] & 0x7f) << 24) |
                (bytes[offset + 6] << 16) |
                (bytes[offset + 7] << 8) |
                bytes[offset + 8],
        };
    }

    function http2HeaderListBytes(headers) {
        let total = 0;
        for (const [name, value] of headers) {
            total += sloppyUtf8ToBytes(name).byteLength + sloppyUtf8ToBytes(value).byteLength + 32;
        }
        return total;
    }

    function parseHttp2Headers(headers, maxHeaderBytes) {
        let status = undefined;
        const regular = [];
        let contentLength = undefined;
        let regularSeen = false;
        if (http2HeaderListBytes(headers) > maxHeaderBytes) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 response headers exceeded the configured limit.",
            );
        }
        for (const [name, value] of headers) {
            if (name === ":status") {
                if (regularSeen || status !== undefined) {
                    throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 response pseudo-headers are malformed.",
                    );
                }
                status = Number(value);
            } else if (name.startsWith(":")) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 response contains an unsupported pseudo-header.",
                );
            } else {
                regularSeen = true;
                if (
                    /[A-Z]/.test(name) ||
                    name.toLowerCase() === "connection" ||
                    name.toLowerCase() === "transfer-encoding"
                ) {
                    throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 response contains an invalid header field.",
                    );
                }
                if (name.toLowerCase() === "content-length") {
                    if (!/^[0-9]+$/.test(value)) {
                        throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE", "HTTP/2 response content-length is invalid.");
                    }
                    const parsedLength = Number(value);
                    if (!Number.isSafeInteger(parsedLength) || parsedLength < 0) {
                        throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE", "HTTP/2 response content-length is out of range.");
                    }
                    if (contentLength !== undefined && contentLength !== parsedLength) {
                        throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE", "HTTP/2 response has conflicting content-length headers.");
                    }
                    contentLength = parsedLength;
                }
                regular.push([name, value]);
            }
        }
        if (!Number.isInteger(status) || status < 100 || status > 599) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 response is missing a valid :status header.",
            );
        }
        return { status, headers: regular, contentLength };
    }

    class Http2ClientSession {
        constructor(connection, pool, originKey) {
            this._connection = connection;
            this._pool = pool;
            this._originKey = originKey;
            this._closed = false;
            this._acceptsStreams = true;
            this._pending = new Uint8Array(0);
            this._dynamicTable = [];
            this._streams = new Map();
            this._nextStreamId = 1;
            this._peerMaxFrameSize = HTTP2_DEFAULT_MAX_FRAME_SIZE;
            this._pendingHeaderStream = 0;
            this._writeQueue = Promise.resolve();
            this._closeListeners = new Set();
            this._reader = undefined;
        }

        get closed() {
            return this._closed;
        }

        get acceptsStreams() {
            return !this._closed && this._acceptsStreams && this._nextStreamId < 0x7fffffff;
        }

        get activeStreamCount() {
            return this._streams.size;
        }

        onClose(listener) {
            if (this._closed) {
                listener();
                return;
            }
            this._closeListeners.add(listener);
        }

        async start() {
            await this._write(
                http2Concat([
                    HTTP2_CLIENT_PREFACE,
                    http2Frame(HTTP2_FRAME_SETTINGS, 0, 0, http2Setting(HTTP2_SETTING_ENABLE_PUSH, 0)),
                ]),
            );
            this._reader = this._readLoop();
            this._reader.catch(() => {});
        }

        async close() {
            if (this._closed) {
                return;
            }
            this._closed = true;
            this._acceptsStreams = false;
            await this._connection.close().catch(() => {});
            this._notifyClosed();
        }

        abort() {
            return this.close();
        }

        _notifyClosed() {
            const listeners = Array.from(this._closeListeners);
            this._closeListeners.clear();
            for (const listener of listeners) {
                listener();
            }
        }

        async _write(bytes) {
            if (this._closed) {
                throw new Error("SLOPPY_E_NET_CONNECTION_CLOSED");
            }
            const write = this._writeQueue.then(() => this._connection.write(bytes));
            this._writeQueue = write.catch(() => {});
            await write;
        }

        _newStream(request) {
            if (!this.acceptsStreams) {
                throw new Error("SLOPPY_E_NET_CONNECTION_CLOSED");
            }
            const streamId = this._nextStreamId;
            this._nextStreamId += 2;
            const stream = {
                streamId,
                request,
                headerBlocks: [],
                dataChunks: [],
                headerBlockBytes: 0,
                totalBodyBytes: 0,
                response: undefined,
                headerBlockEndsStream: false,
                settled: false,
                resolve: undefined,
                reject: undefined,
            };
            stream.promise = new Promise((resolve, reject) => {
                stream.resolve = resolve;
                stream.reject = reject;
            });
            this._streams.set(streamId, stream);
            return stream;
        }

        async request(request, lifecycle) {
            const stream = this._newStream(request);
            const previousAbort = lifecycle.abort;
            lifecycle.connection = this;
            lifecycle.abort = (reason) => {
                const error = reason === "timeout" ? httpRequestTimeoutError() : httpRequestCancelledError();
                this.cancelStream(stream.streamId, error).catch(() => {});
            };
            try {
                const headers = hpackEncodeRequestHeaders(request);
                const frames = http2HeaderFrames(
                    stream.streamId,
                    headers,
                    request.body.byteLength === 0,
                    this._peerMaxFrameSize,
                );
                frames.push(...http2DataFrames(stream.streamId, request.body));
                await this._write(http2Concat(frames));
                return await stream.promise;
            } catch (error) {
                this._finishStream(stream.streamId, undefined, error);
                throw error;
            } finally {
                if (lifecycle.connection === this) {
                    lifecycle.connection = undefined;
                }
                if (lifecycle.abort !== previousAbort) {
                    lifecycle.abort = previousAbort;
                }
            }
        }

        async cancelStream(streamId, error) {
            if (!this._streams.has(streamId)) {
                return;
            }
            this._finishStream(streamId, undefined, error);
            if (!this._closed) {
                await this._write(http2RstStreamFrame(streamId)).catch(() => {});
            }
        }

        _finishStream(streamId, response, error) {
            const stream = this._streams.get(streamId);
            if (stream === undefined || stream.settled) {
                return;
            }
            stream.settled = true;
            this._streams.delete(streamId);
            if (error !== undefined) {
                stream.reject(error);
            } else {
                stream.resolve(response);
            }
            this._pool?.releaseHttp2(this._originKey, this);
        }

        _failAll(error) {
            for (const streamId of Array.from(this._streams.keys())) {
                this._finishStream(streamId, undefined, error);
            }
        }

        async _readLoop() {
            try {
                while (!this._closed) {
                    const frame = await this._readFrame();
                    await this._handleFrame(frame);
                }
            } catch (error) {
                if (!this._closed) {
                    this._failAll(error);
                }
            } finally {
                this._closed = true;
                this._acceptsStreams = false;
                this._failAll(new Error("SLOPPY_E_NET_CONNECTION_CLOSED"));
                await this._connection.close().catch(() => {});
                this._notifyClosed();
            }
        }

        async _readFrame() {
            while (this._pending.byteLength < 9) {
                const chunk = await this._connection.read({ maxBytes: 8192 });
                this._pending = http2Concat([this._pending, chunk]);
            }
            const frame = parseHttp2FrameHeader(this._pending, 0);
            if (frame.length > HTTP2_DEFAULT_MAX_FRAME_SIZE) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 peer frame exceeded the local max frame size.",
                );
            }
            while (this._pending.byteLength < 9 + frame.length) {
                const chunk = await this._connection.read({ maxBytes: 8192 });
                this._pending = http2Concat([this._pending, chunk]);
            }
            const payload = this._pending.slice(9, 9 + frame.length);
            this._pending = this._pending.slice(9 + frame.length);
            return { ...frame, payload };
        }

        async _handleFrame(frame) {
            if (frame.type === HTTP2_FRAME_SETTINGS) {
                await this._handleSettings(frame);
                return;
            }
            if (frame.type === HTTP2_FRAME_PING) {
                await this._handlePing(frame);
                return;
            }
            if (frame.type === HTTP2_FRAME_GOAWAY) {
                this._acceptsStreams = false;
                this._failAll(
                    httpClientError(
                        "SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED",
                        "HTTP/2 peer sent GOAWAY before the response completed.",
                    ),
                );
                await this.close();
                return;
            }
            const stream = this._streams.get(frame.streamId);
            if (stream === undefined) {
                return;
            }
            if (frame.type === HTTP2_FRAME_RST_STREAM) {
                this._finishStream(
                    frame.streamId,
                    undefined,
                    httpClientError(
                        "SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED",
                        "HTTP/2 stream was reset by the peer.",
                    ),
                );
                return;
            }
            if (frame.type === HTTP2_FRAME_HEADERS || frame.type === HTTP2_FRAME_CONTINUATION) {
                await this._handleHeaderFrame(stream, frame);
                return;
            }
            if (frame.type === HTTP2_FRAME_DATA) {
                await this._handleDataFrame(stream, frame);
            }
        }

        async _handleSettings(frame) {
            const payload = frame.payload;
            if (frame.streamId !== 0 || ((frame.flags & HTTP2_FLAG_ACK) !== 0 && frame.length !== 0)) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 SETTINGS frame is malformed.",
                );
            }
            if ((frame.flags & HTTP2_FLAG_ACK) !== 0) {
                return;
            }
            if (payload.byteLength % 6 !== 0) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 SETTINGS payload is malformed.",
                );
            }
            for (let offset = 0; offset < payload.byteLength; offset += 6) {
                const id = (payload[offset] << 8) | payload[offset + 1];
                const value =
                    payload[offset + 2] * 0x1000000 +
                    (payload[offset + 3] << 16) +
                    (payload[offset + 4] << 8) +
                    payload[offset + 5];
                if (id === HTTP2_SETTING_MAX_FRAME_SIZE) {
                    if (value < HTTP2_DEFAULT_MAX_FRAME_SIZE || value > 16777215) {
                        throw httpClientError(
                            "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                            "HTTP/2 SETTINGS_MAX_FRAME_SIZE is invalid.",
                        );
                    }
                    this._peerMaxFrameSize = value;
                }
            }
            await this._write(http2Frame(HTTP2_FRAME_SETTINGS, HTTP2_FLAG_ACK, 0));
        }

        async _handlePing(frame) {
            if (frame.streamId !== 0 || frame.length !== 8) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 PING frame is malformed.",
                );
            }
            if ((frame.flags & HTTP2_FLAG_ACK) === 0) {
                await this._write(http2Frame(HTTP2_FRAME_PING, HTTP2_FLAG_ACK, 0, frame.payload));
            }
        }

        async _handleHeaderFrame(stream, frame) {
            let payload = frame.payload;
            if (frame.type === HTTP2_FRAME_CONTINUATION) {
                if (this._pendingHeaderStream !== frame.streamId) {
                    throw httpClientError(
                        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 CONTINUATION frame is out of sequence.",
                    );
                }
            } else if (this._pendingHeaderStream !== 0) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 response started a new header block before END_HEADERS.",
                );
            }
            if ((frame.flags & 0x8) !== 0) {
                payload = http2UnpadPayload(payload);
            }
            if (frame.type === HTTP2_FRAME_HEADERS && (frame.flags & 0x20) !== 0) {
                if (payload.byteLength < 5) {
                    throw httpClientError(
                        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 response priority field is incomplete.",
                    );
                }
                payload = payload.slice(5);
            }
            stream.headerBlockBytes += payload.byteLength;
            if (stream.headerBlockBytes > stream.request.maxHeaderBytes) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 response headers exceeded the configured limit.",
                );
            }
            stream.headerBlocks.push(payload);
            stream.headerBlockEndsStream =
                stream.headerBlockEndsStream || (frame.flags & HTTP2_FLAG_END_STREAM) !== 0;
            if ((frame.flags & HTTP2_FLAG_END_HEADERS) === 0) {
                this._pendingHeaderStream = frame.streamId;
                return;
            }
            const blockEndedStream = stream.headerBlockEndsStream;
            const decoded = hpackDecodeHeaders(http2Concat(stream.headerBlocks), this._dynamicTable);
            const parsed = parseHttp2Headers(decoded, stream.request.maxHeaderBytes);
            if (parsed.status >= 100 && parsed.status < 200) {
                if (blockEndedStream) {
                    throw httpClientError(
                        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 informational response ended the stream.",
                    );
                }
            } else if (stream.response === undefined) {
                stream.response = parsed;
            }
            stream.headerBlocks.length = 0;
            stream.headerBlockBytes = 0;
            stream.headerBlockEndsStream = false;
            this._pendingHeaderStream = 0;
            if (blockEndedStream && stream.response !== undefined) {
                this._finishStream(stream.streamId, this._buildResponse(stream));
            }
        }

        async _handleDataFrame(stream, frame) {
            let payload = frame.payload;
            if (stream.response === undefined) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 response DATA arrived before final response headers.",
                );
            }
            if ((frame.flags & 0x8) !== 0) {
                payload = http2UnpadPayload(payload);
            }
            stream.totalBodyBytes += payload.byteLength;
            const bodyForbidden =
                stream.request.method === "HEAD" || isHttpBodyForbiddenStatus(stream.response.status);
            if (bodyForbidden && payload.byteLength !== 0) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 response included DATA for a body-forbidden response.",
                );
            }
            if (
                stream.response.contentLength !== undefined &&
                stream.totalBodyBytes > stream.response.contentLength
            ) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 response body exceeded declared content-length.",
                );
            }
            if (stream.totalBodyBytes > stream.request.maxResponseBytes) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT",
                    "HTTP response body exceeded the configured limit.",
                );
            }
            if (payload.byteLength > 0) {
                await this._write(
                    http2Concat([
                        http2WindowUpdateFrame(0, payload.byteLength),
                        http2WindowUpdateFrame(stream.streamId, payload.byteLength),
                    ]),
                );
                stream.dataChunks.push(payload);
            }
            if ((frame.flags & HTTP2_FLAG_END_STREAM) !== 0) {
                this._finishStream(stream.streamId, this._buildResponse(stream));
            }
        }

        _buildResponse(stream) {
            const response = stream.response;
            if (response === undefined) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 response ended before response headers were received.",
                );
            }
            const bodyForbidden =
                stream.request.method === "HEAD" || isHttpBodyForbiddenStatus(response.status);
            if (
                !bodyForbidden &&
                response.contentLength !== undefined &&
                stream.totalBodyBytes !== response.contentLength
            ) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 response body length did not match declared content-length.",
                );
            }
            return new HttpClientResponse(
                response.status,
                "",
                new HttpHeaderBag(response.headers),
                bodyForbidden ? new Uint8Array(0) : concatHttpBytes(stream.dataChunks, stream.totalBodyBytes),
                false,
            );
        }
    }

    async function readHttp2Response(connection, request) {
        let pending = new Uint8Array(0);
        const dynamicTable = [];
        const headerBlocks = [];
        const dataChunks = [];
        let headerBlockBytes = 0;
        let totalBodyBytes = 0;
        let response = undefined;
        let pendingHeaderStream = 0;
        let headerBlockEndsStream = false;
        let peerMaxFrameSize = HTTP2_DEFAULT_MAX_FRAME_SIZE;
        let peerMaxHeaderListSize = request.maxHeaderBytes;

        while (true) {
            while (pending.byteLength < 9) {
                const chunk = await connection.read({ maxBytes: 8192 });
                pending = http2Concat([pending, chunk]);
            }
            const frame = parseHttp2FrameHeader(pending, 0);
            if (frame.length > peerMaxFrameSize) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 peer frame exceeded the current max frame size.",
                );
            }
            if (pending.byteLength < 9 + frame.length) {
                const chunk = await connection.read({ maxBytes: 8192 });
                pending = http2Concat([pending, chunk]);
                continue;
            }
            let payload = pending.slice(9, 9 + frame.length);
            pending = pending.slice(9 + frame.length);

            if (frame.type === HTTP2_FRAME_SETTINGS) {
                if (frame.streamId !== 0 || ((frame.flags & HTTP2_FLAG_ACK) !== 0 && frame.length !== 0)) {
                    throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 SETTINGS frame is malformed.",
                    );
                }
                if ((frame.flags & HTTP2_FLAG_ACK) === 0) {
                    if (payload.byteLength % 6 !== 0) {
                        throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                            "HTTP/2 SETTINGS payload is malformed.",
                        );
                    }
                    for (let offset = 0; offset < payload.byteLength; offset += 6) {
                        const id = (payload[offset] << 8) | payload[offset + 1];
                        const value =
                            payload[offset + 2] * 0x1000000 +
                            (payload[offset + 3] << 16) +
                            (payload[offset + 4] << 8) +
                            payload[offset + 5];
                        if (id === HTTP2_SETTING_MAX_FRAME_SIZE) {
                            if (value < HTTP2_DEFAULT_MAX_FRAME_SIZE || value > 16777215) {
                                throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                                    "HTTP/2 SETTINGS_MAX_FRAME_SIZE is invalid.",
                                );
                            }
                            peerMaxFrameSize = value;
                        } else if (id === HTTP2_SETTING_MAX_HEADER_LIST_SIZE) {
                            peerMaxHeaderListSize = Math.min(peerMaxHeaderListSize, value);
                        }
                    }
                    await connection.write(http2Frame(HTTP2_FRAME_SETTINGS, HTTP2_FLAG_ACK, 0));
                }
                continue;
            }
            if (frame.type === HTTP2_FRAME_PING) {
                if (frame.streamId !== 0 || frame.length !== 8) {
                    throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 PING frame is malformed.",
                    );
                }
                if ((frame.flags & HTTP2_FLAG_ACK) === 0) {
                    await connection.write(http2Frame(HTTP2_FRAME_PING, HTTP2_FLAG_ACK, 0, payload));
                }
                continue;
            }
            if (frame.type === HTTP2_FRAME_GOAWAY) {
                if (response !== undefined) {
                    continue;
                }
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED",
                    "HTTP/2 peer sent GOAWAY before the response completed.",
                );
            }
            if (frame.streamId !== 1) {
                continue;
            }
            if (frame.type === HTTP2_FRAME_RST_STREAM) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED",
                    "HTTP/2 stream was reset by the peer.",
                );
            }
            if (frame.type === HTTP2_FRAME_HEADERS || frame.type === HTTP2_FRAME_CONTINUATION) {
                if (frame.type === HTTP2_FRAME_CONTINUATION) {
                    if (pendingHeaderStream !== frame.streamId) {
                        throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                            "HTTP/2 CONTINUATION frame is out of sequence.",
                        );
                    }
                } else if (pendingHeaderStream !== 0) {
                    throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 response started a new header block before END_HEADERS.",
                    );
                }
                if ((frame.flags & 0x8) !== 0) {
                    payload = http2UnpadPayload(payload);
                }
                if (frame.type === HTTP2_FRAME_HEADERS && (frame.flags & 0x20) !== 0) {
                    if (payload.byteLength < 5) {
                        throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                            "HTTP/2 response priority field is incomplete.",
                        );
                    }
                    payload = payload.slice(5);
                }
                headerBlockBytes += payload.byteLength;
                if (headerBlockBytes > request.maxHeaderBytes) {
                    throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 response headers exceeded the configured limit.",
                    );
                }
                headerBlocks.push(payload);
                headerBlockEndsStream = headerBlockEndsStream || (frame.flags & HTTP2_FLAG_END_STREAM) !== 0;
                if ((frame.flags & HTTP2_FLAG_END_HEADERS) !== 0) {
                    const blockEndedStream = headerBlockEndsStream;
                    const decoded = hpackDecodeHeaders(http2Concat(headerBlocks), dynamicTable);
                    const parsed = parseHttp2Headers(decoded, peerMaxHeaderListSize);
                    if (parsed.status >= 100 && parsed.status < 200) {
                        if (blockEndedStream) {
                            throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                                "HTTP/2 informational response ended the stream.",
                            );
                        }
                    } else if (response === undefined) {
                        response = parsed;
                    }
                    headerBlocks.length = 0;
                    headerBlockBytes = 0;
                    pendingHeaderStream = 0;
                    headerBlockEndsStream = false;
                    if (blockEndedStream && response !== undefined) {
                        break;
                    }
                } else {
                    pendingHeaderStream = frame.streamId;
                }
                if (headerBlockEndsStream && pendingHeaderStream === 0) {
                    break;
                }
                continue;
            }
            if (frame.type === HTTP2_FRAME_DATA) {
                if (response === undefined) {
                    throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 response DATA arrived before final response headers.",
                    );
                }
                if ((frame.flags & 0x8) !== 0) {
                    payload = http2UnpadPayload(payload);
                }
                totalBodyBytes += payload.byteLength;
                if (response !== undefined) {
                    const bodyForbidden = request.method === "HEAD" || isHttpBodyForbiddenStatus(response.status);
                    if (bodyForbidden && payload.byteLength !== 0) {
                        throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                            "HTTP/2 response included DATA for a body-forbidden response.",
                        );
                    }
                    if (response.contentLength !== undefined && totalBodyBytes > response.contentLength) {
                        throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                            "HTTP/2 response body exceeded declared content-length.",
                        );
                    }
                }
                if (totalBodyBytes > request.maxResponseBytes) {
                    throw httpClientError("SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT",
                        "HTTP response body exceeded the configured limit.",
                    );
                }
                if (payload.byteLength > 0) {
                    await connection.write(
                        http2Concat([
                            http2WindowUpdateFrame(0, payload.byteLength),
                            http2WindowUpdateFrame(1, payload.byteLength),
                        ]),
                    );
                }
                dataChunks.push(payload);
                if ((frame.flags & HTTP2_FLAG_END_STREAM) !== 0) {
                    break;
                }
            }
        }

        if (response === undefined) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 response ended before response headers were received.",
            );
        }

        const bodyForbidden = request.method === "HEAD" || isHttpBodyForbiddenStatus(response.status);
        if (!bodyForbidden && response.contentLength !== undefined && totalBodyBytes !== response.contentLength) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 response body length did not match declared content-length.",
            );
        }
        return new HttpClientResponse(
            response.status,
            "",
            new HttpHeaderBag(response.headers),
            bodyForbidden ? new Uint8Array(0) : concatHttpBytes(dataChunks, totalBodyBytes),
            false,
        );
    }

    async function connectHttp2Transport(request, explicitH2) {
        assertHttpNetworkAllowed(request.network, request.url);
        if (request.url.scheme === "http") {
            const bridge = sloppyNativeNet("request");
            const remainingMs = httpRemainingMs(request.expiresAtMs);
            if (remainingMs <= 0) {
                throw httpRequestTimeoutError();
            }
            return {
                connection: new TcpConnection(
                    bridge,
                    await bridge.connect({
                        host: request.url.host,
                        port: request.url.port,
                        timeoutMs: remainingMs === Infinity ? request.timeoutMs : remainingMs,
                        noDelay: true,
                    }),
                ),
                protocol: "h2c",
            };
        }
        const bridge = sloppyNativeNet("request");
        const remainingMs = httpRemainingMs(request.expiresAtMs);
        if (remainingMs <= 0) {
            throw httpRequestTimeoutError();
        }
        if (typeof bridge.connectTls !== "function") {
            throw httpClientTlsUnavailableError();
        }
        if (bridge.tlsAlpn !== true) {
            if (explicitH2) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE",
                    "HTTP/2 over TLS requires an ALPN-capable outbound TLS bridge.",
                );
            }
            return undefined;
        }
        assertHttpTlsBridgeCapabilities(bridge, request.tls, request.operation);
        const handle = await bridge.connectTls({
            host: request.url.host,
            port: request.url.port,
            timeoutMs: remainingMs === Infinity ? request.timeoutMs : remainingMs,
            noDelay: true,
            serverName: request.url.host,
            tls: request.tls,
            alpnProtocols: ["h2", "http/1.1"],
        });
        const connection = new TcpConnection(bridge, handle);
        if (handle.selectedProtocol !== "h2") {
            if (explicitH2) {
                await connection.close().catch(() => {});
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED",
                    "HTTP/2 TLS connection did not negotiate h2 with ALPN.",
                );
            }
            return { connection, protocol: "http/1.1" };
        }
        return { connection, protocol: "h2" };
    }

    async function openHttp2ClientSession(request, pool, originKey) {
        const transport = await connectHttp2Transport(request, true);
        const session = new Http2ClientSession(transport.connection, pool, originKey);
        try {
            await session.start();
            return session;
        } catch (error) {
            await session.close().catch(() => {});
            throw error;
        }
    }

    async function sendHttp1RequestOnConnection(request, connection, lifecycle, keepAlive) {
        lifecycle.connection = connection;
        try {
            await connection.write(serializeHttpRequest(request, keepAlive));
            return await readHttpResponse(connection, request);
        } finally {
            if (lifecycle.connection === connection) {
                lifecycle.connection = undefined;
            }
        }
    }

    async function sendHttp2RequestOnce(request, pool, lifecycle) {
        let session;
        try {
            if (request.protocol === "h2c" && request.url.scheme !== "http") {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    "HTTP/2 h2c requires an http:// URL.",
                );
            }
            if (request.protocol === "h2" && request.url.scheme !== "https") {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    "HTTP/2 h2 requires an https:// URL.",
                );
            }
            const originKey = httpOriginKey(request.url);
            if (pool !== undefined) {
                const lease = await pool.acquireHttp2(originKey, () =>
                    openHttp2ClientSession(request, pool, originKey),
                );
                return await lease.session.request(request, lifecycle);
            }
            session = await openHttp2ClientSession(request, undefined, originKey);
            return await session.request(request, lifecycle);
        } finally {
            if (pool === undefined && session !== undefined) {
                await session.close().catch(() => {});
            }
        }
    }

    async function sendHttpAutoTlsRequestOnce(request, pool, lifecycle) {
        const originKey = httpOriginKey(request.url);
        const existing = pool?.peekHttp2(originKey);
        if (existing !== undefined) {
            return await existing.request(request, lifecycle);
        }
        const transport = await connectHttp2Transport(request, false);
        if (transport === undefined) {
            return undefined;
        }
        if (transport.protocol !== "h2") {
            try {
                return await sendHttp1RequestOnConnection(request, transport.connection, lifecycle, false);
            } finally {
                await transport.connection.close().catch(() => {});
            }
        }

        const session = new Http2ClientSession(transport.connection, pool, originKey);
        try {
            await session.start();
            if (pool !== undefined) {
                pool.adoptHttp2(originKey, session);
                return await session.request(request, lifecycle);
            } else {
                try {
                    return await session.request(request, lifecycle);
                } finally {
                    await session.close().catch(() => {});
                }
            }
        } catch (error) {
            await session.close().catch(() => {});
            throw error;
        }
    }


    function normalizeHttpOptionsObject(value, operation) {
        if (value === undefined || value === null) {
            return undefined;
        }
        if (!isPlainObject(value)) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} options must be a plain object.`);
        }
        return value;
    }

    async function normalizeHttpRequest(baseOptions, request, options, defaultMethod) {
        const operation = "HttpClient.request";
        const requestOptions = typeof request === "string" ? normalizeHttpOptionsObject(options, operation) : undefined;
        const requestObject = typeof request === "string" ? { ...(requestOptions ?? {}), url: request } : request;
        if (!isPlainObject(requestObject)) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", "HttpClient.request requires a request object or URL string.");
        }
        const url = resolveHttpUrl(baseOptions, requestObject.url, operation);
        const method = normalizeHttpMethod(defaultMethod ?? requestObject.method, operation);
        const headers = new Map();
        appendHttpHeaders(headers, baseOptions?.headers, operation);
        appendHttpHeaders(headers, requestObject.headers, operation);
        const redirects = normalizeHttpRedirectPolicy(baseOptions, requestObject, operation);
        const network = normalizeHttpNetworkPolicy(baseOptions, requestObject, operation);
        const protocol = normalizeHttpProtocol(baseOptions, requestObject, operation);
        const tls = normalizeHttpTlsOptions(
            requestObject.tls === undefined ? baseOptions?.tls : requestObject.tls,
            operation,
        );
        assertHttpTlsAllowedForScheme(url, tls, operation);
        const maxRequestBytes =
            parseHttpSize(requestObject.maxRequestBytes ?? baseOptions?.maxRequestBytes, operation) ??
            HTTP_CLIENT_DEFAULT_MAX_REQUEST_BYTES;
        const maxResponseBytes =
            parseHttpSize(requestObject.maxResponseBytes ?? baseOptions?.maxResponseBytes, operation) ??
            HTTP_CLIENT_DEFAULT_MAX_RESPONSE_BYTES;
        const maxHeaderBytes =
            parseHttpSize(requestObject.maxHeaderBytes ?? baseOptions?.maxHeaderBytes, operation) ??
            HTTP_CLIENT_DEFAULT_MAX_HEADER_BYTES;
        const timeoutMs = requestObject.timeoutMs ?? baseOptions?.timeoutMs;
        if (timeoutMs !== undefined && (!Number.isFinite(timeoutMs) || timeoutMs < 0)) {
            throw httpClientError("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS", `${operation} timeoutMs must be a non-negative number.`);
        }
        const deadlineMs = httpDeadlineRemainingMs(requestObject.deadline ?? baseOptions?.deadline, operation);
        const signal = requestObject.signal ?? baseOptions?.signal;
        if (signal !== undefined && !isHttpCancellationSignal(signal)) {
            throw httpClientError(
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} signal must be a Sloppy cancellation signal or AbortSignal-like object.`,
            );
        }
        const timeoutBudgetMs = timeoutMs === undefined ? Infinity : Math.ceil(timeoutMs);
        const timeoutDelayMs = Math.min(timeoutBudgetMs, Math.ceil(deadlineMs));
        const expiresAtMs = timeoutDelayMs === Infinity ? Infinity : Date.now() + timeoutDelayMs;
        const body = await normalizeHttpBody(requestObject, headers, operation, maxRequestBytes, {
            signal,
            expiresAtMs,
        });
        const remainingTimeoutMs = httpRemainingMs(expiresAtMs);
        return Object.freeze({
            url,
            method,
            headers,
            body,
            signal,
            timeoutMs: timeoutBudgetMs === Infinity ? undefined : timeoutBudgetMs,
            timeoutDelayMs: remainingTimeoutMs,
            maxHeaderBytes,
            maxRequestBytes,
            maxResponseBytes,
            redirects,
            network,
            protocol,
            tls,
            operation,
            expiresAtMs,
        });
    }

    function serializeHttpRequest(request, keepAlive = false) {
        const lines = [
            `${request.method} ${request.url.target} HTTP/1.1`,
            `Host: ${request.url.hostHeader}`,
            keepAlive ? "Connection: keep-alive" : "Connection: close",
        ];
        if (!request.headers.has("accept")) {
            lines.push("Accept: */*");
        }
        for (const { name, value } of request.headers.values()) {
            lines.push(`${name}: ${value}`);
        }
        if (request.body.byteLength > 0) {
            lines.push(`Content-Length: ${request.body.byteLength}`);
        }
        lines.push("", "");
        const head = sloppyUtf8ToBytes(lines.join("\r\n"));
        const bytes = new Uint8Array(head.byteLength + request.body.byteLength);
        bytes.set(head, 0);
        bytes.set(request.body, head.byteLength);
        return bytes;
    }

    function mapHttpTransportError(error) {
        const message = String(error?.message ?? error);
        if (message.includes("SLOPPY_E_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH")) {
            return httpClientError("SLOPPY_E_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH", "HTTP client TLS hostname verification failed.", { cause: error });
        }
        if (message.includes("SLOPPY_E_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED")) {
            return httpClientError("SLOPPY_E_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED", "HTTP client TLS certificate validation failed.", { cause: error });
        }
        if (message.includes("SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE")) {
            return httpClientError("SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE", "HTTP client TLS backend is unavailable.", { cause: error });
        }
        if (message.includes("SLOPPY_E_NET_DNS_FAILURE")) {
            return httpClientError("SLOPPY_E_HTTP_CLIENT_DNS_FAILED", "HTTP client DNS resolution failed.", { cause: error });
        }
        if (message.includes("SLOPPY_E_NET_CONNECT_TIMEOUT")) {
            return httpClientError("SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT", "HTTP client request timed out.", { cause: error });
        }
        if (message.includes("SLOPPY_E_NET_CONNECT_CANCELLED")) {
            return httpClientError("SLOPPY_E_HTTP_CLIENT_REQUEST_CANCELLED", "HTTP client request was cancelled.", { cause: error });
        }
        return httpClientError("SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED", "HTTP client transport operation failed.", { cause: error });
    }

    function httpClientTlsUnavailableError() {
        return httpClientError(
            "SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE",
            "HTTPS requires the private outbound TLS bridge.",
        );
    }

    function httpErrorMessageChain(error) {
        let text = "";
        let current = error;
        for (let depth = 0; depth < 5 && current !== undefined && current !== null; depth += 1) {
            text += ` ${String(current.message ?? current)}`;
            current = current.cause;
        }
        return text;
    }

    function isSafeHttpRetryMethod(method) {
        return method === "GET" || method === "HEAD";
    }

    function isStalePooledConnectionError(error) {
        const message = httpErrorMessageChain(error);
        return (
            message.includes("SLOPPY_E_NET_CONNECTION_CLOSED") ||
            message.includes("ECONNRESET") ||
            message.includes("EPIPE")
        );
    }

    async function sendHttpRequestOnce(request, pool, lifecycle) {
        if (request.protocol === "h2" || request.protocol === "h2c") {
            return await sendHttp2RequestOnce(request, pool, lifecycle);
        }
        if (request.protocol === "auto" && request.url.scheme === "https") {
            const negotiated = await sendHttpAutoTlsRequestOnce(request, pool, lifecycle);
            if (negotiated !== undefined) {
                return negotiated;
            }
        }
        let connection;
        let originKey;
        let reusable = false;
        let released = false;
        const acquireConnection = async () => {
            assertHttpNetworkAllowed(request.network, request.url);
            const remainingMs = httpRemainingMs(request.expiresAtMs);
            if (remainingMs <= 0) {
                throw httpRequestTimeoutError();
            }
            const bridge = sloppyNativeNet("request");
            const connectOptions = {
                host: request.url.host,
                port: request.url.port,
                timeoutMs: remainingMs === Infinity ? request.timeoutMs : remainingMs,
                noDelay: true,
            };
            if (request.url.scheme === "https") {
                if (typeof bridge.connectTls !== "function") {
                    throw httpClientTlsUnavailableError();
                }
                assertHttpTlsBridgeCapabilities(bridge, request.tls, request.operation);
                return new TcpConnection(
                    bridge,
                    await bridge.connectTls({
                        ...connectOptions,
                        serverName: request.url.host,
                        tls: request.tls,
                    }),
                );
            }
            return new TcpConnection(bridge, await bridge.connect(connectOptions));
        };

        for (let attempt = 0; attempt < 2; attempt += 1) {
            connection = undefined;
            reusable = false;
            released = false;
            let reused = false;
            try {
                originKey = httpOriginKey(request.url);
                if (pool === undefined) {
                    connection = await acquireConnection();
                } else {
                    const lease = await pool.acquire(originKey, acquireConnection);
                    connection = lease.connection;
                    reused = lease.reused;
                }
                lifecycle.connection = connection;
                await connection.write(serializeHttpRequest(request, pool !== undefined));
                const response = await readHttpResponse(connection, request);
                reusable = response._connectionReusable === true;
                return response;
            } catch (error) {
                if (
                    connection !== undefined &&
                    pool !== undefined &&
                    reused &&
                    attempt === 0 &&
                    isSafeHttpRetryMethod(request.method) &&
                    isStalePooledConnectionError(error)
                ) {
                    released = true;
                    if (lifecycle.connection === connection) {
                        lifecycle.connection = undefined;
                    }
                    await pool.release(originKey, connection, false).catch(() => {});
                    continue;
                }
                throw error;
            } finally {
                if (lifecycle.connection === connection) {
                    lifecycle.connection = undefined;
                }
                if (connection !== undefined && !released) {
                    released = true;
                    if (pool === undefined) {
                        await connection.close().catch(() => {});
                    } else {
                        await pool.release(originKey, connection, reusable).catch(() => {});
                    }
                }
            }
        }
        throw httpClientError("SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED", "HTTP client transport operation failed.");
    }

    async function sendHttpRequestWithRedirects(normalized, pool, lifecycle) {
        if (typeof globalThis.__sloppy?.net?.connect !== "function") {
            return await httpClientUnavailable("request");
        }
        let current = normalized;
        const visited = new Set([httpUrlToString(current.url)]);
        let redirectCount = 0;
        while (true) {
            const response = await sendHttpRequestOnce(current, pool, lifecycle);
            if (!current.redirects.enabled || !isHttpRedirectStatus(response.status)) {
                return response;
            }
            const location = response.headers.get("location");
            if (location === null) {
                return response;
            }
            if (redirectCount >= current.redirects.max) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_MAX_REDIRECTS_EXCEEDED", "HTTP client exceeded the configured redirect limit.");
            }
            if (current.method !== "GET" && current.method !== "HEAD" && !current.redirects.allowPost) {
                throw httpClientError(
                    "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    "HTTP client redirect for request body methods requires redirects.allowPost.",
                );
            }
            const nextUrl = resolveHttpRedirectUrl(current.url, location, current.operation);
            const nextUrlText = httpUrlToString(nextUrl);
            if (visited.has(nextUrlText)) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_REDIRECT_LOOP", "HTTP client detected a redirect loop.");
            }
            visited.add(nextUrlText);
            redirectCount += 1;
            const headers = cloneHttpHeaders(current.headers);
            if (httpOriginKey(nextUrl) !== httpOriginKey(current.url)) {
                stripHttpSensitiveHeaders(headers, current.redirects);
            }
            let method = current.method;
            let body = current.body;
            const hasBody = current.body.byteLength > 0;
            if (
                current.method !== "HEAD" &&
                (response.status === 303 ||
                    ((response.status === 301 || response.status === 302) &&
                        (hasBody || current.method === "POST" || current.method === "PUT" || current.method === "PATCH")))
            ) {
                method = "GET";
                body = new Uint8Array(0);
                headers.delete("content-length");
                headers.delete("content-type");
            }
            current = Object.freeze({ ...current, url: nextUrl, headers, method, body });
        }
    }

    async function sendHttpRequest(baseOptions, request, options = undefined, defaultMethod = undefined, pool = undefined) {
        const normalized = await normalizeHttpRequest(baseOptions, request, options, defaultMethod);
        let timeoutId;
        let timedOut = false;
        let cancelled = false;
        let cancelReason;
        let cleanupCancellation = () => {};
        const lifecycle = { connection: undefined };
        const abortLifecycle = (reason) => {
            if (typeof lifecycle.abort === "function") {
                lifecycle.abort(reason);
                return;
            }
            lifecycle.connection?.abort().catch(() => {});
        };
        const run = async () => {
            return await sendHttpRequestWithRedirects(normalized, pool, lifecycle);
        };

        try {
            if (isHttpCancellationSignal(normalized.signal) && normalized.signal.aborted) {
                cancelled = true;
                cancelReason = normalized.signal.reason;
                throw httpRequestCancelledError();
            }
            if (normalized.timeoutDelayMs <= 0) {
                throw httpRequestTimeoutError();
            }
            if (normalized.timeoutDelayMs === Infinity && normalized.signal === undefined) {
                return await run();
            }
            return await Promise.race([
                run(),
                new Promise((_, reject) => {
                cleanupCancellation = subscribeHttpCancellation(normalized.signal, (reason) => {
                    cancelled = true;
                    cancelReason = reason;
                    abortLifecycle("cancel");
                    reject(httpRequestCancelledError());
                });
                if (normalized.timeoutDelayMs !== Infinity) {
                    timeoutId = setTimeout(() => {
                        timedOut = true;
                        abortLifecycle("timeout");
                        reject(httpRequestTimeoutError());
                    }, normalized.timeoutDelayMs);
                }
                }),
            ]);
        } catch (error) {
            if (String(error?.message ?? "").startsWith("SLOPPY_E_HTTP_CLIENT_")) {
                throw error;
            }
            if (cancelled) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_REQUEST_CANCELLED", "HTTP client request was cancelled.", {
                    cause: error,
                    reason: cancelReason,
                });
            }
            if (timedOut) {
                throw httpClientError("SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT", "HTTP client request timed out.", {
                    cause: error,
                });
            }
            throw mapHttpTransportError(error);
        } finally {
            cleanupCancellation();
            if (timeoutId !== undefined) {
                clearTimeout(timeoutId);
            }
        }
    }

    function withHttpDefaultHeader(options, name, value) {
        const normalizedOptions = normalizeHttpOptionsObject(options, "HttpClient.request");
        const merged = { ...(normalizedOptions ?? {}) };
        if (normalizedOptions?.headers !== undefined && !isPlainObject(normalizedOptions.headers)) {
            return merged;
        }
        const headers = { ...(isPlainObject(normalizedOptions?.headers) ? normalizedOptions.headers : {}) };
        const normalizedName = name.toLowerCase();
        const hasHeader = Object.keys(headers).some((key) => key.toLowerCase() === normalizedName);
        if (!hasHeader) {
            headers[name] = value;
        }
        merged.headers = headers;
        return merged;
    }

    async function readHttpResponseBody(response, kind) {
        if (kind === "json") {
            return await response.json();
        }
        if (kind === "text") {
            return await response.text();
        }
        return await response.bytes();
    }

    async function sendHttpBodyRequest(baseOptions, url, options, kind, pool = undefined) {
        const requestOptions =
            kind === "json" ? withHttpDefaultHeader(options, "Accept", "application/json") : options;
        const response = await sendHttpRequest(baseOptions, url, requestOptions, "GET", pool);
        return await readHttpResponseBody(response, kind);
    }

    function postJsonRequest(baseOptions, url, value, options = undefined, pool = undefined) {
        const normalizedOptions = normalizeHttpOptionsObject(options, "HttpClient.request");
        return sendHttpRequest(baseOptions, url, { ...(normalizedOptions ?? {}), json: value }, "POST", pool);
    }

    function createHttpClientFacade(baseOptions = undefined) {
        const rawBaseOptions = normalizeHttpOptionsObject(baseOptions, "HttpClient.create");
        const normalizedTls = normalizeHttpTlsOptions(rawBaseOptions?.tls, "HttpClient.create");
        const poolOptions = normalizeHttpPoolOptions(rawBaseOptions?.pool, "HttpClient.create");
        baseOptions = createHttpClientBaseOptions(rawBaseOptions, normalizedTls, poolOptions);
        const descriptor = sanitizeHttpClientOptionsDescriptor(
            rawBaseOptions,
            normalizedTls,
            poolOptions,
        );
        const pool = poolOptions === undefined ? undefined : new HttpConnectionPool(poolOptions);
        const client = {
            request(request, options = undefined) {
                return sendHttpRequest(baseOptions, request, options, undefined, pool);
            },
            get(url, options = undefined) {
                return sendHttpRequest(baseOptions, url, options, "GET", pool);
            },
            post(url, options = undefined) {
                return sendHttpRequest(baseOptions, url, options, "POST", pool);
            },
            put(url, options = undefined) {
                return sendHttpRequest(baseOptions, url, options, "PUT", pool);
            },
            patch(url, options = undefined) {
                return sendHttpRequest(baseOptions, url, options, "PATCH", pool);
            },
            delete(url, options = undefined) {
                return sendHttpRequest(baseOptions, url, options, "DELETE", pool);
            },
            head(url, options = undefined) {
                return sendHttpRequest(baseOptions, url, options, "HEAD", pool);
            },
            getJson(url, options = undefined) {
                return sendHttpBodyRequest(baseOptions, url, options, "json", pool);
            },
            postJson(url, value, options = undefined) {
                return postJsonRequest(baseOptions, url, value, options, pool);
            },
            text(url, options = undefined) {
                return sendHttpBodyRequest(baseOptions, url, options, "text", pool);
            },
            json(url, options = undefined) {
                return sendHttpBodyRequest(baseOptions, url, options, "json", pool);
            },
            bytes(url, options = undefined) {
                return sendHttpBodyRequest(baseOptions, url, options, "bytes", pool);
            },
        };
        Object.defineProperty(client, "__sloppyHttpClientOptions", {
            value: descriptor,
            enumerable: false,
        });
        return Object.freeze(client);
    }

    const HttpClient = Object.freeze({
        create(options = undefined) {
            return createHttpClientFacade(options);
        },
        request(request, options = undefined) {
            return sendHttpRequest(undefined, request, options);
        },
        get(url, options = undefined) {
            return sendHttpRequest(undefined, url, options, "GET");
        },
        post(url, options = undefined) {
            return sendHttpRequest(undefined, url, options, "POST");
        },
        put(url, options = undefined) {
            return sendHttpRequest(undefined, url, options, "PUT");
        },
        patch(url, options = undefined) {
            return sendHttpRequest(undefined, url, options, "PATCH");
        },
        delete(url, options = undefined) {
            return sendHttpRequest(undefined, url, options, "DELETE");
        },
        head(url, options = undefined) {
            return sendHttpRequest(undefined, url, options, "HEAD");
        },
        getJson(url, options = undefined) {
            return sendHttpBodyRequest(undefined, url, options, "json");
        },
        postJson(url, value, options = undefined) {
            return postJsonRequest(undefined, url, value, options);
        },
        text(url, options = undefined) {
            return sendHttpBodyRequest(undefined, url, options, "text");
        },
        json(url, options = undefined) {
            return sendHttpBodyRequest(undefined, url, options, "json");
        },
        bytes(url, options = undefined) {
            return sendHttpBodyRequest(undefined, url, options, "bytes");
        },
    });

    class OsError extends Error {
        constructor(code, message) {
            super(`${code}: ${message}`);
            this.name = "OsError";
            this.code = code;
        }
    }

    function sloppyOsError(code, message) {
        return new OsError(code, message);
    }

    function sloppyNativeOs() {
        const bridge = globalThis.__sloppy?.os;
        if (bridge === undefined) {
            throw sloppyOsError(
                "SLOPPY_E_OS_FEATURE_UNAVAILABLE",
                "sloppy/os requires the stdlib.os runtime bridge.",
            );
        }
        return bridge;
    }

    function sloppyOsKey(key, operation) {
        if (typeof key !== "string" || key.length === 0 || key.includes("=") || key.includes("\0")) {
            throw new TypeError(`${operation} requires a non-empty environment key string without '=' or NUL.`);
        }
        return key;
    }

    function sloppyProcessRunArgs(command, args) {
        if (typeof command !== "string" || command.length === 0 || command.includes("\0")) {
            throw new TypeError("OS run command must be a non-empty string without NUL.");
        }
        if (args === undefined) {
            return [];
        }
        if (!Array.isArray(args)) {
            throw new TypeError("OS run args must be an array when provided.");
        }
        return args.map((arg, index) => {
            if (typeof arg !== "string" || arg.includes("\0")) {
                throw new TypeError(`OS run args[${index}] must be a string without NUL.`);
            }
            return arg;
        });
    }

    function sloppyProcessRunOptions(options = undefined) {
        const normalized = {
            capture: "text",
            maxStdoutBytes: 65536,
            maxStderrBytes: 65536,
            timeoutMs: 0,
        };
        if (options === undefined) {
            return normalized;
        }
        if (options === null || typeof options !== "object" || Array.isArray(options)) {
            throw new TypeError("OS run options must be an object when provided.");
        }
        for (const key of Object.keys(options)) {
            if (!["cwd", "env", "capture", "maxStdoutBytes", "maxStderrBytes", "timeoutMs", "deadline", "signal"].includes(key)) {
                throw new TypeError(`OS run does not support option ${key}.`);
            }
        }
        if (options.cwd !== undefined) {
            if (typeof options.cwd !== "string" || options.cwd.includes("\0")) {
                throw new TypeError("OS run cwd must be a string without NUL.");
            }
            normalized.cwd = options.cwd;
        }
        if (options.env !== undefined) {
            if (options.env === null || typeof options.env !== "object" || Array.isArray(options.env)) {
                throw new TypeError("OS run env must be an object when provided.");
            }
            normalized.env = Object.freeze(Object.fromEntries(Object.entries(options.env).map(([key, value]) => {
                sloppyOsKey(key, "OS run env");
                if (typeof value !== "string" || value.includes("\0")) {
                    throw new TypeError(`OS run env.${key} must be a string without NUL.`);
                }
                return [key, value];
            })));
        }
        if (options.capture !== undefined) {
            if (!["none", "text", "bytes"].includes(options.capture)) {
                throw new TypeError('OS run capture must be "none", "text", or "bytes".');
            }
            normalized.capture = options.capture;
        }
        for (const key of ["maxStdoutBytes", "maxStderrBytes", "timeoutMs"]) {
            if (options[key] !== undefined) {
                if (!Number.isFinite(options[key]) || options[key] < 0) {
                    throw new TypeError(`OS run ${key} must be a non-negative number.`);
                }
                normalized[key] = Math.ceil(options[key]);
            }
        }
        if (options.deadline !== undefined && options.deadline !== null) {
            if (typeof options.deadline.remainingMs !== "function") {
                throw new TypeError("OS run deadline must come from sloppy/time Deadline.");
            }
            const remaining = options.deadline.remainingMs();
            if (remaining <= 0) {
                throw sloppyOsError("SLOPPY_E_OS_PROCESS_TIMEOUT", "deadline already expired");
            }
            if (remaining !== Infinity) {
                if (!Number.isFinite(remaining)) {
                    throw new TypeError("OS run deadline remaining time must be finite or Infinity.");
                }
                normalized.timeoutMs = Math.min(normalized.timeoutMs || Infinity, Math.ceil(remaining));
            }
        }
        if (options.signal !== undefined) {
            if (!isCancellationSignal(options.signal)) {
                throw new TypeError("OS run signal must be a cancellation signal.");
            }
        }
        if (options.signal?.aborted === true) {
            throw sloppyOsError("SLOPPY_E_OS_PROCESS_CANCELLED", "process was cancelled");
        }
        return normalized;
    }

    function sloppyProcessStartOptions(options = undefined) {
        const normalized = { stdin: "ignore", stdout: "ignore", stderr: "ignore" };
        if (options === undefined) {
            return normalized;
        }
        if (options === null || typeof options !== "object" || Array.isArray(options)) {
            throw new TypeError("OS start options must be an object when provided.");
        }
        for (const key of Object.keys(options)) {
            if (!["cwd", "env", "stdin", "stdout", "stderr", "deadline", "signal"].includes(key)) {
                throw new TypeError(`OS start does not support option ${key}.`);
            }
        }
        if (options.cwd !== undefined) {
            if (typeof options.cwd !== "string" || options.cwd.includes("\0")) {
                throw new TypeError("OS start cwd must be a string without NUL.");
            }
            normalized.cwd = options.cwd;
        }
        if (options.env !== undefined) {
            if (options.env === null || typeof options.env !== "object" || Array.isArray(options.env)) {
                throw new TypeError("OS start env must be an object when provided.");
            }
            normalized.env = Object.freeze(Object.fromEntries(Object.entries(options.env).map(([key, value]) => {
                sloppyOsKey(key, "OS start env");
                if (typeof value !== "string" || value.includes("\0")) {
                    throw new TypeError(`OS start env.${key} must be a string without NUL.`);
                }
                return [key, value];
            })));
        }
        for (const key of ["stdin", "stdout", "stderr"]) {
            if (options[key] !== undefined) {
                if (!["ignore", "pipe"].includes(options[key])) {
                    throw new TypeError(`OS start ${key} must be "ignore" or "pipe".`);
                }
                normalized[key] = options[key];
            }
        }
        if (options.deadline !== undefined && options.deadline !== null) {
            if (typeof options.deadline.remainingMs !== "function") {
                throw new TypeError("OS start deadline must come from sloppy/time Deadline.");
            }
            if (options.deadline.remainingMs() <= 0) {
                throw sloppyOsError("SLOPPY_E_OS_PROCESS_TIMEOUT", "deadline already expired");
            }
        }
        if (options.signal !== undefined) {
            if (!isCancellationSignal(options.signal)) {
                throw new TypeError("OS start signal must be a cancellation signal.");
            }
            if (options.signal.aborted === true) {
                throw sloppyOsError("SLOPPY_E_OS_PROCESS_CANCELLED", "process was cancelled");
            }
        }
        return normalized;
    }

    function sloppyProcessWaitOptions(options = undefined) {
        const normalized = { timeoutMs: 0 };
        if (options === undefined) {
            return normalized;
        }
        if (options === null || typeof options !== "object" || Array.isArray(options)) {
            throw new TypeError("ProcessHandle.wait options must be an object when provided.");
        }
        for (const key of Object.keys(options)) {
            if (!["timeoutMs", "deadline", "signal"].includes(key)) {
                throw new TypeError(`ProcessHandle.wait does not support option ${key}.`);
            }
        }
        if (options.timeoutMs !== undefined) {
            if (!Number.isFinite(options.timeoutMs) || options.timeoutMs < 0) {
                throw new TypeError("ProcessHandle.wait timeoutMs must be a non-negative number.");
            }
            normalized.timeoutMs = Math.ceil(options.timeoutMs);
        }
        if (options.deadline !== undefined && options.deadline !== null) {
            if (typeof options.deadline.remainingMs !== "function") {
                throw new TypeError("ProcessHandle.wait deadline must come from sloppy/time Deadline.");
            }
            const remaining = options.deadline.remainingMs();
        if (remaining <= 0) {
            throw sloppyOsError("SLOPPY_E_OS_PROCESS_TIMEOUT", "deadline already expired");
        }
        if (remaining !== Infinity) {
            if (!Number.isFinite(remaining)) {
                throw new TypeError("ProcessHandle.wait deadline remaining time must be finite or Infinity.");
            }
            normalized.timeoutMs = Math.min(normalized.timeoutMs || Infinity, Math.ceil(remaining));
        }
        }
        if (options.signal !== undefined) {
            if (!isCancellationSignal(options.signal)) {
                throw new TypeError("ProcessHandle.wait signal must be a cancellation signal.");
            }
            if (options.signal.aborted === true) {
                throw sloppyOsError("SLOPPY_E_OS_PROCESS_CANCELLED", "process was cancelled");
            }
        }
        return normalized;
    }

    function sloppyCallProcessBridge(handle, directName, bridgeName, args, unavailableMessage) {
        if (handle !== null && typeof handle === "object" && typeof handle[directName] === "function") {
            return handle[directName](...args);
        }
        const bridge = sloppyNativeOs();
        if (typeof bridge[bridgeName] === "function") {
            return bridge[bridgeName](handle, ...args);
        }
        throw sloppyOsError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", unavailableMessage);
    }

    class ProcessPipe {
        constructor(handle, name) {
            this._handle = handle;
            this._name = name;
        }

        async read(maxBytes = 65536) {
            if (!Number.isFinite(maxBytes) || maxBytes <= 0) {
                throw new TypeError("Process pipe read maxBytes must be a positive number.");
            }
            const method = this._name === "stderr" ? "readStderr" : "readStdout";
            const bridgeMethod = this._name === "stderr" ? "processReadStderr" : "processReadStdout";
            return sloppyCallProcessBridge(
                this._handle,
                method,
                bridgeMethod,
                [Math.ceil(maxBytes)],
                "OS process pipe bridge is unavailable.",
            );
        }

        async readText(maxBytes = 65536) {
            const value = await this.read(maxBytes);
            if (typeof value === "string") {
                return value;
            }
            return new TextDecoder().decode(value);
        }

        async *readLines(options = undefined) {
            if (options !== undefined && (options === null || typeof options !== "object" || Array.isArray(options))) {
                throw new TypeError("Process pipe readLines options must be an object when provided.");
            }
            const chunkSize = options?.chunkSize ?? 4096;
            const decoder = new TextDecoder();
            let buffered = "";
            for (;;) {
                const value = await this.read(chunkSize);
                const chunk = typeof value === "string" ? value : decoder.decode(value, { stream: true });
                if (chunk.length === 0) {
                    break;
                }
                buffered += chunk;
                for (;;) {
                    const newline = buffered.indexOf("\n");
                    if (newline < 0) {
                        break;
                    }
                    const line = buffered.slice(0, newline).replace(/\r$/, "");
                    buffered = buffered.slice(newline + 1);
                    yield line;
                }
            }
            buffered += decoder.decode();
            if (buffered.length !== 0) {
                yield buffered.replace(/\r$/, "");
            }
        }
    }

    class ProcessInput {
        constructor(handle) {
            this._handle = handle;
        }

        async write(value) {
            if (typeof value !== "string" && !(value instanceof Uint8Array)) {
                throw new TypeError("Process stdin write requires a string or Uint8Array.");
            }
            return sloppyCallProcessBridge(
                this._handle,
                "writeStdin",
                "processWriteStdin",
                [value],
                "OS process stdin bridge is unavailable.",
            );
        }

        async writeText(text) {
            if (typeof text !== "string") {
                throw new TypeError("Process stdin writeText requires a string.");
            }
            return this.write(text);
        }

        async close() {
            return sloppyCallProcessBridge(
                this._handle,
                "closeStdin",
                "processCloseStdin",
                [],
                "OS process stdin bridge is unavailable.",
            );
        }
    }

    class ProcessHandle {
        constructor(handle, options) {
            this._handle = handle;
            this.stdin = options.stdin === "pipe" ? new ProcessInput(handle) : undefined;
            this.stdout = options.stdout === "pipe" ? new ProcessPipe(handle, "stdout") : undefined;
            this.stderr = options.stderr === "pipe" ? new ProcessPipe(handle, "stderr") : undefined;
        }

        async wait(options = undefined) {
            return sloppyCallProcessBridge(
                this._handle,
                "wait",
                "processWait",
                [sloppyProcessWaitOptions(options)],
                "OS process wait bridge is unavailable.",
            );
        }

        async terminate() {
            return sloppyCallProcessBridge(
                this._handle,
                "terminate",
                "processTerminate",
                [],
                "OS process terminate bridge is unavailable.",
            );
        }

        async kill() {
            return sloppyCallProcessBridge(
                this._handle,
                "kill",
                "processKill",
                [],
                "OS process kill bridge is unavailable.",
            );
        }

        async cancel() {
            return sloppyCallProcessBridge(
                this._handle,
                "cancel",
                "processCancel",
                [],
                "OS process cancel bridge is unavailable.",
            );
        }

        async dispose() {
            if (typeof this._handle.dispose === "function") {
                return this._handle.dispose();
            }
            const bridge = sloppyNativeOs();
            if (typeof bridge.processDispose === "function") {
                return bridge.processDispose(this._handle);
            }
            return undefined;
        }
    }

    function sloppyShutdownHandler(handler) {
        if (typeof handler !== "function") {
            throw new TypeError("Signals.onShutdown requires a function.");
        }
        return handler;
    }

    function sloppyShutdownContext(ctx = undefined) {
        const source = ctx === null || typeof ctx !== "object" ? {} : ctx;
        return Object.freeze({
            signal: typeof source.signal === "string" ? source.signal : "shutdown",
            forced: source.forced === true,
            reason: source.reason,
        });
    }

    function sloppySignalHandlerFailure(error) {
        const failure = sloppyOsError("SLOPPY_E_OS_SIGNAL_HANDLER_FAILURE", "shutdown signal handler failed");
        failure.cause = error;
        return failure;
    }

    const System = Object.freeze({
        get platform() {
            return sloppyNativeOs().systemInfo().platform;
        },
        get arch() {
            return sloppyNativeOs().systemInfo().arch;
        },
        get cpuCount() {
            return sloppyNativeOs().systemInfo().cpuCount;
        },
        get tempDirectory() {
            return sloppyNativeOs().systemInfo().tempDirectory;
        },
        get hostname() {
            return sloppyNativeOs().systemInfo().hostname;
        },
        get endOfLine() {
            return sloppyNativeOs().systemInfo().endOfLine;
        },
    });

    const Environment = Object.freeze({
        get(key) {
            key = sloppyOsKey(key, "Environment.get");
            const value = sloppyNativeOs().environmentGet(key);
            return value === undefined ? undefined : String(value);
        },
        has(key) {
            return sloppyNativeOs().environmentHas(sloppyOsKey(key, "Environment.has")) === true;
        },
        list(options = undefined) {
            const prefix = options?.prefix ?? "";
            if (typeof prefix !== "string") {
                throw new TypeError("Environment.list prefix must be a string.");
            }
            return Object.freeze([...sloppyNativeOs().environmentList(prefix)].map(String));
        },
    });

    const Process = Object.freeze({
        async run(command, args = [], options = undefined) {
            const bridge = sloppyNativeOs();
            if (typeof bridge.processRun !== "function") {
                throw sloppyOsError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "OS run bridge is unavailable.");
            }
            return bridge.processRun(command, sloppyProcessRunArgs(command, args), sloppyProcessRunOptions(options));
        },
        async start(command, args = [], options = undefined) {
            const bridge = sloppyNativeOs();
            if (typeof bridge.processStart !== "function") {
                throw sloppyOsError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "OS start bridge is unavailable.");
            }
            const startOptions = sloppyProcessStartOptions(options);
            return new ProcessHandle(
                await bridge.processStart(command, sloppyProcessRunArgs(command, args), startOptions),
                startOptions,
            );
        },
    });

    const Signals = Object.freeze({
        onShutdown(handler) {
            handler = sloppyShutdownHandler(handler);
            const bridge = sloppyNativeOs();
            if (typeof bridge.signalsOnShutdown !== "function") {
                throw sloppyOsError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "OS shutdown signal bridge is unavailable.");
            }
            return bridge.signalsOnShutdown(async (ctx = undefined) => {
                try {
                    await handler(sloppyShutdownContext(ctx));
                } catch (error) {
                    throw sloppySignalHandlerFailure(error);
                }
            });
        },
    });

    class SloppyWorkerError extends Error {
        constructor(code, message, options = undefined) {
            super(`${code}: ${message}`, options);
            this.name = "SloppyWorkerError";
            this.code = code;
        }
    }

    function sloppyWorkerError(code, message, cause = undefined) {
        return new SloppyWorkerError(code, message, cause === undefined ? undefined : { cause });
    }

    function sloppyWorkerBridgeError(error, fallbackCode, fallbackMessage) {
        if (error instanceof SloppyWorkerError) {
            return error;
        }
        const code = typeof error?.code === "string" && error.code.startsWith("SLOPPY_E_")
            ? error.code
            : fallbackCode;
        const message = typeof error?.message === "string" && error.message.length > 0
            ? error.message
            : fallbackMessage;
        return sloppyWorkerError(code, message, error);
    }

    const sloppyWorkerSerializationMarker = "__sloppyWorkerSerialized";

    function sloppyWorkerTypedArrayBackingStore(view) {
        return Reflect.get(view, "buf" + "fer");
    }

    function sloppyWorkerSerializePayload(value, seen = new Set()) {
        if (value === undefined) {
            return null;
        }
        if (value === null || typeof value === "string" || typeof value === "boolean") {
            return value;
        }
        if (typeof value === "number" && Number.isFinite(value)) {
            return value;
        }
        if (value instanceof ArrayBuffer) {
            return value.slice(0);
        }
        if (ArrayBuffer.isView(value)) {
            const copy = sloppyWorkerTypedArrayBackingStore(value).slice(value.byteOffset, value.byteOffset + value.byteLength);
            if (value instanceof DataView) {
                return new DataView(copy);
            }
            return new value.constructor(copy);
        }
        if (Array.isArray(value)) {
            if (seen.has(value)) {
                throw sloppyWorkerError("SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED", "worker payload contains a cycle");
            }
            seen.add(value);
            const copy = value.map((item) => sloppyWorkerSerializePayload(item, seen));
            seen.delete(value);
            return copy;
        }
        if (value !== null && typeof value === "object" &&
            (Object.getPrototypeOf(value) === Object.prototype || Object.getPrototypeOf(value) === null))
        {
            if (seen.has(value)) {
                throw sloppyWorkerError("SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED", "worker payload contains a cycle");
            }
            if (Object.prototype.hasOwnProperty.call(value, sloppyWorkerSerializationMarker)) {
                throw sloppyWorkerError("SLOPPY_E_WORKER_UNSUPPORTED_PAYLOAD", "worker payload uses a reserved serialization marker");
            }
            seen.add(value);
            const copy = {};
            for (const [key, item] of Object.entries(value)) {
                if (item !== undefined) {
                    copy[key] = sloppyWorkerSerializePayload(item, seen);
                }
            }
            seen.delete(value);
            return copy;
        }
        throw sloppyWorkerError("SLOPPY_E_WORKER_UNSUPPORTED_PAYLOAD", "worker payload type is unsupported");
    }

    function sloppyWorkerName(name, subject) {
        if (typeof name !== "string" || name.length === 0 || name.trim() !== name) {
            throw new TypeError(`${subject} name must be a non-empty stable string.`);
        }
        return name;
    }

    function sloppyWorkerPositive(value, field, fallback) {
        const resolved = value === undefined ? fallback : value;
        if (!Number.isInteger(resolved) || resolved <= 0) {
            throw new TypeError(`${field} must be a positive integer.`);
        }
        return resolved;
    }

    function sloppyWorkerDeadlineRemainingMs(deadline) {
        if (deadline === undefined || deadline === null) {
            return Infinity;
        }
        if (typeof deadline.remainingMs !== "function") {
            throw new TypeError("worker deadline must expose remainingMs().");
        }
        const remaining = Number(deadline.remainingMs());
        return Number.isFinite(remaining) ? Math.max(0, remaining) : Infinity;
    }

    function sloppyWorkerCombineDeadlines(first, second) {
        if (first === undefined || first === null) {
            return second;
        }
        if (second === undefined || second === null) {
            return first;
        }
        return Object.freeze({
            remainingMs() {
                return Math.min(sloppyWorkerDeadlineRemainingMs(first), sloppyWorkerDeadlineRemainingMs(second));
            },
        });
    }

    function sloppyWorkerNormalizeOptions(options = undefined) {
        if (options === undefined || options === null) {
            return { deadline: undefined, signal: undefined };
        }
        if (typeof options !== "object") {
            throw new TypeError("worker operation options must be an object.");
        }
        let timeoutDeadline = undefined;
        if (options.timeoutMs !== undefined) {
            if (!Number.isFinite(options.timeoutMs) || options.timeoutMs < 0) {
                throw new TypeError("worker timeoutMs must be finite and non-negative.");
            }
            timeoutDeadline = Deadline.after(Math.ceil(options.timeoutMs));
        }
        return {
            deadline: sloppyWorkerCombineDeadlines(options.deadline, timeoutDeadline),
            signal: options.signal,
        };
    }

    function sloppyWorkerSignalCancelled(signal) {
        return signal !== null && typeof signal === "object" &&
            (signal.cancelled === true || signal.aborted === true);
    }

    function sloppyWorkerSubscribeSignal(signal, listener) {
        if (signal === null || typeof signal !== "object" || typeof listener !== "function") {
            return () => {};
        }
        if (sloppyWorkerSignalCancelled(signal)) {
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

    function sloppyWorkerRejectForCancellation(reason) {
        if (reason === "deadline" || reason === "timeout") {
            return sloppyWorkerError("SLOPPY_E_WORK_JOB_TIMEOUT", "worker operation timed out");
        }
        return sloppyWorkerError("SLOPPY_E_WORK_JOB_CANCELLED", "worker operation was cancelled");
    }

    function sloppyWorkerContext(options, extra = {}) {
        const controller = new WorkerCancellationController();
        const cleanupParent = sloppyWorkerSubscribeSignal(options.signal, (reason) => controller.cancel(reason));
        const remainingMs = sloppyWorkerDeadlineRemainingMs(options.deadline);
        let timer = undefined;
        if (remainingMs === 0) {
            controller.cancel("deadline");
        } else if (remainingMs !== Infinity) {
            timer = setTimeout(() => controller.cancel("deadline"), Math.ceil(remainingMs));
        }
        return {
            ctx: Object.freeze({
                signal: controller.signal,
                deadline: options.deadline,
                ...extra,
            }),
            cleanup() {
                cleanupParent();
                if (timer !== undefined) {
                    clearTimeout(timer);
                }
            },
        };
    }

    class WorkerCancellationSignal {
        constructor() {
            this.cancelled = false;
            this.aborted = false;
            this.reason = undefined;
            this._listeners = new Set();
            Object.seal(this);
        }

        throwIfCancelled() {
            if (this.cancelled) {
                throw sloppyWorkerError("SLOPPY_E_WORK_JOB_CANCELLED", "worker operation was cancelled");
            }
        }

        addEventListener(type, listener) {
            if (type === "abort" && typeof listener === "function") {
                this._listeners.add(listener);
            }
        }

        removeEventListener(type, listener) {
            if (type === "abort") {
                this._listeners.delete(listener);
            }
        }

        _subscribe(listener) {
            if (this.cancelled) {
                listener(this.reason);
                return () => {};
            }
            this._listeners.add(listener);
            return () => {
                this._listeners.delete(listener);
            };
        }

        _cancel(reason) {
            if (this.cancelled) {
                return false;
            }
            this.cancelled = true;
            this.aborted = true;
            this.reason = reason;
            for (const listener of Array.from(this._listeners)) {
                listener(reason);
            }
            this._listeners.clear();
            return true;
        }
    }

    class WorkerCancellationController {
        constructor() {
            this.signal = new WorkerCancellationSignal();
            Object.freeze(this);
        }

        cancel(reason = "cancelled") {
            return this.signal._cancel(reason);
        }
    }

    class ClassicWorkQueue {
        constructor(name, options = undefined) {
            this.name = sloppyWorkerName(name, "WorkQueue");
            this.maxQueued = sloppyWorkerPositive(options?.maxQueued, "WorkQueue.maxQueued", 1024);
            this.concurrency = sloppyWorkerPositive(options?.concurrency, "WorkQueue.concurrency", 1);
            this.overflow = options?.overflow ?? "reject";
            if (!["reject", "backpressure"].includes(this.overflow)) {
                throw new TypeError("WorkQueue overflow must be \"reject\" or \"backpressure\".");
            }
            this.maxBackpressureWaiters = sloppyWorkerPositive(options?.maxBackpressureWaiters, "WorkQueue.maxBackpressureWaiters", this.maxQueued);
            this._handler = undefined;
            this._queue = [];
            this._waiters = [];
            this._active = 0;
            this._stopped = false;
            this._nextJobId = 1;
            this._stopPromise = undefined;
            this._resolveStop = undefined;
            this.__sloppyWorkerResource = "workQueue";
            Object.seal(this);
        }

        get state() {
            return Object.freeze({ name: this.name, queued: this._queue.length, active: this._active, stopped: this._stopped });
        }

        process(handler) {
            if (typeof handler !== "function") {
                throw new TypeError("WorkQueue.process requires a job handler.");
            }
            if (this._handler !== undefined) {
                throw new TypeError("WorkQueue.process may only be called once.");
            }
            this._handler = handler;
            this._pump();
            return this;
        }

        enqueue(data, options = undefined) {
            if (this._stopped) {
                return Promise.reject(sloppyWorkerError("SLOPPY_E_WORK_QUEUE_STOPPED", "work queue is stopped"));
            }
            if (this._handler === undefined) {
                return Promise.reject(sloppyWorkerError("SLOPPY_E_WORK_QUEUE_STOPPED", "work queue has no processor"));
            }
            return this._enqueuePayload(sloppyWorkerSerializePayload(data), options);
        }

        _enqueuePayload(payload, options = undefined) {
            if (this._stopped) {
                return Promise.reject(sloppyWorkerError("SLOPPY_E_WORK_QUEUE_STOPPED", "work queue is stopped"));
            }
            const submit = () => new Promise((resolve, reject) => {
                if (this._stopped) {
                    reject(sloppyWorkerError("SLOPPY_E_WORK_QUEUE_STOPPED", "work queue is stopped"));
                    return;
                }
                this._queue.push({
                    id: this._nextJobId++,
                    data: payload,
                    options: sloppyWorkerNormalizeOptions(options),
                    resolve,
                    reject,
                });
                this._pump();
            });
            if (this._queue.length >= this.maxQueued && this._active >= this.concurrency) {
                if (this.overflow === "reject") {
                    return Promise.reject(sloppyWorkerError("SLOPPY_E_WORK_QUEUE_FULL", "work queue is full"));
                }
                if (this._waiters.length >= this.maxBackpressureWaiters) {
                    return Promise.reject(sloppyWorkerError("SLOPPY_E_WORK_QUEUE_FULL", "work queue backpressure waiters are full"));
                }
                return new Promise((resolve, reject) => {
                    this._waiters.push({
                        reject,
                        resume: () => submit().then(resolve, reject),
                    });
                });
            }
            return submit();
        }

        async drain() {
            if (this._queue.length === 0 && this._active === 0) {
                return;
            }
            if (this._stopPromise === undefined) {
                this._stopPromise = new Promise((resolve) => {
                    this._resolveStop = resolve;
                });
            }
            await this._stopPromise;
        }

        async stop(options = undefined) {
            this._stopped = true;
            if (options?.drain === false) {
                while (this._queue.length > 0) {
                    this._queue.shift().reject(sloppyWorkerError("SLOPPY_E_WORKER_SHUTDOWN_CANCELLED", "queued job was cancelled by shutdown"));
                }
            }
            const waiterError = options?.drain === false
                ? sloppyWorkerError("SLOPPY_E_WORKER_SHUTDOWN_CANCELLED", "queued job was cancelled by shutdown")
                : sloppyWorkerError("SLOPPY_E_WORK_QUEUE_STOPPED", "work queue is stopped");
            while (this._waiters.length > 0) {
                this._waiters.shift().reject(waiterError);
            }
            await this.drain();
        }

        _pump() {
            while (!this._stopped && this._waiters.length > 0 && this._queue.length < this.maxQueued) {
                this._waiters.shift().resume();
            }
            while (this._handler && this._active < this.concurrency && this._queue.length > 0) {
                const job = this._queue.shift();
                this._active += 1;
                this._runJob(job);
            }
            if (this._queue.length === 0 && this._active === 0 && this._resolveStop !== undefined) {
                this._resolveStop();
                this._resolveStop = undefined;
                this._stopPromise = undefined;
            }
        }

        async _runJob(job) {
            const owned = sloppyWorkerContext(job.options);
            try {
                if (sloppyWorkerSignalCancelled(owned.ctx.signal)) {
                    throw sloppyWorkerRejectForCancellation(owned.ctx.signal.reason);
                }
                const result = await Promise.race([
                    this._handler(Object.freeze({ id: job.id, data: job.data }), owned.ctx),
                    new Promise((_, reject) => {
                        sloppyWorkerSubscribeSignal(owned.ctx.signal, (reason) => reject(sloppyWorkerRejectForCancellation(reason)));
                    }),
                ]);
                job.resolve(result);
            } catch (error) {
                if (error instanceof SloppyWorkerError) {
                    job.reject(error);
                } else {
                    job.reject(sloppyWorkerError("SLOPPY_E_WORK_JOB_FAILED", "work queue job failed", error));
                }
            } finally {
                owned.cleanup();
                this._active -= 1;
                this._pump();
            }
        }

        __sloppyPlanMetadata() {
            return Object.freeze({ kind: "workQueue", name: this.name, maxQueued: this.maxQueued, concurrency: this.concurrency, overflow: this.overflow });
        }
    }

    const BackgroundService = Object.freeze({
        create(name, handler, options = undefined) {
            sloppyWorkerName(name, "BackgroundService");
            if (typeof handler !== "function") {
                throw new TypeError("BackgroundService.create requires a function.");
            }
            const controller = new WorkerCancellationController();
            let promise;
            const service = {
                __sloppyWorkerResource: "backgroundService",
                name,
                state: "created",
                start() {
                    if (service.state !== "created") {
                        return service;
                    }
                    service.state = "running";
                    promise = Promise.resolve(handler(Object.freeze({ name, signal: controller.signal })))
                        .catch((error) => {
                            service.state = "failed";
                            service.failure = sloppyWorkerError("SLOPPY_E_BACKGROUND_SERVICE_FAILED", "background service failed", error);
                        });
                    return service;
                },
                async stop(reason = "app shutdown") {
                    controller.cancel(reason);
                    await promise;
                    service.state = "stopped";
                },
                __sloppyStartForApp() {
                    return service.start();
                },
                __sloppyPlanMetadata() {
                    return Object.freeze({ kind: "backgroundService", name, shutdown: options?.shutdown ?? "cancel-and-drain" });
                },
            };
            return Object.seal(service);
        },
    });

    const WorkQueue = Object.freeze({
        create(name, options = undefined) {
            return new ClassicWorkQueue(name, options);
        },
    });

    const WorkerPool = Object.freeze({
        create(name, options = undefined) {
            const queue = new ClassicWorkQueue(name, { maxQueued: options?.maxQueued ?? 64, concurrency: options?.workers ?? 1 });
            queue.process(async (job, ctx) => {
                const bridge = globalThis.__sloppy?.workers;
                if (bridge === undefined || typeof bridge.runPool !== "function") {
                    throw sloppyWorkerError("SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE", "worker pool bridge is unavailable");
                }
                try {
                    return await bridge.runPool(name, job.data.fn, job.data.input, ctx);
                } catch (error) {
                    throw sloppyWorkerBridgeError(error, "SLOPPY_E_WORKER_CRASHED", "worker pool operation failed");
                }
            });
            return Object.freeze({
                __sloppyWorkerResource: "workerPool",
                run(fn, runOptions = undefined) {
                    if (typeof fn !== "function") {
                        return Promise.reject(new TypeError("WorkerPool.run requires a function."));
                    }
                    return queue._enqueuePayload({ fn, input: sloppyWorkerSerializePayload(runOptions?.input) }, runOptions);
                },
                stop(options = undefined) {
                    return queue.stop(options);
                },
                __sloppyPlanMetadata() {
                    return Object.freeze({ kind: "workerPool", name, workers: options?.workers ?? 1, maxQueued: options?.maxQueued ?? 64 });
                },
            });
        },
    });

    class ClassicNativeWorkerHandle {
        constructor(modulePath, nativeHandle, options) {
            this.modulePath = modulePath;
            this._native = nativeHandle;
            this._stopped = false;
            this._memoryLimitMb = options.memoryLimitMb;
            this.__sloppyWorkerResource = "jsWorker";
            Object.seal(this);
        }

        async invoke(exportName, payload = undefined, options = undefined) {
            if (this._stopped) {
                throw sloppyWorkerError("SLOPPY_E_WORKER_STALE_HANDLE", "worker handle has been stopped");
            }
            if (typeof exportName !== "string" || exportName.length === 0) {
                throw new TypeError("Worker.invoke export name must be a non-empty string.");
            }
            try {
                return await this._native.invoke(exportName, sloppyWorkerSerializePayload(payload), options);
            } catch (error) {
                throw sloppyWorkerBridgeError(error, "SLOPPY_E_WORKER_CRASHED", "worker crashed");
            }
        }

        async post(message, options = undefined) {
            if (this._stopped) {
                throw sloppyWorkerError("SLOPPY_E_WORKER_STALE_HANDLE", "worker handle has been stopped");
            }
            try {
                return await this._native.post(sloppyWorkerSerializePayload(message), options);
            } catch (error) {
                throw sloppyWorkerBridgeError(error, "SLOPPY_E_WORKER_CRASHED", "worker crashed");
            }
        }

        onMessage(callback) {
            if (this._stopped) {
                throw sloppyWorkerError("SLOPPY_E_WORKER_STALE_HANDLE", "worker handle has been stopped");
            }
            if (typeof callback !== "function") {
                throw new TypeError("Worker.onMessage requires a callback.");
            }
            const subscribe = this._native.onMessage ?? this._native.addMessageListener;
            if (typeof subscribe !== "function") {
                throw sloppyWorkerError("SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE", "worker receive bridge is unavailable");
            }
            return subscribe.call(this._native, callback);
        }

        async stop() {
            if (this._stopped) {
                return;
            }
            this._stopped = true;
            try {
                await this._native.stop();
            } catch (error) {
                throw sloppyWorkerBridgeError(error, "SLOPPY_E_WORKER_STALE_HANDLE", "worker handle has been stopped");
            }
        }

        __sloppyPlanMetadata() {
            return Object.freeze({ kind: "jsWorker", path: this.modulePath, memoryLimitMb: this._memoryLimitMb });
        }
    }

    const Worker = Object.freeze({
        async start(modulePath, options = undefined) {
            if (typeof modulePath !== "string" || modulePath.length === 0) {
                throw new TypeError("Worker.start module path must be a non-empty string.");
            }
            const memoryLimitMb = sloppyWorkerPositive(options?.memoryLimitMb, "Worker.memoryLimitMb", 128);
            const bridge = globalThis.__sloppy?.workers;
            if (bridge !== undefined && typeof bridge.startWorker === "function") {
                try {
                    const nativeHandle = await bridge.startWorker(modulePath, { memoryLimitMb });
                    return new ClassicNativeWorkerHandle(modulePath, nativeHandle, { memoryLimitMb });
                } catch (error) {
                    throw sloppyWorkerBridgeError(error, "SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED", "worker isolate startup failed");
                }
            }
            throw sloppyWorkerError("SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE", "worker bridge is unavailable");
        },
    });

    function __createFrameworkServiceProvider() {
        const registrations = new Map();
        const singletonDisposables = [];
        let disposed = false;

        function validateToken(token) {
            if (typeof token !== "string" || token.length === 0) {
                throw new TypeError("Sloppy Framework service token must be a non-empty string.");
            }
        }

        function add(lifetime, token, factory) {
            validateToken(token);
            if (typeof factory !== "function") {
                throw new TypeError(`Sloppy Framework ${lifetime} service factory must be a function.`);
            }
            if (registrations.has(token)) {
                throw new Error(`sloppy: service '${token}' is already registered.`);
            }
            registrations.set(token, { lifetime, factory, initialized: false, value: undefined });
        }

        function disposeValue(value) {
            if (value === null || value === undefined) {
                return undefined;
            }
            if (typeof value[Symbol.dispose] === "function") {
                return value[Symbol.dispose]();
            }
            if (typeof value.dispose === "function") {
                return value.dispose();
            }
            if (typeof value.close === "function") {
                return value.close();
            }
            return undefined;
        }

        function disposalError(errors, message) {
            if (errors.length === 1) {
                return errors[0];
            }
            return new AggregateError(errors, message);
        }

        async function disposeValues(values, message) {
            const errors = [];
            for (const value of values) {
                try {
                    await disposeValue(value);
                } catch (error) {
                    errors.push(error);
                }
            }
            if (errors.length !== 0) {
                throw disposalError(errors, message);
            }
        }

        function createRootScope() {
            const resolving = [];
            const resolvingLifetimes = [];
            const scope = {
                context: undefined,
                get(token) {
                    return resolve(scope, token);
                },
                track(value) {
                    singletonDisposables.push(value);
                    return value;
                },
                __disposed() {
                    return false;
                },
                __hasScoped() {
                    return false;
                },
                __getScoped() {
                    return undefined;
                },
                __setScoped() {
                    throw new Error("sloppy: root service scope cannot store scoped services.");
                },
                __resolving() {
                    return resolving;
                },
                __resolvingLifetimes() {
                    return resolvingLifetimes;
                },
                __push(token, lifetime) {
                    resolving.push(token);
                    resolvingLifetimes.push(lifetime);
                },
                __pop() {
                    resolving.pop();
                    resolvingLifetimes.pop();
                },
            };
            return Object.freeze(scope);
        }

        function createScope(context) {
            const scoped = new Map();
            const transient = [];
            const resolving = [];
            const resolvingLifetimes = [];
            let scopeDisposed = false;
            const scope = {
                context,
                get(token) {
                    return resolve(scope, token);
                },
                track(value) {
                    transient.push(value);
                    return value;
                },
                async dispose() {
                    if (scopeDisposed) {
                        return;
                    }
                    scopeDisposed = true;
                    await disposeValues(
                        [...transient.reverse(), ...Array.from(scoped.values()).reverse()],
                        "sloppy: service scope disposal failed.",
                    );
                },
                __disposed() {
                    return scopeDisposed;
                },
                __hasScoped(token) {
                    return scoped.has(token);
                },
                __getScoped(token) {
                    return scoped.get(token);
                },
                __setScoped(token, value) {
                    scoped.set(token, value);
                },
                __resolving() {
                    return resolving;
                },
                __resolvingLifetimes() {
                    return resolvingLifetimes;
                },
                __push(token, lifetime) {
                    resolving.push(token);
                    resolvingLifetimes.push(lifetime);
                },
                __pop() {
                    resolving.pop();
                    resolvingLifetimes.pop();
                },
            };
            return Object.freeze(scope);
        }

        const rootScope = createRootScope();

        function resolve(scope, token) {
            validateToken(token);
            if (disposed) {
                throw new Error("sloppy: service provider is disposed.");
            }
            if (scope.__disposed()) {
                throw new Error("sloppy: service scope is disposed.");
            }
            if (!registrations.has(token)) {
                throw new Error(`sloppy: service '${token}' is not registered.`);
            }
            const registration = registrations.get(token);
            if (scope.__resolving().includes(token)) {
                throw new Error(`sloppy: service circular dependency detected: ${[...scope.__resolving(), token].join(" -> ")}.`);
            }
            if (registration.lifetime === "scoped" && scope.__resolvingLifetimes().includes("singleton")) {
                throw new Error(`sloppy: singleton service cannot depend on scoped service '${token}'.`);
            }
            if (registration.lifetime === "singleton") {
                if (!registration.initialized) {
                    rootScope.__push(token, "singleton");
                    try {
                        registration.value = registration.factory(rootScope);
                        singletonDisposables.push(registration.value);
                        registration.initialized = true;
                    } finally {
                        rootScope.__pop();
                    }
                }
                return registration.value;
            }
            if (registration.lifetime === "scoped") {
                if (!scope.__hasScoped(token)) {
                    scope.__push(token, "scoped");
                    try {
                        scope.__setScoped(token, registration.factory(scope));
                    } finally {
                        scope.__pop();
                    }
                }
                return scope.__getScoped(token);
            }
            scope.__push(token, "transient");
            try {
                const value = registration.factory(scope);
                scope.track(value);
                return value;
            } finally {
                scope.__pop();
            }
        }

        return Object.freeze({
            addSingleton(token, factory) {
                add("singleton", token, factory);
            },
            addScoped(token, factory) {
                add("scoped", token, factory);
            },
            addTransient(token, factory) {
                add("transient", token, factory);
            },
            createScope,
            async dispose() {
                if (disposed) {
                    return;
                }
                disposed = true;
                await disposeValues(
                    singletonDisposables.reverse(),
                    "sloppy: service provider disposal failed.",
                );
            },
        });
    }

    const FFI_TYPE_NAMES = Object.freeze([
        "void", "bool", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "isize",
        "usize", "f32", "f64", "ptr", "handle", "hwnd", "hmodule", "ntstatus", "cstring", "lpcstr",
        "utf16", "lpcwstr", "bytes", "mutBytes",
    ]);
    const t = Object.freeze(Object.fromEntries(FFI_TYPE_NAMES.map((name) => [name, Object.freeze({ kind: "ffi.type", name })])));

    function ffiBridge(operation) {
        const bridge = globalThis.__sloppy?.ffi ?? null;
        if (bridge === null) {
            throw new Error(`SLOPPY_E_FFI_RUNTIME_UNAVAILABLE: runtime feature stdlib.ffi is inactive or unavailable

Feature:
  stdlib.ffi

Operation:
  ${operation}

Reason:
  The active Sloppy Plan did not enable the __sloppy.ffi V8 intrinsic namespace.`);
        }
        return bridge;
    }

    const FFI_STRUCT_RESERVED_FIELDS = new Set(["ptr", "get", "set"]);

    function ffiTypeName(value, operation) {
        if (value === undefined) {
            return undefined;
        }
        if (
            value === null ||
            typeof value !== "object" ||
            value.kind !== "ffi.type" ||
            typeof value.name !== "string" ||
            t[value.name] !== value
        ) {
            throw new TypeError(`${operation} requires FFI types from sloppy/ffi t.`);
        }
        return value.name;
    }

    const unsafeFfi = Object.freeze({
        library(name, functions, options = undefined) {
            if (typeof name !== "string" || name.length === 0) {
                throw new TypeError("unsafeFfi.library name must be a non-empty string.");
            }
            if (functions === null || typeof functions !== "object" || Array.isArray(functions)) {
                throw new TypeError("unsafeFfi.library functions must be an object.");
            }
            return ffiBridge("unsafeFfi.library").library(name, functions, options ?? {});
        },
        fn(returnType, parameters, options = undefined) {
            if (!Array.isArray(parameters)) {
                throw new TypeError("unsafeFfi.fn parameters must be an array of FFI types.");
            }
            return Object.freeze({
                kind: "ffi.fn",
                returnType: ffiTypeName(returnType, "unsafeFfi.fn"),
                parameters: parameters.map((parameter) => ffiTypeName(parameter, "unsafeFfi.fn")),
                options: options === undefined ? Object.freeze({}) : Object.freeze({ ...options }),
            });
        },
        ref(type, initialValue = undefined) {
            const cell = ffiBridge("unsafeFfi.ref").ref(ffiTypeName(type, "unsafeFfi.ref"), initialValue);
            Object.defineProperty(cell, "value", {
                enumerable: true,
                get() {
                    return cell.get();
                },
                set(value) {
                    cell.set(value);
                },
            });
            Object.defineProperty(cell, "ptr", {
                enumerable: false,
                value: cell,
            });
            return cell;
        },
        buffer(byteLength) {
            const bytes = ffiBridge("unsafeFfi.buffer").buffer(byteLength);
            Object.defineProperty(bytes, "ptr", {
                enumerable: false,
                value: bytes,
            });
            return bytes;
        },
        cstringBuffer(valueOrByteLength) {
            const cstring = ffiBridge("unsafeFfi.cstringBuffer").cstringBuffer(valueOrByteLength);
            Object.defineProperty(cstring, "ptr", {
                enumerable: false,
                value: cstring,
            });
            return cstring;
        },
        utf16Buffer(valueOrCodeUnits) {
            const utf16 = ffiBridge("unsafeFfi.utf16Buffer").utf16Buffer(valueOrCodeUnits);
            Object.defineProperty(utf16, "ptr", {
                enumerable: false,
                value: utf16,
            });
            return utf16;
        },
        struct(name, fields, options = undefined) {
            if (typeof name !== "string" || name.length === 0) {
                throw new TypeError("unsafeFfi.struct name must be a non-empty string.");
            }
            if (fields === null || typeof fields !== "object" || Array.isArray(fields)) {
                throw new TypeError("unsafeFfi.struct fields must be an object.");
            }
            const normalized = Object.fromEntries(
                Object.entries(fields).map(([field, fieldType]) => {
                    if (FFI_STRUCT_RESERVED_FIELDS.has(field)) {
                        throw new TypeError(`unsafeFfi.struct field '${field}' is reserved.`);
                    }
                    return [field, ffiTypeName(fieldType, "unsafeFfi.struct")];
                }),
            );
            const layout = ffiBridge("unsafeFfi.struct").struct(name, normalized, options ?? {});
            const alloc = layout.alloc.bind(layout);
            Object.defineProperty(layout, "alloc", {
                enumerable: true,
                value(initial = undefined) {
                    const instance = alloc(initial);
                    Object.defineProperty(instance, "ptr", {
                        enumerable: false,
                        value: instance,
                    });
                    for (const field of Object.keys(normalized)) {
                        Object.defineProperty(instance, field, {
                            enumerable: true,
                            get() {
                                return instance.get(field);
                            },
                            set(value) {
                                instance.set(field, value);
                            },
                        });
                    }
                    return instance;
                },
            });
            return layout;
        },
    });

    function __sloppyRealtimeSse(handler, options = undefined) {
        if (typeof handler !== "function") {
            throw new TypeError("Sloppy Realtime.sse handler must be a function.");
        }
        return async function sloppySseHandler(ctx) {
            let closed = false;
            let queued = 0;
            const maxQueuedEvents = options?.maxQueuedEvents ?? 64;
            return Results.stream(async (writer) => {
                function fieldText(value, field) {
                    const text = String(value);
                    if (/[\r\n]/u.test(text)) {
                        throw new TypeError(`Sloppy SSE ${field} must not contain CR or LF.`);
                    }
                    return text;
                }
                function frame(data, frameOptions = undefined) {
                    const text = typeof data === "string" ? data : JSON.stringify(data);
                    const lines = [];
                    if (frameOptions?.comment !== undefined) {
                        lines.push(`: ${fieldText(frameOptions.comment, "comment")}`);
                    }
                    if (frameOptions?.event !== undefined) {
                        lines.push(`event: ${fieldText(frameOptions.event, "event")}`);
                    }
                    if (frameOptions?.id !== undefined) {
                        lines.push(`id: ${fieldText(frameOptions.id, "id")}`);
                    }
                    if (frameOptions?.retry !== undefined) {
                        if (!Number.isInteger(frameOptions.retry) || frameOptions.retry < 0) {
                            throw new TypeError("Sloppy SSE retry must be a non-negative integer.");
                        }
                        lines.push(`retry: ${frameOptions.retry}`);
                    }
                    for (const line of String(text).split("\n")) {
                        lines.push(`data: ${line}`);
                    }
                    lines.push("", "");
                    return lines.join("\n");
                }
                function write(frame) {
                    if (closed) {
                        throw new TypeError("Sloppy SSE stream is closed.");
                    }
                    if (queued >= maxQueuedEvents) {
                        throw new TypeError("Sloppy SSE bounded write queue is full.");
                    }
                    queued += 1;
                    try {
                        writer.writeText(frame);
                    } finally {
                        queued -= 1;
                    }
                }
                const stream = Object.freeze({
                    send(data) {
                        write(frame(data));
                    },
                    event(name, data, eventOptions = undefined) {
                        if (typeof name !== "string" || name.length === 0 || !/^[A-Za-z0-9_.:-]+$/u.test(name)) {
                            throw new TypeError("Sloppy SSE event names must be non-empty token strings.");
                        }
                        write(frame(data, { ...(eventOptions ?? {}), event: name }));
                    },
                    comment(text) {
                        write(`: ${fieldText(text, "comment")}\n\n`);
                    },
                    heartbeat() {
                        write(": heartbeat\n\n");
                    },
                    close() {
                        if (closed) {
                            return;
                        }
                        closed = true;
                        writer.close();
                    },
                });
                await handler(ctx, stream);
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

    function __sloppyRealtimeWebSocket(handler) {
        if (typeof handler !== "function") {
            throw new TypeError("Sloppy WebSocket route handler must be a function.");
        }
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

    function __sloppyRealtimeHub(name) {
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
                    for (const groupName of client.groups) {
                        groups.get(groupName)?.delete(id);
                    }
                    connections.delete(id);
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
        return Object.freeze({
            name,
            socket(handler) {
                if (typeof handler !== "function") {
                    throw new TypeError("Sloppy Realtime.hub socket handler must be a function.");
                }
                return __sloppyRealtimeWebSocket(handler);
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
                        for (const groupName of client.groups) {
                            groups.get(groupName)?.delete(connectionId);
                        }
                        connections.delete(connectionId);
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
        });
    }

    const Realtime = Object.freeze({
        sse: __sloppyRealtimeSse,
        websocket: __sloppyRealtimeWebSocket,
        hub: __sloppyRealtimeHub,
        textBytes(value) {
            return Text.utf8.encode(String(value));
        },
    });

    globalThis.__sloppy_runtime = Object.freeze({
        Results,
        Realtime,
        ProblemDetails,
        Random,
        Hash,
        Hmac,
        Password,
        ConstantTime,
        Secret,
        NonCryptoHash,
        Base64,
        Base64Url,
        Hex,
        Text,
        Binary,
        Compression,
        Checksums,
        data,
        Migrations,
        ProviderHealth,
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
        TcpClient,
        TcpListener,
        TcpConnection,
        NetworkAddress,
        HttpClient,
        System,
        Environment,
        Process,
        Signals,
        OsError,
        BackgroundService,
        WorkQueue,
        WorkerPool,
        Worker,
        WorkerCancellationController,
        WorkerCancellationSignal,
        SloppyWorkerError,
        unsafeFfi,
        t,
        __createFrameworkServiceProvider,
    });
})();
