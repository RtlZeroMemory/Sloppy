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
    const QUERY_MARKER = "__sloppyQuery";
    const LOWERED_QUERY_MARKER = Symbol.for("sloppy.loweredQuery");
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
    const REAL_PROVIDER_HANDLES = new WeakMap();
    const POSTGRES_MAX_POOL_CONNECTIONS = 256;
    const SQLSERVER_MAX_POOL_CONNECTIONS = 256;
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

    function validateWorkerOptions(options, operation) {
        if (options === undefined || options === null) {
            return Object.freeze({});
        }
        if (!isPlainObject(options)) {
            throw new TypeError(`${operation} options must be a plain object.`);
        }
        return Object.freeze({ ...options });
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

    function validatePlaceholderStyle(style) {
        if (!Object.prototype.hasOwnProperty.call(PLACEHOLDER_STYLES, style)) {
            throw new TypeError("Sloppy data placeholderStyle must be one of question, postgres, or named.");
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

        const lowered = {
            [QUERY_MARKER]: true,
            text,
            parameters: Object.freeze([...values]),
            parameterCount: values.length,
            placeholderStyle: normalized.placeholderStyle,
            placeholders: Object.freeze(placeholders),
            templateStrings: Object.freeze([...strings]),
        };
        Object.defineProperty(lowered, LOWERED_QUERY_MARKER, {
            value: true,
            enumerable: false,
            configurable: false,
            writable: false,
        });
        return Object.freeze(lowered);
    }

    function isLoweredQuery(value) {
        return value !== null &&
            typeof value === "object" &&
            value[LOWERED_QUERY_MARKER] === true &&
            value[QUERY_MARKER] === true &&
            typeof value.text === "string" &&
            Array.isArray(value.parameters) &&
            Number.isInteger(value.parameterCount) &&
            value.parameterCount === value.parameters.length &&
            Object.prototype.hasOwnProperty.call(PLACEHOLDER_STYLES, value.placeholderStyle);
    }

    function renderLoweredQueryText(query, placeholderStyle) {
        validatePlaceholderStyle(placeholderStyle);
        if (query.placeholderStyle === placeholderStyle) {
            return query.text;
        }
        if (!Array.isArray(query.templateStrings) ||
            query.templateStrings.length !== query.parameters.length + 1)
        {
            throw new TypeError("Sloppy lowered query cannot be rewritten for this provider.");
        }

        let text = query.templateStrings[0];
        for (let index = 0; index < query.parameters.length; index += 1) {
            text += PLACEHOLDER_STYLES[placeholderStyle](index + 1).text +
                query.templateStrings[index + 1];
        }
        return text;
    }

    function sql(strings, ...params) {
        return createLoweredQuery(strings, params, { placeholderStyle: "question" });
    }

    sql.lower = function lower(strings, params = [], options) {
        if (!Array.isArray(params)) {
            throw new TypeError("Sloppy sql.lower values must be an array.");
        }
        return createLoweredQuery(strings, params, options);
    };
    sql.decimal = values.decimal;
    sql.uuid = values.uuid;
    sql.date = values.date;
    sql.time = values.time;
    sql.timestamp = values.timestamp;
    sql.instant = values.instant;
    sql.offsetDateTime = values.offsetDateTime;
    sql.json = values.json;
    sql.rawJson = values.rawJson;
    sql.bytes = values.bytes;
    sql.isDbValue = values.isDbValue;

    Object.freeze(sql);

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
        if (typeof name !== "string" || name.length === 0 || name.includes("\0")) {
            throw new TypeError("Sloppy data.sqlite provider name must be a non-empty string without NUL.");
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
        if (typeof database !== "string" || database.length === 0 || database.includes("\0")) {
            throw new TypeError("Sloppy sqlite.open database must be a non-empty string without NUL.");
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

        if (typeof options.capability !== "string" || options.capability.length === 0 || options.capability.includes("\0")) {
            throw new TypeError("Sloppy sqlite.open capability must be a non-empty string without NUL.");
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
        if (isLoweredQuery(sql) && params === undefined) {
            return {
                text: renderLoweredQueryText(sql, "question"),
                parameters: normalizeSqliteParams([...sql.parameters], operation),
            };
        }
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

    function validateProviderOperationOptions(
        options,
        operation,
        allowResultMode = false,
        allowMaxRows = false,
        allowCursorOptions = false,
    ) {
        const defaults = Object.freeze({
            mode: allowResultMode ? "object" : undefined,
            batchSize: undefined,
            maxRows: undefined,
            timeoutMs: undefined,
        });
        if (options === undefined) {
            return defaults;
        }
        if (!isPlainObject(options)) {
            throw new TypeError(`Sloppy ${operation} options must be a plain object.`);
        }
        const allowedKeys = new Set(["timeoutMs", "deadline", "signal"]);
        if (allowResultMode) {
            allowedKeys.add("mode");
        }
        if (allowMaxRows) {
            allowedKeys.add("maxRows");
        }
        if (allowCursorOptions) {
            allowedKeys.add("batchSize");
        }
        for (const key of Object.keys(options)) {
            if (!allowedKeys.has(key)) {
                throw new TypeError(`Sloppy ${operation} option '${key}' is not supported by the current runtime bridge.`);
            }
        }
        const maxRows = options.maxRows;
        if (maxRows !== undefined) {
            if (!allowMaxRows || !Number.isInteger(maxRows) || maxRows < 1 || maxRows > 0xffffffff) {
                throw new TypeError(`Sloppy ${operation} maxRows option must be an integer from 1 to 4294967295.`);
            }
        }
        const timeoutMs = options.timeoutMs;
        if (timeoutMs !== undefined) {
            if (!Number.isInteger(timeoutMs) || timeoutMs < 0 || timeoutMs > 0xffffffff) {
                throw new TypeError(`Sloppy ${operation} timeoutMs option must be an integer from 0 to 4294967295.`);
            }
            if (timeoutMs === 0) {
                throw new Error(`SLOPPY_E_DEADLINE_EXCEEDED: Sloppy ${operation} deadline was exceeded`);
            }
        }
        const batchSize = options.batchSize;
        if (batchSize !== undefined) {
            if (!allowCursorOptions || !Number.isInteger(batchSize) || batchSize < 1 || batchSize > 4096) {
                throw new TypeError(`Sloppy ${operation} batchSize option must be an integer from 1 to 4096.`);
            }
        }
        const signal = options.signal;
        if (
            signal !== undefined &&
            signal !== null &&
            (typeof signal !== "object" || Array.isArray(signal))
        ) {
            throw new TypeError(`Sloppy ${operation} signal option must be an object.`);
        }
        if (signal?.aborted === true) {
            throw cancelledError(signal.reason);
        }
        if (typeof signal?.throwIfAborted === "function") {
            try {
                signal.throwIfAborted();
            }
            catch {
                throw cancelledError(signal.reason);
            }
        }
        const deadline = options.deadline;
        if (
            deadline !== undefined &&
            deadline !== null &&
            (typeof deadline !== "object" || Array.isArray(deadline))
        ) {
            throw new TypeError(`Sloppy ${operation} deadline option must be an object or null.`);
        }
        if (deadline?.expired === true) {
            throw new Error(`SLOPPY_E_DEADLINE_EXCEEDED: Sloppy ${operation} deadline was exceeded`);
        }
        let deadlineMs = Infinity;
        if (deadline?.remainingMs !== undefined) {
            if (typeof deadline.remainingMs !== "function") {
                throw new TypeError(`Sloppy ${operation} deadline.remainingMs must be a function when supplied.`);
            }
            deadlineMs = deadline.remainingMs();
            if (typeof deadlineMs !== "number" || Number.isNaN(deadlineMs)) {
                throw new TypeError(`Sloppy ${operation} deadline.remainingMs must return a number.`);
            }
        }
        let effectiveTimeoutMs = timeoutMs;
        if (deadlineMs <= 0) {
            throw new Error(`SLOPPY_E_DEADLINE_EXCEEDED: Sloppy ${operation} deadline was exceeded`);
        }
        if (deadlineMs !== Infinity) {
            if (!Number.isFinite(deadlineMs)) {
                throw new TypeError(`Sloppy ${operation} deadline remainingMs() must return a finite number or Infinity.`);
            }
            const roundedDeadlineMs = Math.min(Math.ceil(deadlineMs), 0xffffffff);
            effectiveTimeoutMs = effectiveTimeoutMs === undefined
                ? roundedDeadlineMs
                : Math.min(effectiveTimeoutMs, roundedDeadlineMs);
        }
        return Object.freeze({
            mode: allowResultMode ? normalizeResultMode(options.mode, operation) : undefined,
            batchSize,
            maxRows,
            timeoutMs: effectiveTimeoutMs,
        });
    }

    function providerQueryBridgeOptions(
        validatedOptions,
        includeTimeout = false,
        includeCursorOptions = false,
    ) {
        if (
            validatedOptions.maxRows === undefined &&
            (!includeTimeout || validatedOptions.timeoutMs === undefined) &&
            (!includeCursorOptions || validatedOptions.batchSize === undefined)
        ) {
            return undefined;
        }
        const bridgeOptions = {};
        if (includeCursorOptions && validatedOptions.batchSize !== undefined) {
            bridgeOptions.batchSize = validatedOptions.batchSize;
        }
        if (validatedOptions.maxRows !== undefined) {
            bridgeOptions.maxRows = validatedOptions.maxRows;
        }
        if (includeTimeout && validatedOptions.timeoutMs !== undefined) {
            bridgeOptions.timeoutMs = validatedOptions.timeoutMs;
        }
        return Object.freeze(bridgeOptions);
    }

    function invokeProviderQuery(method, handle, query, validatedOptions, includeTimeout = false) {
        const bridgeOptions = providerQueryBridgeOptions(validatedOptions, includeTimeout);
        if (bridgeOptions === undefined) {
            return method(handle, query.text, query.parameters);
        }
        return method(handle, query.text, query.parameters, bridgeOptions);
    }

    function requireCursorBridgeMethod(bridge, method, provider) {
        if (typeof bridge?.[method] !== "function") {
            throw new Error(`sloppy: ${provider} cursor bridge is unavailable`);
        }
        return bridge[method];
    }

    function invokeProviderCursorOpen(method, handle, query, validatedOptions, includeTimeout = false) {
        const bridgeOptions = providerQueryBridgeOptions(validatedOptions, includeTimeout, true);
        if (bridgeOptions === undefined) {
            return method(handle, query.text, query.parameters);
        }
        return method(handle, query.text, query.parameters, bridgeOptions);
    }

    function createDataCursor(provider, bridge, nativeCursor, mode, validatedOptions, registry) {
        if (!isPlainObject(nativeCursor)) {
            throw new TypeError(`Sloppy ${provider} cursor bridge returned an invalid cursor handle.`);
        }

        let closed = nativeCursor.closed === true;
        let started = false;
        let rowsSeen = 0;
        let cursor = null;
        const columns = Object.freeze(Array.isArray(nativeCursor.columns) ? [...nativeCursor.columns] : []);

        async function close() {
            if (closed) {
                registry?.delete(cursor);
                return;
            }
            closed = true;
            registry?.delete(cursor);
            await requireCursorBridgeMethod(bridge, "cursorClose", provider)(nativeCursor);
        }

        async function next() {
            if (closed) {
                throw new Error(`sloppy: ${provider} cursor is closed`);
            }
            started = true;
            let result;
            try {
                result = await requireCursorBridgeMethod(bridge, "cursorNext", provider)(nativeCursor);
            } catch (error) {
                try {
                    await close();
                } catch {
                    // Preserve the provider error.
                }
                throw error;
            }
            if (!isPlainObject(result) || typeof result.done !== "boolean") {
                await close();
                throw new TypeError(`Sloppy ${provider} cursor bridge returned an invalid iterator result.`);
            }
            if (result.done) {
                await close();
                return Object.freeze({ done: true, value: undefined });
            }
            rowsSeen += 1;
            return Object.freeze({ done: false, value: result.value });
        }

        cursor = {
            get closed() {
                return closed;
            },
            get columns() {
                return columns;
            },
            get mode() {
                return mode;
            },
            get provider() {
                return provider;
            },
            close,
            next,
            async return() {
                await close();
                return Object.freeze({ done: true, value: undefined });
            },
            async throw(error) {
                await close();
                throw error;
            },
            [Symbol.asyncIterator]() {
                if (started) {
                    throw new Error(`sloppy: ${provider} cursor is single-use`);
                }
                return this;
            },
            __debug() {
                return Object.freeze({ kind: "data-cursor", closed, mode, provider, rowsSeen });
            },
        };
        registry?.add(cursor);
        return Object.freeze(cursor);
    }

    function closeActiveCursors(cursors) {
        if (!(cursors instanceof Set) || cursors.size === 0) {
            return;
        }
        for (const cursor of Array.from(cursors)) {
            try {
                const result = cursor.close();
                if (result !== undefined && typeof result.catch === "function") {
                    result.catch(() => {});
                }
            } catch {
            }
        }
        cursors.clear();
    }

    function markRealDataProvider(provider, kind) {
        REAL_PROVIDER_HANDLES.set(provider, kind);
        return provider;
    }

    function isRealDataProvider(provider, kind = undefined) {
        const actual = REAL_PROVIDER_HANDLES.get(provider);
        return kind === undefined ? actual !== undefined : actual === kind;
    }

    async function openProviderCursor(provider, bridge, handle, query, validated, mode, methodName, registry) {
        const method = requireCursorBridgeMethod(bridge, methodName, provider);
        const nativeCursor = await invokeProviderCursorOpen(method, handle, query, validated, true);
        return createDataCursor(provider, bridge, nativeCursor, mode, validated, registry);
    }

    function createSqliteConnection(bridge, handle) {
        const state = {
            closed: false,
            handle,
            transactionActive: false,
            activeCursors: new Set(),
        };

        function assertOpen(operation) {
            if (state.closed) {
                throw sqliteClosedError(operation);
            }
        }

        function createTransaction() {
            const txState = {
                closed: false,
                activeCursors: new Set(),
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
                    const validated = validateProviderOperationOptions(options, "sqlite.transaction.query", true, true);
                    const query = normalizeSqliteQuery("query", sql, params);
                    const method = validated.mode === "raw" ? bridge.transactionQueryRaw : bridge.transactionQuery;
                    return invokeProviderQuery(method, state.handle, query, validated, true);
                },
                queryRaw(sql, params, options) {
                    assertTransactionOpen("transaction.queryRaw");
                    const validated = validateProviderOperationOptions(options, "sqlite.transaction.queryRaw", false, true);
                    const query = normalizeSqliteQuery("queryRaw", sql, params);
                    return invokeProviderQuery(bridge.transactionQueryRaw, state.handle, query, validated, true);
                },
                async queryCursor(sql, params, options) {
                    assertTransactionOpen("transaction.queryCursor");
                    const validated = validateProviderOperationOptions(options, "sqlite.transaction.queryCursor", true, true, true);
                    const query = normalizeSqliteQuery("queryCursor", sql, params);
                    const methodName = validated.mode === "raw"
                        ? "transactionQueryRawCursor"
                        : "transactionQueryCursor";
                    return openProviderCursor("sqlite", bridge, state.handle, query, validated, validated.mode, methodName, txState.activeCursors);
                },
                async queryRawCursor(sql, params, options) {
                    assertTransactionOpen("transaction.queryRawCursor");
                    const validated = validateProviderOperationOptions(options, "sqlite.transaction.queryRawCursor", false, true, true);
                    const query = normalizeSqliteQuery("queryRawCursor", sql, params);
                    return openProviderCursor("sqlite", bridge, state.handle, query, validated, "raw", "transactionQueryRawCursor", txState.activeCursors);
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
                    closeActiveCursors(txState.activeCursors);
                    txState.closed = true;
                },
            };
        }

        async function rollbackAfterCallbackError(error, transaction) {
            try {
                transaction.close();
                await bridge.transactionRollback(state.handle);
            } catch {
                transaction.close();
                state.transactionActive = false;
                state.closed = true;
                try {
                    bridge.close(state.handle);
                } catch {
                    // Preserve the original callback or thenable error while preventing reuse.
                }
                throw error;
            }
            state.transactionActive = false;
            throw error;
        }

        async function commitTransaction(transaction) {
            try {
                transaction.close();
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
            state.transactionActive = false;
        }

        const connection = {
            __debug() {
                return Object.freeze({
                    kind: "sqlite-connection",
                    provider: "sqlite",
                });
            },
            exec(sql, params, options) {
                assertOpen("exec");
                validateProviderOperationOptions(options, "sqlite.exec");
                const query = normalizeSqliteQuery("exec", sql, params);
                return bridge.exec(state.handle, query.text, query.parameters);
            },
            query(sql, params, options) {
                assertOpen("query");
                const validated = validateProviderOperationOptions(options, "sqlite.query", true, true);
                const query = normalizeSqliteQuery("query", sql, params);
                const method = validated.mode === "raw" ? bridge.queryRaw : bridge.query;
                return invokeProviderQuery(method, state.handle, query, validated, true);
            },
            queryRaw(sql, params, options) {
                assertOpen("queryRaw");
                const validated = validateProviderOperationOptions(options, "sqlite.queryRaw", false, true);
                const query = normalizeSqliteQuery("queryRaw", sql, params);
                return invokeProviderQuery(bridge.queryRaw, state.handle, query, validated, true);
            },
            async queryCursor(sql, params, options) {
                assertOpen("queryCursor");
                const validated = validateProviderOperationOptions(options, "sqlite.queryCursor", true, true, true);
                const query = normalizeSqliteQuery("queryCursor", sql, params);
                const methodName = validated.mode === "raw" ? "queryRawCursor" : "queryCursor";
                return openProviderCursor("sqlite", bridge, state.handle, query, validated, validated.mode, methodName, state.activeCursors);
            },
            async queryRawCursor(sql, params, options) {
                assertOpen("queryRawCursor");
                const validated = validateProviderOperationOptions(options, "sqlite.queryRawCursor", false, true, true);
                const query = normalizeSqliteQuery("queryRawCursor", sql, params);
                return openProviderCursor("sqlite", bridge, state.handle, query, validated, "raw", "queryRawCursor", state.activeCursors);
            },
            stream(sql, params, options) {
                return this.queryCursor(sql, params, options);
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

                closeActiveCursors(state.activeCursors);
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
        };
        return Object.freeze(markRealDataProvider(connection, "sqlite"));
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
        const provider = normalizeSqliteProviderToken(name);
        const bridge = requireSqliteBridge();
        return createSqliteConnection(bridge, bridge.open({
            provider,
        }));
    }

    sqlite.open = function open(options) {
        const safeOptions = normalizeSqliteOpenOptions(options);
        const bridge = requireSqliteBridge();
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
        if (typeof options.connectionString !== "string" || options.connectionString.length === 0 || options.connectionString.includes("\0")) {
            throw new TypeError("Sloppy postgres.open connectionString must be a non-empty string without NUL.");
        }
        const capability = options.capability ?? "data.postgres";
        if (typeof capability !== "string" || capability.length === 0 || capability.includes("\0")) {
            throw new TypeError("Sloppy postgres.open capability must be a non-empty string without NUL.");
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
            capability,
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
        if (isLoweredQuery(sql) && params === undefined) {
            return {
                text: renderLoweredQueryText(sql, "postgres"),
                parameters: normalizePostgresParams([...sql.parameters], operation),
            };
        }
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
            activeCursors: new Set(),
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
                activeCursors: new Set(),
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
                    const validated = validateProviderOperationOptions(options, "postgres.transaction.query", true, true);
                    const query = normalizePostgresQuery("query", sql, params);
                    const method = validated.mode === "raw" ? bridge.transactionQueryRaw : bridge.transactionQuery;
                    return invokeProviderQuery(method, state.handle, query, validated, true);
                },
                queryRaw(sql, params, options) {
                    assertTransactionOpen("transaction.queryRaw");
                    const validated = validateProviderOperationOptions(options, "postgres.transaction.queryRaw", false, true);
                    const query = normalizePostgresQuery("queryRaw", sql, params);
                    return invokeProviderQuery(bridge.transactionQueryRaw, state.handle, query, validated, true);
                },
                async queryCursor(sql, params, options) {
                    assertTransactionOpen("transaction.queryCursor");
                    const validated = validateProviderOperationOptions(options, "postgres.transaction.queryCursor", true, true, true);
                    const query = normalizePostgresQuery("queryCursor", sql, params);
                    const methodName = validated.mode === "raw"
                        ? "transactionQueryRawCursor"
                        : "transactionQueryCursor";
                    return openProviderCursor("postgres", bridge, state.handle, query, validated, validated.mode, methodName, txState.activeCursors);
                },
                async queryRawCursor(sql, params, options) {
                    assertTransactionOpen("transaction.queryRawCursor");
                    const validated = validateProviderOperationOptions(options, "postgres.transaction.queryRawCursor", false, true, true);
                    const query = normalizePostgresQuery("queryRawCursor", sql, params);
                    return openProviderCursor("postgres", bridge, state.handle, query, validated, "raw", "transactionQueryRawCursor", txState.activeCursors);
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
                    closeActiveCursors(txState.activeCursors);
                    txState.closed = true;
                },
            };
        }

        async function rollbackAfterCallbackError(error, transaction) {
            try {
                transaction.close();
                await bridge.transactionRollback(state.handle);
            } catch {
                transaction.close();
                state.transactionActive = false;
                state.closed = true;
                try {
                    bridge.close(state.handle);
                } catch {
                    // Preserve the callback error while preventing resource reuse.
                }
                throw error;
            }
            state.transactionActive = false;
            throw error;
        }

        async function commitTransaction(transaction) {
            try {
                transaction.close();
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
            state.transactionActive = false;
        }

        const connection = {
            exec(sql, params, options) {
                assertOpen("exec");
                validateProviderOperationOptions(options, "postgres.exec");
                const query = normalizePostgresQuery("exec", sql, params);
                return bridge.exec(state.handle, query.text, query.parameters);
            },
            query(sql, params, options) {
                assertOpen("query");
                const validated = validateProviderOperationOptions(options, "postgres.query", true, true);
                const query = normalizePostgresQuery("query", sql, params);
                const method = validated.mode === "raw" ? bridge.queryRaw : bridge.query;
                return invokeProviderQuery(method, state.handle, query, validated, true);
            },
            queryRaw(sql, params, options) {
                assertOpen("queryRaw");
                const validated = validateProviderOperationOptions(options, "postgres.queryRaw", false, true);
                const query = normalizePostgresQuery("queryRaw", sql, params);
                return invokeProviderQuery(bridge.queryRaw, state.handle, query, validated, true);
            },
            async queryCursor(sql, params, options) {
                assertOpen("queryCursor");
                const validated = validateProviderOperationOptions(options, "postgres.queryCursor", true, true, true);
                const query = normalizePostgresQuery("queryCursor", sql, params);
                const methodName = validated.mode === "raw" ? "queryRawCursor" : "queryCursor";
                return openProviderCursor("postgres", bridge, state.handle, query, validated, validated.mode, methodName, state.activeCursors);
            },
            async queryRawCursor(sql, params, options) {
                assertOpen("queryRawCursor");
                const validated = validateProviderOperationOptions(options, "postgres.queryRawCursor", false, true, true);
                const query = normalizePostgresQuery("queryRawCursor", sql, params);
                return openProviderCursor("postgres", bridge, state.handle, query, validated, "raw", "queryRawCursor", state.activeCursors);
            },
            stream(sql, params, options) {
                return this.queryCursor(sql, params, options);
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
                closeActiveCursors(state.activeCursors);
                bridge.close(state.handle);
                state.closed = true;
            },
            __debug() {
                return Object.freeze({
                    kind: "postgres-connection",
                    provider: "postgres",
                    closed: state.closed,
                    transactionActive: state.transactionActive,
                    resource: "opaque",
                });
            },
        };
        return Object.freeze(markRealDataProvider(connection, "postgres"));
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
        if (typeof options.connectionString !== "string" || options.connectionString.length === 0 || options.connectionString.includes("\0")) {
            throw new TypeError("Sloppy sqlserver.open connectionString must be a non-empty string without NUL.");
        }
        const capability = options.capability ?? "data.sqlserver";
        if (typeof capability !== "string" || capability.length === 0 || capability.includes("\0")) {
            throw new TypeError("Sloppy sqlserver.open capability must be a non-empty string without NUL.");
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
            capability,
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
        if (isLoweredQuery(sql) && params === undefined) {
            return {
                text: renderLoweredQueryText(sql, "named"),
                parameters: normalizeSqlServerParams([...sql.parameters], operation),
            };
        }
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
            activeCursors: new Set(),
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
                activeCursors: new Set(),
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
                    const validated = validateProviderOperationOptions(options, "sqlserver.transaction.query", true, true);
                    const query = normalizeSqlServerQuery("query", sql, params);
                    const method = validated.mode === "raw" ? bridge.transactionQueryRaw : bridge.transactionQuery;
                    return invokeProviderQuery(method, state.handle, query, validated, true);
                },
                queryRaw(sql, params, options) {
                    assertTransactionOpen("transaction.queryRaw");
                    const validated = validateProviderOperationOptions(options, "sqlserver.transaction.queryRaw", false, true);
                    const query = normalizeSqlServerQuery("queryRaw", sql, params);
                    return invokeProviderQuery(bridge.transactionQueryRaw, state.handle, query, validated, true);
                },
                async queryCursor(sql, params, options) {
                    assertTransactionOpen("transaction.queryCursor");
                    const validated = validateProviderOperationOptions(options, "sqlserver.transaction.queryCursor", true, true, true);
                    const query = normalizeSqlServerQuery("queryCursor", sql, params);
                    const methodName = validated.mode === "raw"
                        ? "transactionQueryRawCursor"
                        : "transactionQueryCursor";
                    return openProviderCursor("sqlserver", bridge, state.handle, query, validated, validated.mode, methodName, txState.activeCursors);
                },
                async queryRawCursor(sql, params, options) {
                    assertTransactionOpen("transaction.queryRawCursor");
                    const validated = validateProviderOperationOptions(options, "sqlserver.transaction.queryRawCursor", false, true, true);
                    const query = normalizeSqlServerQuery("queryRawCursor", sql, params);
                    return openProviderCursor("sqlserver", bridge, state.handle, query, validated, "raw", "transactionQueryRawCursor", txState.activeCursors);
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
                    closeActiveCursors(txState.activeCursors);
                    txState.closed = true;
                },
            };
        }

        async function rollbackAfterCallbackError(error, transaction) {
            try {
                transaction.close();
                await bridge.transactionRollback(state.handle);
            } catch {
                transaction.close();
                state.transactionActive = false;
                state.closed = true;
                try {
                    bridge.close(state.handle);
                } catch {
                    // Preserve the callback error while preventing resource reuse.
                }
                throw error;
            }
            state.transactionActive = false;
            throw error;
        }

        async function commitTransaction(transaction) {
            try {
                transaction.close();
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
            state.transactionActive = false;
        }

        const connection = {
            exec(sql, params, options) {
                assertOpen("exec");
                validateProviderOperationOptions(options, "sqlserver.exec");
                const query = normalizeSqlServerQuery("exec", sql, params);
                return bridge.exec(state.handle, query.text, query.parameters);
            },
            query(sql, params, options) {
                assertOpen("query");
                const validated = validateProviderOperationOptions(options, "sqlserver.query", true, true);
                const query = normalizeSqlServerQuery("query", sql, params);
                const method = validated.mode === "raw" ? bridge.queryRaw : bridge.query;
                return invokeProviderQuery(method, state.handle, query, validated, true);
            },
            queryRaw(sql, params, options) {
                assertOpen("queryRaw");
                const validated = validateProviderOperationOptions(options, "sqlserver.queryRaw", false, true);
                const query = normalizeSqlServerQuery("queryRaw", sql, params);
                return invokeProviderQuery(bridge.queryRaw, state.handle, query, validated, true);
            },
            async queryCursor(sql, params, options) {
                assertOpen("queryCursor");
                const validated = validateProviderOperationOptions(options, "sqlserver.queryCursor", true, true, true);
                const query = normalizeSqlServerQuery("queryCursor", sql, params);
                const methodName = validated.mode === "raw" ? "queryRawCursor" : "queryCursor";
                return openProviderCursor("sqlserver", bridge, state.handle, query, validated, validated.mode, methodName, state.activeCursors);
            },
            async queryRawCursor(sql, params, options) {
                assertOpen("queryRawCursor");
                const validated = validateProviderOperationOptions(options, "sqlserver.queryRawCursor", false, true, true);
                const query = normalizeSqlServerQuery("queryRawCursor", sql, params);
                return openProviderCursor("sqlserver", bridge, state.handle, query, validated, "raw", "queryRawCursor", state.activeCursors);
            },
            stream(sql, params, options) {
                return this.queryCursor(sql, params, options);
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
                closeActiveCursors(state.activeCursors);
                bridge.close(state.handle);
                state.closed = true;
            },
            __debug() {
                return Object.freeze({
                    kind: "sqlserver-connection",
                    provider: "sqlserver",
                    closed: state.closed,
                    transactionActive: state.transactionActive,
                    resource: "opaque",
                });
            },
        };
        return Object.freeze(markRealDataProvider(connection, "sqlserver"));
    }

    const sqlserver = Object.freeze({
        open(options) {
            const bridge = requireSqlServerBridge();
            return createSqlServerConnection(bridge, bridge.open(normalizeSqlServerOpenOptions(options)));
        },
    });

    const MIGRATIONS_TABLE = "_sloppy_migrations";
    const MIGRATION_HASH_PREFIX = "fnv1a32:";
    const MIGRATION_PROVIDER_KINDS = Object.freeze({
        sqlite: true,
        postgres: true,
        sqlserver: true,
    });

    function normalizeDataMigrationOptions(options) {
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy Migrations options must be a plain object.");
        }
        const provider = options.provider;
        const path = options.path;
        if (typeof provider !== "string" || provider.length === 0) {
            throw new TypeError("Sloppy Migrations provider must be a non-empty string.");
        }
        if (MIGRATION_PROVIDER_KINDS[provider] !== true) {
            throw new TypeError("Sloppy Migrations provider must be sqlite, postgres, or sqlserver.");
        }
        if (typeof path !== "string" || path.length === 0) {
            throw new TypeError("Sloppy Migrations path must be a non-empty string.");
        }
        const slash = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"));
        const directory = slash < 0 ? "." : path.slice(0, slash);
        const pattern = slash < 0 ? path : path.slice(slash + 1);
        const parts = path.split(/[\\/]/);
        if (
            pattern !== "*.sql" ||
            directory === "." ||
            path.startsWith("/") ||
            path.startsWith("\\") ||
            path.startsWith("./") ||
            path.startsWith("../") ||
            /^[A-Za-z]:[\\/]/.test(path) ||
            path.includes("://") ||
            parts.includes(".") ||
            parts.includes("..")
        ) {
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

    function dataMigrationFsPath(path) {
        if (
            path === "." ||
            path.startsWith("/") ||
            /^[A-Za-z]:[\\/]/.test(path) ||
            path.startsWith("./") ||
            path.startsWith("../") ||
            path.includes("://")
        ) {
            return path;
        }
        return `./${path}`;
    }

    function dataMigrationHash(text) {
        let hash = 0x811c9dc5;
        const addByte = (value) => {
            hash ^= value & 0xff;
            hash = Math.imul(hash, 0x01000193) >>> 0;
        };
        for (let index = 0; index < text.length; index += 1) {
            const code = text.codePointAt(index);
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

    function connectionProviderKind(db, operation) {
        const debug = typeof db?.__debug === "function" ? db.__debug() : undefined;
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
            `Sloppy ${operation} only supports sqlite, postgres, and sqlserver connections created by sloppy/data.`,
        );
    }

    function dataMigrationProviderKind(db) {
        return connectionProviderKind(db, "Migrations");
    }

    function resolveDataMigrationProviderKind(db, options) {
        const providerKind = dataMigrationProviderKind(db);
        if (options.provider !== providerKind) {
            throw new TypeError(
                `Sloppy Migrations provider '${options.provider}' does not match connection provider '${providerKind}'.`,
            );
        }
        return providerKind;
    }

    const DATA_MIGRATION_SQL = Object.freeze({
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

    async function listDataMigrationFiles(options) {
        let entries;
        try {
            entries = await Directory.list(dataMigrationFsPath(options.directory));
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

    async function ensureDataMigrationsTable(db, providerKind) {
        await db.exec(DATA_MIGRATION_SQL[providerKind].ensure, []);
    }

    function isMissingDataMigrationsTableError(error) {
        const message = String(error?.message ?? error).toLowerCase();
        return message.includes("_sloppy_migrations")
            && (message.includes("no such table")
                || message.includes("does not exist")
                || message.includes("invalid object name")
                || message.includes("undefined_table")
                || message.includes("42p01"));
    }

    async function readDataAppliedMigrations(db, providerKind, options = {}) {
        if (options.ensure !== false) {
            await ensureDataMigrationsTable(db, providerKind);
        }
        let rows;
        try {
            rows = await db.query(DATA_MIGRATION_SQL[providerKind].select, []);
        } catch (error) {
            if (options.ensure === false && isMissingDataMigrationsTableError(error)) {
                return new Map();
            }
            throw error;
        }
        const applied = new Map();
        for (const row of rows) {
            applied.set(row.name, row);
        }
        return applied;
    }

    function dataMigrationStatusFor(files, applied) {
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
                hash: appliedRow.hash,
                appliedAt: appliedRow.appliedAt,
            });
        });
    }

    async function migrationFilesWithContent(options) {
        const files = await listDataMigrationFiles(options);
        const withContent = [];
        for (const file of files) {
            let sqlText;
            try {
                sqlText = await File.readText(dataMigrationFsPath(file.path));
            } catch (error) {
                throw new Error(
                    `sloppy: migration file is missing or unreadable

Provider:
  ${options.provider}

Path:
  ${file.path}

Fix:
  Ensure every listed migration file exists and is readable before running migrations.`,
                    { cause: error },
                );
            }
            withContent.push({
                ...file,
                sql: sqlText,
                hash: dataMigrationHash(sqlText),
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

    async function dataMigrationStatus(db, options) {
        const checked = normalizeDataMigrationOptions(options);
        const providerKind = resolveDataMigrationProviderKind(db, checked);
        const files = await migrationFilesWithContent(checked);
        const applied = await readDataAppliedMigrations(db, providerKind, { ensure: false });
        const migrations = dataMigrationStatusFor(files, applied);
        const changed = migrations.some((migration) => migration.status === "changed");
        const pending = migrations.filter((migration) => migration.status === "pending").length;
        return Object.freeze({
            provider: providerKind,
            path: checked.path,
            status: changed ? "changed" : pending > 0 ? "pending" : "current",
            pending,
            applied: migrations.filter((migration) => migration.status === "applied").length,
            migrations: Object.freeze(migrations),
        });
    }

    async function applyDataMigrations(db, options) {
        const checked = normalizeDataMigrationOptions(options);
        const providerKind = resolveDataMigrationProviderKind(db, checked);
        const dialect = DATA_MIGRATION_SQL[providerKind];
        const files = await migrationFilesWithContent(checked);
        const applied = await readDataAppliedMigrations(db, providerKind);
        const records = dataMigrationStatusFor(files, applied);
        for (const record of records) {
            assertMigrationHashNotChanged(record);
        }

        let appliedCount = 0;
        for (const file of files) {
            if (applied.has(file.name)) {
                continue;
            }
            let didApply = false;
            try {
                didApply = await db.transaction(async (tx) => {
                    const current = await readDataAppliedMigrations(tx, providerKind);
                    const record = dataMigrationStatusFor([file], current)[0];
                    assertMigrationHashNotChanged(record);
                    if (current.has(file.name)) {
                        return false;
                    }
                    const appliedAt = dialect.appliedAt();
                    const params = appliedAt === undefined
                        ? [file.name, file.hash]
                        : [file.name, file.hash, appliedAt];
                    await tx.exec(dialect.insert, params);
                    await tx.exec(file.sql, []);
                    return true;
                });
            } catch (error) {
                const current = await readDataAppliedMigrations(db, providerKind);
                const record = dataMigrationStatusFor([file], current)[0];
                assertMigrationHashNotChanged(record);
                if (current.has(file.name)) {
                    applied.set(file.name, current.get(file.name));
                    continue;
                }
                throw error;
            }
            if (didApply) {
                applied.set(file.name, { name: file.name, hash: file.hash });
                appliedCount += 1;
            }
        }

        return Object.freeze({
            provider: providerKind,
            path: checked.path,
            applied: appliedCount,
            skipped: files.length - appliedCount,
        });
    }

    async function checkDataProviderHealth(db, options = {}) {
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy ProviderHealth options must be a plain object.");
        }
        const provider = connectionProviderKind(db, "ProviderHealth");
        if (options.provider !== undefined && (typeof options.provider !== "string" || options.provider.length === 0)) {
            throw new TypeError("Sloppy ProviderHealth provider must be a non-empty string.");
        }
        if (options.provider !== undefined && options.provider !== provider) {
            throw new TypeError(
                `Sloppy ProviderHealth provider '${options.provider}' does not match connection provider '${provider}'.`,
            );
        }
        await db.queryOne("select 1 as ok", []);
        return Object.freeze({ provider, ok: true });
    }

    const DataMigrations = Object.freeze({
        apply: applyDataMigrations,
        status: dataMigrationStatus,
    });

    const DataProviderHealth = Object.freeze({
        check: checkDataProviderHealth,
    });

    const data = Object.freeze({
        sqlite,
        postgres,
        sqlserver,
        sql,
        migrations: DataMigrations,
        providerHealth: DataProviderHealth,
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
        if (typeof path !== "string" || path.length === 0 || path.includes("\0")) {
            throw new TypeError(`Sloppy File.${operation} path must be a non-empty string without NUL.`);
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

    function validateFsTempPrefix(options, operation) {
        const prefix = options?.prefix ?? "sloppy-";
        if (typeof prefix !== "string" || prefix.length === 0 || prefix.includes("\0")) {
            throw new TypeError(`${operation} prefix must be a non-empty string without NUL.`);
        }
        return prefix;
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
            const prefix = validateFsTempPrefix(options, "Sloppy File.createTemp");
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
            const prefix = validateFsTempPrefix(options, "Sloppy Directory.createTemp");
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
        sha384(secret, value) {
            return Promise.resolve(sloppyHmac("sha384", secret, value, "Hmac.sha384"));
        },
        sha512(secret, value) {
            return Promise.resolve(sloppyHmac("sha512", secret, value, "Hmac.sha512"));
        },
        async verifySha256(secret, value, signature) {
            const actual = sloppyHmac("sha256", secret, value, "Hmac.verifySha256");
            const expected = sloppyCryptoDataToBytes(signature, "Hmac.verifySha256");
            return ConstantTime.equals(actual, expected);
        },
        async verifySha384(secret, value, signature) {
            const actual = sloppyHmac("sha384", secret, value, "Hmac.verifySha384");
            const expected = sloppyCryptoDataToBytes(signature, "Hmac.verifySha384");
            return ConstantTime.equals(actual, expected);
        },
        async verifySha512(secret, value, signature) {
            const actual = sloppyHmac("sha512", secret, value, "Hmac.verifySha512");
            const expected = sloppyCryptoDataToBytes(signature, "Hmac.verifySha512");
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

    const COMPRESSION_BRIDGE_ERROR_CODES = new Set([
        "SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE",
        "SLOPPY_E_CODEC_DECOMPRESSION_LIMIT_EXCEEDED",
        "SLOPPY_E_CODEC_COMPRESSED_STREAM_CORRUPT",
    ]);

    const COMPRESSION_BRIDGE_ERROR_MESSAGES = Object.freeze({
        SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE: "Compression backend unavailable.",
        SLOPPY_E_CODEC_DECOMPRESSION_LIMIT_EXCEEDED: "Decompression output limit exceeded.",
        SLOPPY_E_CODEC_COMPRESSED_STREAM_CORRUPT: "Compressed stream is corrupt.",
    });

    function normalizeCompressionError(error) {
        if (error instanceof CodecError) {
            return error;
        }
        const message = typeof error?.message === "string" ? error.message : String(error);
        const match = /\b(SLOPPY_E_CODEC_[A-Z_]+)\b(?::\s*)?(.*)$/u.exec(message);
        if (match !== null && COMPRESSION_BRIDGE_ERROR_CODES.has(match[1])) {
            const normalized = codecError(
                match[1],
                COMPRESSION_BRIDGE_ERROR_MESSAGES[match[1]] ?? "Compression backend failed.",
            );
            normalized.cause = error;
            return normalized;
        }
        return error;
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
            const promise = Promise.resolve(invoke(new Uint8Array(bytes))).then(
                (result) => normalizeCompressionResult(result, operation),
                (error) => Promise.reject(normalizeCompressionError(error)),
            );
            return raceCompressionTerminal(promise, options ?? {}, operation);
        } catch (error) {
            return Promise.reject(normalizeCompressionError(error));
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
            const nativeOutput = Promise.resolve(invoke(inputBytes, parsed.codecOptions)).catch((error) =>
                Promise.reject(normalizeCompressionError(error)),
            );
            const output = await raceCompressionTerminal(nativeOutput, parsed, operation);
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

    function sloppyProcessInfo(value) {
        if (value === null || typeof value !== "object") {
            throw sloppyOsError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "OS process info bridge returned invalid data.");
        }
        const invalid = () => {
            throw sloppyOsError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "OS process info bridge returned invalid data.");
        };
        const isProcessId = (pid) => Number.isInteger(pid) && pid >= 0;
        if (
            !isProcessId(value.pid) ||
            !isProcessId(value.parentPid) ||
            typeof value.executablePath !== "string" ||
            typeof value.cwd !== "string" ||
            typeof value.argsAvailable !== "boolean" ||
            !Array.isArray(value.args)
        ) {
            invalid();
        }
        if (!value.argsAvailable && value.args.length !== 0) {
            invalid();
        }
        const args = value.argsAvailable ? value.args.map((arg) => {
            if (typeof arg !== "string") {
                invalid();
            }
            return arg;
        }) : [];
        return Object.freeze({
            pid: value.pid,
            parentPid: value.parentPid,
            executablePath: value.executablePath,
            cwd: value.cwd,
            args: Object.freeze(args),
            argsAvailable: value.argsAvailable,
        });
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
        info() {
            const bridge = sloppyNativeOs();
            if (typeof bridge.processInfo !== "function") {
                throw sloppyOsError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "OS process info bridge is unavailable.");
            }
            return sloppyProcessInfo(bridge.processInfo());
        },
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
            options = validateWorkerOptions(options, "WorkQueue.create");
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
            options = validateWorkerOptions(options, "BackgroundService.create");
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
                failure: undefined,
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
            options = validateWorkerOptions(options, "WorkerPool.create");
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
            options = validateWorkerOptions(options, "Worker.start");
            if (typeof modulePath !== "string" || modulePath.length === 0 || modulePath.includes("\0")) {
                throw new TypeError("Worker.start module path must be a non-empty string without NUL.");
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
                    writer.writeText(frame);
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

    const WEBSOCKET_ROUTE_HANDLER = Symbol.for("sloppy.websocket.routeHandler");
    const WEBSOCKET_ROUTE_OPTIONS = Symbol.for("sloppy.websocket.routeOptions");

    function __sloppyRealtimeWebSocket(handler, options = undefined) {
        if (typeof handler !== "function") {
            throw new TypeError("Sloppy WebSocket route handler must be a function.");
        }
        const routeOptions = __sloppyRealtimeDeepFreeze(options === undefined ? {} : { ...options });
        function sloppyWebSocketRoute(ctx) {
            if (ctx?.__sloppyWebSocketHandshake === true && ctx.__sloppyWebSocket !== undefined) {
                ctx.__sloppyWebSocket.__setContext?.(ctx);
                if (handler.length >= 2) {
                    return handler(ctx, ctx.__sloppyWebSocket);
                }
                return handler(ctx.__sloppyWebSocket);
            }
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

    // Keep this block behaviorally aligned with stdlib/sloppy/cache.js for V8/package runtime.
    function isSchema(value) {
        return value !== null && typeof value === "object" && typeof value.validate === "function";
    }

    const CACHE_MARKER = Symbol("SloppyCache");
    const DEFAULT_MEMORY_MAX_ENTRIES = 1024;
    const DEFAULT_KEY_MAX_LENGTH = 512;
    const DEFAULT_TAG_MAX_LENGTH = 128;
    const DEFAULT_VALUE_MAX_BYTES = 1024 * 1024;
    const DEFAULT_DISTRIBUTED_TABLE = "sloppy_cache_entries";
    const IDENTIFIER_PATTERN = /^[A-Za-z_][0-9A-Za-z_]{0,62}$/u;

    function nowMs(clock = undefined) {
        if (clock !== undefined && typeof clock.now === "function") {
            return clock.now().getTime();
        }
        if (clock !== undefined && typeof clock.monotonicNowMs === "function") {
            return clock.monotonicNowMs();
        }
        return Date.now();
    }

    function nowDate(clock = undefined) {
        if (clock !== undefined && typeof clock.now === "function") {
            return clock.now();
        }
        return new Date();
    }

    function cloneJsonValue(value, subject = "cache value") {
        let text;
        try {
            text = serializeJson(value);
        } catch (error) {
            throw new TypeError(`Sloppy ${subject} must be JSON-serializable.`, { cause: error });
        }
        if (text === undefined) {
            throw new TypeError(`Sloppy ${subject} must be JSON-serializable.`);
        }
        return JSON.parse(text);
    }

    function jsonBytes(text) {
        return Text.utf8.encode(text).byteLength;
    }

    function stableHash(value) {
        const text = String(value);
        let hash = 0x811c9dc5;
        for (let index = 0; index < text.length; index += 1) {
            hash ^= text.charCodeAt(index) & 0xff;
            hash = Math.imul(hash, 0x01000193) >>> 0;
        }
        return `fnv1a32:${hash.toString(16).padStart(8, "0")}`;
    }

    function normalizeName(name, subject = "cache name") {
        if (typeof name !== "string" || name.trim().length === 0 || name.length > 128 || /[\x00-\x1F\x7F]/u.test(name)) {
            throw new TypeError(`Sloppy ${subject} must be a non-empty stable string at most 128 characters.`);
        }
        return name;
    }

    function normalizeTokenName(name, subject = "cache token name") {
        if (typeof name !== "string") {
            throw new TypeError(`Sloppy ${subject} must be a non-empty stable token name.`);
        }
        const normalized = name.trim().toLowerCase().replace(/\s+/gu, "-");
        if (normalized.length === 0 || normalized.length > 128 || !/^[a-z0-9][a-z0-9._-]*$/u.test(normalized)) {
            throw new TypeError(`Sloppy ${subject} must start with a letter or digit and contain only letters, digits, '.', '_', or '-'.`);
        }
        return normalized;
    }

    function normalizeKey(key, options = {}) {
        const maxLength = options.maxKeyLength ?? DEFAULT_KEY_MAX_LENGTH;
        if (!Number.isInteger(maxLength) || maxLength < 1 || maxLength > 4096) {
            throw new TypeError("Sloppy cache maxKeyLength must be an integer from 1 to 4096.");
        }
        if (typeof key !== "string" || key.length === 0 || key.length > maxLength || /[\x00-\x1F\x7F]/u.test(key)) {
            throw new TypeError(`Sloppy cache key must be a non-empty string at most ${maxLength} characters without control characters.`);
        }
        return key;
    }

    function normalizeTag(tag, options = {}) {
        const maxLength = options.maxTagLength ?? DEFAULT_TAG_MAX_LENGTH;
        if (!Number.isInteger(maxLength) || maxLength < 1 || maxLength > 1024) {
            throw new TypeError("Sloppy cache maxTagLength must be an integer from 1 to 1024.");
        }
        if (typeof tag !== "string" || tag.length === 0 || tag.length > maxLength || /[\x00-\x1F\x7F]/u.test(tag)) {
            throw new TypeError(`Sloppy cache tag must be a non-empty string at most ${maxLength} characters without control characters.`);
        }
        return tag;
    }

    function normalizeTags(tags, options = {}) {
        if (tags === undefined) {
            return Object.freeze([]);
        }
        if (!Array.isArray(tags)) {
            throw new TypeError("Sloppy cache tags must be an array.");
        }
        return Object.freeze([...new Set(tags.map((tag) => normalizeTag(tag, options)))]);
    }

    function normalizeTtlMs(value, subject = "ttlMs") {
        if (value === undefined) {
            return undefined;
        }
        if (!Number.isInteger(value) || value < 0 || value > 0x7fffffff) {
            throw new TypeError(`Sloppy cache ${subject} must be an integer from 0 to 2147483647.`);
        }
        return value;
    }

    function normalizeAbsoluteExpiration(value) {
        if (value === undefined) {
            return undefined;
        }
        const date = value instanceof Date ? value : new Date(value);
        const time = date.getTime();
        if (!Number.isFinite(time)) {
            throw new TypeError("Sloppy cache absoluteExpiration must be a valid Date or ISO timestamp.");
        }
        return time;
    }

    function normalizeSchema(value) {
        if (value === undefined) {
            return undefined;
        }
        if (!isSchema(value)) {
            throw new TypeError("Sloppy cache schema must be a Schema value.");
        }
        return value;
    }

    function normalizeEntryOptions(options = {}) {
        if (options === undefined) {
            return Object.freeze({});
        }
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy cache entry options must be a plain object.");
        }
        return Object.freeze({
            ttlMs: normalizeTtlMs(options.ttlMs),
            absoluteExpiration: normalizeAbsoluteExpiration(options.absoluteExpiration),
            slidingExpirationMs: normalizeTtlMs(options.slidingExpirationMs, "slidingExpirationMs"),
            tags: normalizeTags(options.tags, options),
            schema: normalizeSchema(options.schema),
            staleWhileRevalidateMs: normalizeTtlMs(options.staleWhileRevalidateMs, "staleWhileRevalidateMs"),
            stampedeProtection: options.stampedeProtection !== false,
            namespace: options.namespace === undefined ? undefined : normalizeName(options.namespace, "cache namespace"),
            cacheNull: options.cacheNull !== false,
            signal: options.signal,
        });
    }

    function expiresAtFromOptions(options, clock = undefined) {
        const base = nowMs(clock);
        let expiresAt = options.ttlMs === undefined ? undefined : base + options.ttlMs;
        if (options.absoluteExpiration !== undefined) {
            expiresAt = expiresAt === undefined ? options.absoluteExpiration : Math.min(expiresAt, options.absoluteExpiration);
        }
        return expiresAt;
    }

    function refreshSlidingExpiration(baseMs, slidingExpirationMs, absoluteExpiration = undefined) {
        const refreshed = baseMs + slidingExpirationMs;
        return absoluteExpiration === undefined ? refreshed : Math.min(refreshed, absoluteExpiration);
    }

    function isExpired(entry, clock = undefined) {
        return entry.expiresAt !== undefined && nowMs(clock) >= entry.expiresAt;
    }

    function validateValueWithSchema(value, schema, key) {
        if (schema === undefined) {
            return value;
        }
        const result = schema.validate(value);
        if (result.ok) {
            return result.value;
        }
        throw new SloppyCacheError("SLOPPY_E_CACHE_SCHEMA_MISMATCH", `Sloppy cache value for '${stableHash(key)}' failed schema validation.`, {
            keyHash: stableHash(key),
            issues: result.issues,
        });
    }

    function cacheToken(name = "default") {
        return `cache.${normalizeTokenName(name, "cache token name")}`;
    }

    function cacheMetricName(operation) {
        switch (operation) {
        case "gets":
            return "cache.gets.total";
        case "hits":
            return "cache.hits.total";
        case "misses":
            return "cache.misses.total";
        case "sets":
            return "cache.sets.total";
        case "removes":
            return "cache.removes.total";
        case "evictions":
            return "cache.evictions.total";
        case "expired":
            return "cache.expired.total";
        case "tagInvalidations":
            return "cache.tag_invalidations.total";
        case "factoryRuns":
            return "cache.get_or_create.factory.total";
        case "stampedeWaiters":
            return "cache.stampede.waiters.total";
        case "staleHits":
            return "cache.stale_hits.total";
        default:
            return undefined;
        }
    }

    function recordCacheMetric(cache, operation) {
        const name = cacheMetricName(operation);
        if (name === undefined || cache.metrics === undefined || cache.metrics === null) {
            return;
        }
        const labels = Object.freeze({
            cache: cache.name,
            backend: cache.kind,
            operation,
        });
        try {
            if (typeof cache.metrics.increment === "function") {
                cache.metrics.increment(name, labels);
                return;
            }
            cache.metrics.counter?.(name, {
                description: "Cache operations by cache name, backend, and operation.",
            })?.inc(labels);
        } catch {
            // Metrics must not change cache behavior.
        }
    }

    class SloppyCacheError extends Error {
        constructor(code, message, details = undefined) {
            super(message);
            this.name = "SloppyCacheError";
            this.code = code;
            this.details = details === undefined ? undefined : Object.freeze({ ...details });
            this.__sloppyCacheError = true;
        }
    }

    class BaseCache {
        constructor(name, kind, options = {}) {
            this.name = normalizeName(name ?? "default");
            this.kind = kind;
            this.namespace = normalizeName(options.namespace ?? this.name, "cache namespace");
            this.maxKeyLength = options.maxKeyLength ?? DEFAULT_KEY_MAX_LENGTH;
            this.maxTagLength = options.maxTagLength ?? DEFAULT_TAG_MAX_LENGTH;
            this.clock = options.clock;
            this.metrics = options.metrics;
            this.disposed = false;
            this.inflight = new Map();
            this.counters = {
                gets: 0,
                hits: 0,
                misses: 0,
                sets: 0,
                removes: 0,
                evictions: 0,
                expired: 0,
                tagInvalidations: 0,
                factoryRuns: 0,
                stampedeWaiters: 0,
                staleHits: 0,
            };
            Object.defineProperty(this, CACHE_MARKER, { value: true });
            Object.defineProperty(this, "__sloppyCache", { value: true, enumerable: true });
        }

        _assertOpen(operation) {
            if (this.disposed) {
                throw new SloppyCacheError("SLOPPY_E_CACHE_DISPOSED", `Sloppy cache '${this.name}' is disposed.`, { operation });
            }
        }

        _key(key) {
            return normalizeKey(key, this);
        }

        _entryOptions(options = {}) {
            return normalizeEntryOptions(options);
        }

        _record(operation) {
            if (Object.prototype.hasOwnProperty.call(this.counters, operation)) {
                this.counters[operation] += 1;
            }
            recordCacheMetric(this, operation);
        }

        __setMetricsRegistry(metrics) {
            this.metrics = metrics;
            return this;
        }

        async getOrCreate(key, options, factory) {
            this._assertOpen("getOrCreate");
            const normalizedKey = this._key(key);
            const normalizedOptions = this._entryOptions(options);
            if (typeof factory !== "function") {
                throw new TypeError("Sloppy cache getOrCreate factory must be a function.");
            }
            const existing = await this.get(normalizedKey, normalizedOptions);
            if (existing !== undefined) {
                return existing;
            }
            if (normalizedOptions.stampedeProtection === false) {
                return this._runFactory(normalizedKey, normalizedOptions, factory);
            }
            const inflightKey = `${this.namespace}\0${normalizedKey}`;
            const current = this.inflight.get(inflightKey);
            if (current !== undefined) {
                this._record("stampedeWaiters");
                return current;
            }
            const created = this._runFactory(normalizedKey, normalizedOptions, factory)
                .finally(() => {
                    this.inflight.delete(inflightKey);
                });
            this.inflight.set(inflightKey, created);
            return created;
        }

        async _runFactory(key, options, factory) {
            this._record("factoryRuns");
            if (options.signal?.aborted === true) {
                options.signal.throwIfAborted?.();
                throw new SloppyCacheError("SLOPPY_E_CACHE_CANCELLED", "Sloppy cache factory was cancelled before it started.", {
                    keyHash: stableHash(key),
                });
            }
            const value = await factory(options.signal);
            if (value === null && options.cacheNull === false) {
                return value;
            }
            const validated = validateValueWithSchema(value, options.schema, key);
            await this.set(key, validated, options);
            return validated;
        }

        delete(key) {
            return this.remove(key);
        }

        invalidate(key) {
            return this.remove(key);
        }

        stats() {
            return Object.freeze({
                name: this.name,
                kind: this.kind,
                namespace: this.namespace,
                disposed: this.disposed,
                ...this.counters,
            });
        }

        dispose() {
            this.disposed = true;
            this.inflight.clear();
        }
    }

    class MemoryCache extends BaseCache {
        constructor(name, options = {}) {
            super(name, "memory", options);
            if (!isPlainObject(options)) {
                throw new TypeError("Sloppy memory cache options must be a plain object.");
            }
            this.maxEntries = options.maxEntries ?? DEFAULT_MEMORY_MAX_ENTRIES;
            if (!Number.isInteger(this.maxEntries) || this.maxEntries < 1 || this.maxEntries > 1_000_000) {
                throw new TypeError("Sloppy memory cache maxEntries must be an integer from 1 to 1000000.");
            }
            this.defaultTtlMs = normalizeTtlMs(options.ttlMs);
            this.entries = new Map();
            Object.seal(this);
        }

        _entryOptions(options = {}) {
            const normalized = normalizeEntryOptions(options);
            return Object.freeze({
                ...normalized,
                ttlMs: normalized.ttlMs ?? this.defaultTtlMs,
            });
        }

        _entry(key, value, options) {
            const json = serializeJson(value);
            if (json === undefined) {
                throw new TypeError("Sloppy memory cache value must be JSON-serializable.");
            }
            if (jsonBytes(json) > (options.maxValueBytes ?? DEFAULT_VALUE_MAX_BYTES)) {
                throw new SloppyCacheError("SLOPPY_E_CACHE_VALUE_TOO_LARGE", "Sloppy memory cache value exceeds maxValueBytes.", {
                    keyHash: stableHash(key),
                });
            }
            const timestamp = nowMs(this.clock);
            return {
                key,
                valueJson: json,
                tags: options.tags,
                createdAt: timestamp,
                updatedAt: timestamp,
                lastAccessedAt: timestamp,
                absoluteExpiration: options.absoluteExpiration,
                expiresAt: expiresAtFromOptions(options, this.clock),
                slidingExpirationMs: options.slidingExpirationMs,
            };
        }

        _deleteExpired() {
            for (const [key, entry] of this.entries) {
                if (isExpired(entry, this.clock)) {
                    this.entries.delete(key);
                    this._record("expired");
                }
            }
        }

        _evictIfNeeded() {
            if (this.entries.size <= this.maxEntries) {
                return;
            }
            this._deleteExpired();
            while (this.entries.size > this.maxEntries) {
                let oldestKey;
                let oldestAccess = Infinity;
                for (const [key, entry] of this.entries) {
                    if (entry.lastAccessedAt < oldestAccess) {
                        oldestAccess = entry.lastAccessedAt;
                        oldestKey = key;
                    }
                }
                if (oldestKey === undefined) {
                    return;
                }
                this.entries.delete(oldestKey);
                this._record("evictions");
            }
        }

        async get(key, schemaOrOptions = undefined) {
            this._assertOpen("get");
            const normalizedKey = this._key(key);
            const options = isSchema(schemaOrOptions)
                ? Object.freeze({ schema: schemaOrOptions })
                : this._entryOptions(schemaOrOptions ?? {});
            this._record("gets");
            const entry = this.entries.get(normalizedKey);
            if (entry === undefined) {
                this._record("misses");
                return undefined;
            }
            if (isExpired(entry, this.clock)) {
                this.entries.delete(normalizedKey);
                this._record("expired");
                this._record("misses");
                return undefined;
            }
            entry.lastAccessedAt = nowMs(this.clock);
            if (entry.slidingExpirationMs !== undefined) {
                entry.expiresAt = refreshSlidingExpiration(entry.lastAccessedAt, entry.slidingExpirationMs, entry.absoluteExpiration);
            }
            this._record("hits");
            return validateValueWithSchema(JSON.parse(entry.valueJson), options.schema, normalizedKey);
        }

        async has(key) {
            return (await this.get(key)) !== undefined;
        }

        async set(key, value, options = {}) {
            this._assertOpen("set");
            const normalizedKey = this._key(key);
            const normalizedOptions = this._entryOptions(options);
            const validated = validateValueWithSchema(value, normalizedOptions.schema, normalizedKey);
            this.entries.set(normalizedKey, this._entry(normalizedKey, validated, normalizedOptions));
            this._record("sets");
            this._evictIfNeeded();
            return this;
        }

        async remove(key) {
            this._assertOpen("remove");
            const removed = this.entries.delete(this._key(key));
            if (removed) {
                this._record("removes");
            }
            return removed;
        }

        async invalidateTag(tag) {
            return this.invalidateTags([tag]);
        }

        async invalidateTags(tags) {
            this._assertOpen("invalidateTags");
            const normalized = normalizeTags(tags, this);
            let removed = 0;
            for (const [key, entry] of this.entries) {
                if (entry.tags.some((tag) => normalized.includes(tag))) {
                    this.entries.delete(key);
                    removed += 1;
                }
            }
            this._record("tagInvalidations");
            return removed;
        }

        async clear(options = {}) {
            this._assertOpen("clear");
            if (options !== undefined && !isPlainObject(options)) {
                throw new TypeError("Sloppy cache clear options must be a plain object.");
            }
            const count = this.entries.size;
            this.entries.clear();
            return count;
        }

        async cleanup() {
            this._assertOpen("cleanup");
            const before = this.entries.size;
            this._deleteExpired();
            return before - this.entries.size;
        }

        stats() {
            return Object.freeze({
                ...super.stats(),
                entries: this.entries.size,
                maxEntries: this.maxEntries,
            });
        }

        dispose() {
            super.dispose();
            this.entries.clear();
        }
    }

    function providerKind(db, operation) {
        const debug = typeof db?.__debug === "function" ? db.__debug() : undefined;
        if (debug?.kind === "sqlite-connection" && isRealDataProvider(db, "sqlite")) {
            return "sqlite";
        }
        if (debug?.kind === "postgres-connection" && isRealDataProvider(db, "postgres")) {
            return "postgres";
        }
        if (debug?.kind === "sqlserver-connection" && isRealDataProvider(db, "sqlserver")) {
            return "sqlserver";
        }
        const expected = operation === "sqlServer" ? "sqlserver" : operation;
        throw new TypeError(`Sloppy Cache.${operation} requires a real ${expected} connection from sloppy/data.`);
    }

    function placeholder(kind, index) {
        return kind === "postgres" ? `$${index}` : "?";
    }

    function validateTableName(value) {
        const table = value ?? DEFAULT_DISTRIBUTED_TABLE;
        if (typeof table !== "string" || !IDENTIFIER_PATTERN.test(table)) {
            throw new TypeError("Sloppy distributed cache table must be a simple SQL identifier.");
        }
        return table;
    }

    function isSqlServerDuplicateKeyError(error) {
        const code = error?.number ?? error?.code ?? error?.state;
        if (code === 2627 || code === 2601 || code === "2627" || code === "2601") {
            return true;
        }
        const message = String(error?.message ?? error ?? "");
        return /duplicate key|unique constraint|violation of (primary key|unique)/iu.test(message);
    }

    function affectedRows(result) {
        const value = result?.affectedRows ?? result?.affected_rows ?? result?.rowCount ?? result?.rowsAffected;
        return Number.isInteger(value) ? value : undefined;
    }

    function distributedSql(kind, table) {
        const columns = "namespace, cache_key, value_json, created_at, updated_at, expires_at, absolute_expires_at, sliding_expiration_ms, tags_json";
        const values = Array.from({ length: 9 }, (_, index) => placeholder(kind, index + 1)).join(", ");
        const updateSet = [
            "value_json = excluded.value_json",
            "updated_at = excluded.updated_at",
            "expires_at = excluded.expires_at",
            "absolute_expires_at = excluded.absolute_expires_at",
            "sliding_expiration_ms = excluded.sliding_expiration_ms",
            "tags_json = excluded.tags_json",
        ].join(", ");
        if (kind === "postgres") {
            return Object.freeze({
                ensure: `create table if not exists ${table} (` +
                    "namespace text not null, cache_key text not null, value_json text not null, " +
                    "created_at text not null, updated_at text not null, expires_at text null, " +
                    "absolute_expires_at text null, " +
                    "sliding_expiration_ms integer null, tags_json text not null, primary key (namespace, cache_key))",
                ensureAbsoluteExpires: `alter table ${table} add column if not exists absolute_expires_at text null`,
                get: `select value_json, expires_at, absolute_expires_at, sliding_expiration_ms, tags_json from ${table} where namespace = $1 and cache_key = $2`,
                selectNamespace: `select cache_key, tags_json from ${table} where namespace = $1`,
                deleteOne: `delete from ${table} where namespace = $1 and cache_key = $2`,
                clearNamespace: `delete from ${table} where namespace = $1`,
                clearAll: `delete from ${table}`,
                cleanup: `delete from ${table} where expires_at is not null and expires_at <= $1`,
                set: `insert into ${table} (${columns}) values (${values}) on conflict (namespace, cache_key) do update set ${updateSet}`,
            });
        }
        if (kind === "sqlserver") {
            return Object.freeze({
                ensure: `if object_id(N'dbo.${table}', N'U') is null begin create table dbo.${table} (` +
                    "namespace nvarchar(128) not null, cache_key nvarchar(256) not null, value_json nvarchar(max) not null, " +
                    "created_at nvarchar(64) not null, updated_at nvarchar(64) not null, expires_at nvarchar(64) null, " +
                    "absolute_expires_at nvarchar(64) null, " +
                    "sliding_expiration_ms int null, tags_json nvarchar(max) not null, constraint " +
                    `pk_${table} primary key (namespace, cache_key)) end`,
                ensureAbsoluteExpires: `if col_length(N'dbo.${table}', N'absolute_expires_at') is null alter table dbo.${table} add absolute_expires_at nvarchar(64) null`,
                get: `select value_json, expires_at, absolute_expires_at, sliding_expiration_ms, tags_json from dbo.${table} where namespace = ? and cache_key = ?`,
                selectNamespace: `select cache_key, tags_json from dbo.${table} where namespace = ?`,
                deleteOne: `delete from dbo.${table} where namespace = ? and cache_key = ?`,
                clearNamespace: `delete from dbo.${table} where namespace = ?`,
                clearAll: `delete from dbo.${table}`,
                cleanup: `delete from dbo.${table} where expires_at is not null and expires_at <= ?`,
                update: `update dbo.${table} set value_json = ?, updated_at = ?, expires_at = ?, absolute_expires_at = ?, sliding_expiration_ms = ?, tags_json = ? where namespace = ? and cache_key = ?`,
                insert: `insert into dbo.${table} (${columns}) values (${values})`,
            });
        }
        return Object.freeze({
            ensure: `create table if not exists ${table} (` +
                "namespace text not null, cache_key text not null, value_json text not null, " +
                "created_at text not null, updated_at text not null, expires_at text null, " +
                "absolute_expires_at text null, " +
                "sliding_expiration_ms integer null, tags_json text not null, primary key (namespace, cache_key))",
            ensureAbsoluteExpires: `alter table ${table} add column absolute_expires_at text null`,
            get: `select value_json, expires_at, absolute_expires_at, sliding_expiration_ms, tags_json from ${table} where namespace = ? and cache_key = ?`,
            selectNamespace: `select cache_key, tags_json from ${table} where namespace = ?`,
            deleteOne: `delete from ${table} where namespace = ? and cache_key = ?`,
            clearNamespace: `delete from ${table} where namespace = ?`,
            clearAll: `delete from ${table}`,
            cleanup: `delete from ${table} where expires_at is not null and expires_at <= ?`,
            set: `insert into ${table} (${columns}) values (${values}) on conflict(namespace, cache_key) do update set ${updateSet}`,
        });
    }

    class DistributedCache extends BaseCache {
        constructor(name, db, kind, options = {}) {
            super(name, kind, options);
            if (!isPlainObject(options)) {
                throw new TypeError("Sloppy distributed cache options must be a plain object.");
            }
            this.db = db;
            this.provider = kind;
            this.table = validateTableName(options.table);
            this.maxValueBytes = options.maxValueBytes ?? DEFAULT_VALUE_MAX_BYTES;
            if (!Number.isInteger(this.maxValueBytes) || this.maxValueBytes < 1) {
                throw new TypeError("Sloppy distributed cache maxValueBytes must be a positive integer.");
            }
            this.defaultTtlMs = normalizeTtlMs(options.ttlMs);
            this.sql = distributedSql(kind, this.table);
            this.initialized = false;
            Object.seal(this);
        }

        async _ensure() {
            if (this.initialized) {
                return;
            }
            await this.db.exec(this.sql.ensure, []);
            if (this.sql.ensureAbsoluteExpires !== undefined) {
                try {
                    await this.db.exec(this.sql.ensureAbsoluteExpires, []);
                } catch (error) {
                    if (!/duplicate column|already exists/iu.test(String(error?.message ?? error))) {
                        throw error;
                    }
                }
            }
            this.initialized = true;
        }

        _entryOptions(options = {}) {
            const normalized = normalizeEntryOptions(options);
            return Object.freeze({
                ...normalized,
                ttlMs: normalized.ttlMs ?? this.defaultTtlMs,
            });
        }

        _iso(timeMs) {
            return timeMs === undefined ? null : new Date(timeMs).toISOString();
        }

        _time(value) {
            if (value === null || value === undefined || value === "") {
                return undefined;
            }
            const time = Date.parse(String(value));
            return Number.isFinite(time) ? time : undefined;
        }

        _rowOptions(row, fallbackOptions = {}) {
            const expiresAt = this._time(row.expires_at ?? row.expiresAt);
            const absoluteExpiration = this._time(row.absolute_expires_at ?? row.absoluteExpiresAt);
            const tags = JSON.parse(row.tags_json ?? row.tagsJson ?? "[]");
            const slidingExpirationMs = row.sliding_expiration_ms ?? row.slidingExpirationMs;
            const options = {
                tags,
                schema: fallbackOptions.schema,
            };
            if (slidingExpirationMs !== null && slidingExpirationMs !== undefined) {
                options.slidingExpirationMs = Number(slidingExpirationMs);
                if (absoluteExpiration !== undefined) {
                    options.absoluteExpiration = absoluteExpiration;
                } else if (expiresAt !== undefined) {
                    options.absoluteExpiration = expiresAt;
                }
            } else if (expiresAt !== undefined) {
                options.absoluteExpiration = expiresAt;
            }
            return options;
        }

        async _refreshSliding(normalizedKey, row, value, fallbackOptions) {
            const slidingExpirationMs = row.sliding_expiration_ms ?? row.slidingExpirationMs;
            if (slidingExpirationMs === null || slidingExpirationMs === undefined) {
                return;
            }
            await this.set(normalizedKey, value, {
                ...this._rowOptions(row, fallbackOptions),
                slidingExpirationMs: Number(slidingExpirationMs),
            });
        }

        async _getWithMetadata(normalizedKey, schemaOrOptions = undefined) {
            await this._ensure();
            const options = isSchema(schemaOrOptions)
                ? Object.freeze({ schema: schemaOrOptions })
                : this._entryOptions(schemaOrOptions ?? {});
            const row = await this.db.queryOne(this.sql.get, [this.namespace, normalizedKey]);
            if (row === null || row === undefined) {
                return undefined;
            }
            const expiresAt = this._time(row.expires_at ?? row.expiresAt);
            if (expiresAt !== undefined && nowMs(this.clock) >= expiresAt) {
                await this.remove(normalizedKey);
                this._record("expired");
                return undefined;
            }
            const value = validateValueWithSchema(JSON.parse(row.value_json ?? row.valueJson), options.schema, normalizedKey);
            const entryOptions = this._rowOptions(row, options);
            await this._refreshSliding(normalizedKey, row, value, options);
            return Object.freeze({ value, options: entryOptions });
        }

        async get(key, schemaOrOptions = undefined) {
            this._assertOpen("get");
            await this._ensure();
            const normalizedKey = this._key(key);
            this._record("gets");
            const entry = await this._getWithMetadata(normalizedKey, schemaOrOptions);
            if (entry === undefined) {
                this._record("misses");
                return undefined;
            }
            this._record("hits");
            return entry.value;
        }

        async has(key) {
            return (await this.get(key)) !== undefined;
        }

        async set(key, value, options = {}) {
            this._assertOpen("set");
            await this._ensure();
            const normalizedKey = this._key(key);
            const normalizedOptions = this._entryOptions(options);
            const validated = validateValueWithSchema(value, normalizedOptions.schema, normalizedKey);
            const json = serializeJson(validated);
            if (jsonBytes(json) > this.maxValueBytes) {
                throw new SloppyCacheError("SLOPPY_E_CACHE_VALUE_TOO_LARGE", "Sloppy distributed cache value exceeds maxValueBytes.", {
                    keyHash: stableHash(normalizedKey),
                    provider: this.provider,
                });
            }
            const timestamp = nowDate(this.clock).toISOString();
            const expiresAt = this._iso(expiresAtFromOptions(normalizedOptions, this.clock));
            const absoluteExpiresAt = this._iso(normalizedOptions.absoluteExpiration);
            const tagsJson = serializeJson(normalizedOptions.tags);
            if (this.provider === "sqlserver") {
                try {
                    await this.db.exec(this.sql.insert, [
                        this.namespace,
                        normalizedKey,
                        json,
                        timestamp,
                        timestamp,
                        expiresAt,
                        absoluteExpiresAt,
                        normalizedOptions.slidingExpirationMs ?? null,
                        tagsJson,
                    ]);
                } catch (error) {
                    if (!isSqlServerDuplicateKeyError(error)) {
                        throw error;
                    }
                    const updateResult = await this.db.exec(this.sql.update, [
                        json,
                        timestamp,
                        expiresAt,
                        absoluteExpiresAt,
                        normalizedOptions.slidingExpirationMs ?? null,
                        tagsJson,
                        this.namespace,
                        normalizedKey,
                    ]);
                    const updated = affectedRows(updateResult);
                    if (updated !== undefined && updated < 1) {
                        throw new SloppyCacheError("SLOPPY_E_CACHE_WRITE_FAILED", "Sloppy SQL Server distributed cache update did not write a row.", {
                            keyHash: stableHash(normalizedKey),
                            provider: this.provider,
                        });
                    }
                }
            } else {
                await this.db.exec(this.sql.set, [
                    this.namespace,
                    normalizedKey,
                    json,
                    timestamp,
                    timestamp,
                    expiresAt,
                    absoluteExpiresAt,
                    normalizedOptions.slidingExpirationMs ?? null,
                    tagsJson,
                ]);
            }
            this._record("sets");
            return this;
        }

        async remove(key) {
            this._assertOpen("remove");
            await this._ensure();
            await this.db.exec(this.sql.deleteOne, [this.namespace, this._key(key)]);
            this._record("removes");
            return true;
        }

        async invalidateTag(tag) {
            return this.invalidateTags([tag]);
        }

        async invalidateTags(tags) {
            this._assertOpen("invalidateTags");
            await this._ensure();
            const normalized = normalizeTags(tags, this);
            const rows = await this.db.query(this.sql.selectNamespace, [this.namespace]);
            let removed = 0;
            for (const row of rows) {
                const rowTags = JSON.parse(row.tags_json ?? row.tagsJson ?? "[]");
                if (rowTags.some((current) => normalized.includes(current))) {
                    await this.db.exec(this.sql.deleteOne, [this.namespace, row.cache_key ?? row.cacheKey]);
                    removed += 1;
                }
            }
            this._record("tagInvalidations");
            return removed;
        }

        async clear(options = {}) {
            this._assertOpen("clear");
            await this._ensure();
            if (options !== undefined && !isPlainObject(options)) {
                throw new TypeError("Sloppy distributed cache clear options must be a plain object.");
            }
            if (options.dangerouslyClearAll === true) {
                await this.db.exec(this.sql.clearAll, []);
                return true;
            }
            await this.db.exec(this.sql.clearNamespace, [this.namespace]);
            return true;
        }

        async cleanup() {
            this._assertOpen("cleanup");
            await this._ensure();
            await this.db.exec(this.sql.cleanup, [nowDate(this.clock).toISOString()]);
            return true;
        }
    }

    class HybridCache extends BaseCache {
        constructor(name, options) {
            if (!isPlainObject(options)) {
                throw new TypeError("Sloppy hybrid cache options must be a plain object.");
            }
            super(name, "hybrid", options);
            if (!isCache(options.memory) || options.memory.kind !== "memory") {
                throw new TypeError("Sloppy hybrid cache memory must be a memory Cache instance.");
            }
            if (!isCache(options.distributed) || options.distributed.kind === "memory" || options.distributed.kind === "hybrid") {
                throw new TypeError("Sloppy hybrid cache distributed must be a distributed Cache instance.");
            }
            this.memory = options.memory;
            this.distributed = options.distributed;
            this.populateMemoryOnDistributedHit = options.populateMemoryOnDistributedHit !== false;
            this.failOpenOnDistributedRead = options.failOpenOnDistributedRead === true;
            this.owned = options.owned !== false;
            Object.seal(this);
        }

        async get(key, schemaOrOptions = undefined) {
            this._assertOpen("get");
            this._record("gets");
            const normalizedKey = this._key(key);
            const memory = await this.memory.get(normalizedKey, schemaOrOptions);
            if (memory !== undefined) {
                this._record("hits");
                return memory;
            }
            let distributed;
            let distributedOptions;
            try {
                if (typeof this.distributed._getWithMetadata === "function") {
                    const entry = await this.distributed._getWithMetadata(normalizedKey, schemaOrOptions);
                    distributed = entry?.value;
                    distributedOptions = entry?.options;
                } else {
                    distributed = await this.distributed.get(normalizedKey, schemaOrOptions);
                }
            } catch (error) {
                if (this.failOpenOnDistributedRead) {
                    this._record("misses");
                    return undefined;
                }
                throw error;
            }
            if (distributed === undefined) {
                this._record("misses");
                return undefined;
            }
            if (this.populateMemoryOnDistributedHit) {
                await this.memory.set(normalizedKey, distributed, {
                    ...(distributedOptions ?? {}),
                    ...(isPlainObject(schemaOrOptions) ? schemaOrOptions : {}),
                });
            }
            this._record("hits");
            return distributed;
        }

        async has(key) {
            return (await this.get(key)) !== undefined;
        }

        async set(key, value, options = {}) {
            this._assertOpen("set");
            await this.distributed.set(key, value, options);
            await this.memory.set(key, value, options);
            this._record("sets");
            return this;
        }

        async remove(key) {
            this._assertOpen("remove");
            await Promise.all([this.memory.remove(key), this.distributed.remove(key)]);
            this._record("removes");
            return true;
        }

        async invalidateTag(tag) {
            return this.invalidateTags([tag]);
        }

        async invalidateTags(tags) {
            this._assertOpen("invalidateTags");
            const removed = await Promise.all([this.memory.invalidateTags(tags), this.distributed.invalidateTags(tags)]);
            this._record("tagInvalidations");
            return removed.reduce((sum, value) => sum + Number(value ?? 0), 0);
        }

        async clear(options = {}) {
            this._assertOpen("clear");
            await Promise.all([this.memory.clear(options), this.distributed.clear(options)]);
            return true;
        }

        async cleanup(options = {}) {
            this._assertOpen("cleanup");
            await Promise.all([this.memory.cleanup(options), this.distributed.cleanup(options)]);
            return true;
        }

        stats() {
            return Object.freeze({
                ...super.stats(),
                memory: this.memory.stats(),
                distributed: this.distributed.stats(),
            });
        }

        dispose() {
            super.dispose();
            if (this.owned) {
                this.memory.dispose?.();
                this.distributed.dispose?.();
            }
        }
    }

    class NoopCache extends BaseCache {
        constructor(name = "noop") {
            super(name, "noop", {});
        }
        async get(key, schemaOrOptions = undefined) {
            this._assertOpen("get");
            this._key(key);
            if (isSchema(schemaOrOptions)) {
                return undefined;
            }
            this._entryOptions(schemaOrOptions ?? {});
            return undefined;
        }
        async has(key) {
            this._assertOpen("has");
            this._key(key);
            return false;
        }
        async set(key, value, options = {}) {
            this._assertOpen("set");
            this._key(key);
            this._entryOptions(options);
            cloneJsonValue(value);
            return this;
        }
        async remove(key) {
            this._assertOpen("remove");
            this._key(key);
            return false;
        }
        async invalidateTag(tag) {
            this._assertOpen("invalidateTag");
            normalizeTag(tag, this);
            return 0;
        }
        async invalidateTags(tags) {
            this._assertOpen("invalidateTags");
            normalizeTags(tags, this);
            return 0;
        }
        async clear(options = {}) {
            this._assertOpen("clear");
            if (options !== undefined && !isPlainObject(options)) {
                throw new TypeError("Sloppy cache clear options must be a plain object.");
            }
            return 0;
        }
        async cleanup() {
            this._assertOpen("cleanup");
            return 0;
        }
    }

    function isCache(value) {
        return value !== null && typeof value === "object" && value[CACHE_MARKER] === true && value.__sloppyCache === true;
    }

    function cacheFromFactoryArgs(kind, nameOrOptions, maybeOptions) {
        if (typeof nameOrOptions === "string") {
            return { name: nameOrOptions, options: maybeOptions ?? {} };
        }
        return { name: "default", options: nameOrOptions ?? {} };
    }

    function distributedFromFactoryArgs(operation, dbOrOptions, maybeOptions) {
        if (isPlainObject(dbOrOptions) && dbOrOptions.db !== undefined) {
            return {
                name: dbOrOptions.name ?? "default",
                db: dbOrOptions.db,
                options: { ...dbOrOptions, ...maybeOptions },
            };
        }
        return {
            name: maybeOptions?.name ?? "default",
            db: dbOrOptions,
            options: maybeOptions ?? {},
        };
    }

    function createDistributed(operation, expectedKind, dbOrOptions, maybeOptions) {
        const { name, db, options } = distributedFromFactoryArgs(operation, dbOrOptions, maybeOptions);
        const actualKind = providerKind(db, operation);
        if (actualKind !== expectedKind) {
            throw new TypeError(`Sloppy Cache.${operation} expected ${expectedKind} connection, got ${actualKind}.`);
        }
        return new DistributedCache(name, db, actualKind, options);
    }

    function key(...parts) {
        if (parts.length === 0) {
            throw new TypeError("Sloppy Cache.key requires at least one part.");
        }
        return parts.map((part) => {
            if (part === null || part === undefined) {
                throw new TypeError("Sloppy Cache.key parts must not be null or undefined.");
            }
            return encodeURIComponent(String(part));
        }).join(":");
    }

    function tags(...values) {
        return normalizeTags(values.flat());
    }

    const Cache = Object.freeze({
        memory(nameOrOptions = undefined, maybeOptions = undefined) {
            const { name, options } = cacheFromFactoryArgs("memory", nameOrOptions, maybeOptions);
            return new MemoryCache(name, options);
        },
        sqlite(dbOrOptions, maybeOptions = undefined) {
            return createDistributed("sqlite", "sqlite", dbOrOptions, maybeOptions);
        },
        postgres(dbOrOptions, maybeOptions = undefined) {
            return createDistributed("postgres", "postgres", dbOrOptions, maybeOptions);
        },
        sqlServer(dbOrOptions, maybeOptions = undefined) {
            return createDistributed("sqlServer", "sqlserver", dbOrOptions, maybeOptions);
        },
        sqlserver(dbOrOptions, maybeOptions = undefined) {
            return createDistributed("sqlServer", "sqlserver", dbOrOptions, maybeOptions);
        },
        distributed(kind, db, options = undefined) {
            if (kind === "sqlite") {
                return createDistributed("sqlite", "sqlite", db, options);
            }
            if (kind === "postgres") {
                return createDistributed("postgres", "postgres", db, options);
            }
            if (kind === "sqlserver" || kind === "sqlServer") {
                return createDistributed("sqlServer", "sqlserver", db, options);
            }
            throw new TypeError("Sloppy Cache.distributed kind must be sqlite, postgres, or sqlserver.");
        },
        hybrid(name, options) {
            return new HybridCache(name, options);
        },
        noop(name = "noop") {
            return new NoopCache(name);
        },
        token: cacheToken,
        key,
        tags,
        isCache,
        keyHash: stableHash,
        __testing: Object.freeze({
            distributedSql,
            normalizeEntryOptions,
        }),
    });
    const REALTIME_CHANNEL = Symbol.for("sloppy.realtime.channel");
    const REALTIME_EVENT = Symbol.for("sloppy.realtime.event");
    const REALTIME_RESERVED_EVENTS = new Set(["connect", "disconnect", "error", "ping", "pong", "join", "leave", "system"]);
    const REALTIME_PROTOCOL_UNSAFE_PATTERN = /[^!#$%&'*+\-.^_`|~0-9A-Za-z]/gu;
    const __sloppyRealtimeSchemaRuntime = createSloppySchemaRuntime();
    const __sloppyRealtimeSchema = __sloppyRealtimeSchemaRuntime.Schema;
    const __sloppyRealtimeIsSchema = __sloppyRealtimeSchemaRuntime.isSchema;
    const __sloppyRealtimeIsValidationError = __sloppyRealtimeSchemaRuntime.isValidationError;

    class SloppyRealtimeError extends Error {
        constructor(code, message, options = undefined) {
            super(message);
            this.name = "SloppyRealtimeError";
            this.code = String(code);
            this.event = options?.event;
            this.closeCode = options?.closeCode;
            this.issues = Array.isArray(options?.issues) ? options.issues.slice(0, 32) : [];
            this.__sloppyRealtimeError = true;
        }
    }

    function __sloppyRealtimeDeepFreeze(value) {
        if (value === null || typeof value !== "object" || Object.isFrozen(value)) {
            return value;
        }
        for (const child of Object.values(value)) {
            __sloppyRealtimeDeepFreeze(child);
        }
        return Object.freeze(value);
    }

    function __sloppyRealtimeSnapshot(value) {
        if (value === undefined) {
            return undefined;
        }
        return __sloppyRealtimeDeepFreeze(JSON.parse(JSON.stringify(value)));
    }

    function __sloppyRealtimeIdentifier(value, subject) {
        if (typeof value !== "string" || !/^[A-Za-z_][A-Za-z0-9_.:-]{0,127}$/u.test(value)) {
            throw new TypeError(`Sloppy Realtime ${subject} must be a stable identifier.`);
        }
    }

    function __sloppyRealtimeEventName(value) {
        __sloppyRealtimeIdentifier(value, "event name");
        if (REALTIME_RESERVED_EVENTS.has(value)) {
            throw new TypeError(`Sloppy Realtime event name '${value}' is reserved.`);
        }
    }

    function __sloppyRealtimeDefaultProtocol(name) {
        return `sloppy.realtime.${name.replace(REALTIME_PROTOCOL_UNSAFE_PATTERN, "-")}.v1`;
    }

    function __sloppyRealtimeAuthList(value) {
        if (value === undefined) {
            return Object.freeze([]);
        }
        return Object.freeze((Array.isArray(value) ? value : [value]).map(String));
    }

    function __sloppyRealtimeAuth(auth = undefined) {
        if (auth === undefined || auth === null) {
            return undefined;
        }
        if (!isPlainObject(auth)) {
            throw new TypeError("Sloppy Realtime event auth must be a plain object.");
        }
        return Object.freeze({
            required: auth.required === true,
            scopes: __sloppyRealtimeAuthList(auth.scopes),
            roles: __sloppyRealtimeAuthList(auth.roles),
            policy: auth.policy === undefined ? undefined : String(auth.policy),
        });
    }

    function __sloppyRealtimeEvent(schemaValue, auth = undefined) {
        if (!__sloppyRealtimeIsSchema(schemaValue)) {
            throw new TypeError("Sloppy Realtime event schema must be a Sloppy schema.");
        }
        const eventAuth = __sloppyRealtimeAuth(auth);
        const event = {
            [REALTIME_EVENT]: true,
            schema: schemaValue,
            metadata: Object.freeze({
                schema: schemaValue.metadata,
                ...(eventAuth === undefined ? {} : { auth: eventAuth }),
            }),
            requiresAuth() {
                return __sloppyRealtimeEvent(schemaValue, { ...(eventAuth ?? {}), required: true });
            },
            requiresScope(...scopes) {
                for (const scope of scopes) {
                    __sloppyRealtimeIdentifier(scope, "event authorization scope");
                }
                return __sloppyRealtimeEvent(schemaValue, {
                    ...(eventAuth ?? {}),
                    required: true,
                    scopes: [...new Set([...(eventAuth?.scopes ?? []), ...scopes])],
                });
            },
            requiresRole(...roles) {
                for (const role of roles) {
                    __sloppyRealtimeIdentifier(role, "event authorization role");
                }
                return __sloppyRealtimeEvent(schemaValue, {
                    ...(eventAuth ?? {}),
                    required: true,
                    roles: [...new Set([...(eventAuth?.roles ?? []), ...roles])],
                });
            },
            authorize(policy) {
                __sloppyRealtimeIdentifier(policy, "event authorization policy");
                return __sloppyRealtimeEvent(schemaValue, { ...(eventAuth ?? {}), required: true, policy });
            },
        };
        return Object.freeze(event);
    }

    function __sloppyRealtimeIsEvent(value) {
        return value !== null && typeof value === "object" && value[REALTIME_EVENT] === true;
    }

    function __sloppyRealtimeNormalizeEvent(value, subject) {
        if (__sloppyRealtimeIsEvent(value)) {
            return value;
        }
        if (__sloppyRealtimeIsSchema(value)) {
            return __sloppyRealtimeEvent(value);
        }
        throw new TypeError(`Sloppy Realtime ${subject} must be a Sloppy schema or Realtime.event(...).`);
    }

    function __sloppyRealtimeEvents(events, subject) {
        if (!isPlainObject(events)) {
            throw new TypeError(`Sloppy Realtime ${subject} events must be a plain object.`);
        }
        const entries = {};
        for (const [name, value] of Object.entries(events)) {
            __sloppyRealtimeEventName(name);
            entries[name] = __sloppyRealtimeNormalizeEvent(value, `${subject}.${name}`);
        }
        return Object.freeze(entries);
    }

    function __sloppyRealtimeValidateEnvelope(value, direction) {
        if (!isPlainObject(value) ||
            typeof value.type !== "string" ||
            value.type.length === 0 ||
            !Object.prototype.hasOwnProperty.call(value, "data") ||
            (value.id !== undefined && typeof value.id !== "string"))
        {
            throw new SloppyRealtimeError(
                "SLOPPY_E_REALTIME_MALFORMED_ENVELOPE",
                `Realtime ${direction} message envelope is invalid.`,
                { closeCode: 1003 },
            );
        }
    }

    function __sloppyRealtimeValidateEvent(events, eventName, data, direction) {
        const event = events[eventName];
        if (event === undefined) {
            throw new SloppyRealtimeError(
                "SLOPPY_E_REALTIME_UNKNOWN_EVENT",
                `Realtime ${direction} event is not registered.`,
                { event: eventName, closeCode: 1008 },
            );
        }
        try {
            return __sloppyRealtimeSchema.validate(data, event.schema);
        } catch (error) {
            throw new SloppyRealtimeError(
                "SLOPPY_E_REALTIME_VALIDATION_FAILED",
                "Realtime message validation failed.",
                { event: eventName, issues: __sloppyRealtimeIsValidationError(error) ? error.issues : [] },
            );
        }
    }

    function __sloppyRealtimeErrorEnvelope(error, event = undefined) {
        const code = error instanceof SloppyRealtimeError ? error.code : "SLOPPY_E_REALTIME_HANDLER_ERROR";
        return __sloppyRealtimeDeepFreeze({
            type: "error",
            error: {
                code,
                message: error instanceof SloppyRealtimeError ? error.message : "Realtime message handling failed.",
                ...(event === undefined ? {} : { event }),
                ...((error instanceof SloppyRealtimeError && error.issues.length !== 0) ? { issues: error.issues.slice(0, 32) } : {}),
            },
        });
    }

    function __sloppyRealtimeChannel(name, definition) {
        __sloppyRealtimeIdentifier(name, "channel name");
        if (!isPlainObject(definition)) {
            throw new TypeError("Sloppy Realtime channel definition must be a plain object.");
        }
        const client = __sloppyRealtimeEvents(definition.client ?? {}, "client");
        const server = __sloppyRealtimeEvents(definition.server ?? {}, "server");
        for (const eventName of Object.keys(client)) {
            if (Object.prototype.hasOwnProperty.call(server, eventName)) {
                throw new TypeError(`Sloppy Realtime event '${eventName}' cannot be both a client and server event.`);
            }
        }
        const metadata = __sloppyRealtimeDeepFreeze({
            name,
            protocol: definition.protocol ?? __sloppyRealtimeDefaultProtocol(name),
            client: Object.fromEntries(Object.entries(client).map(([eventName, event]) => [eventName, event.metadata])),
            server: Object.fromEntries(Object.entries(server).map(([eventName, event]) => [eventName, event.metadata])),
        });
        function parseEnvelope(value, direction) {
            let envelope = value;
            if (typeof value === "string") {
                try {
                    envelope = JSON.parse(value);
                } catch {
                    throw new SloppyRealtimeError(
                        "SLOPPY_E_REALTIME_MALFORMED_JSON",
                        `Sloppy Realtime ${direction} message must be valid JSON.`,
                    );
                }
            }
            __sloppyRealtimeValidateEnvelope(envelope, direction);
            return envelope;
        }
        const channel = {
            [REALTIME_CHANNEL]: true,
            name,
            client,
            server,
            metadata,
            parseClientMessage(value) {
                const envelope = parseEnvelope(value, "client");
                return Object.freeze({
                    ...envelope,
                    data: __sloppyRealtimeValidateEvent(client, envelope.type, envelope.data, "client"),
                });
            },
            parseServerMessage(value) {
                const envelope = parseEnvelope(value, "server");
                return Object.freeze({
                    ...envelope,
                    data: __sloppyRealtimeValidateEvent(server, envelope.type, envelope.data, "server"),
                });
            },
            serializeClientMessage(eventName, data, options = undefined) {
                __sloppyRealtimeEventName(eventName);
                return __sloppyRealtimeDeepFreeze({
                    ...(options?.id === undefined ? {} : { id: String(options.id) }),
                    type: eventName,
                    data: __sloppyRealtimeValidateEvent(client, eventName, data, "client"),
                });
            },
            serializeServerMessage(eventName, data, options = undefined) {
                __sloppyRealtimeEventName(eventName);
                return __sloppyRealtimeDeepFreeze({
                    ...(options?.id === undefined ? {} : { id: String(options.id) }),
                    type: eventName,
                    data: __sloppyRealtimeValidateEvent(server, eventName, data, "server"),
                });
            },
            errorEnvelope: __sloppyRealtimeErrorEnvelope,
        };
        return Object.freeze(channel);
    }

    function __sloppyRealtimeIsChannel(value) {
        return value !== null && typeof value === "object" && value[REALTIME_CHANNEL] === true;
    }

    function __sloppyRealtimeGroupName(value) {
        if (typeof value !== "string" || value.length === 0 || value.length > 256 || /[\u0000-\u001f\u007f]/u.test(value)) {
            throw new TypeError("Sloppy Realtime group names must be non-empty bounded strings without control characters.");
        }
        return value;
    }

    function __sloppyRealtimeMemoryBackplane() {
        const connections = new Map();
        const groups = new Map();
        const presence = new Map();
        let disposed = false;
        function ensureOpen() {
            if (disposed) {
                throw new SloppyRealtimeError("SLOPPY_E_REALTIME_BACKPLANE_ERROR", "Realtime backplane is disposed.");
            }
        }
        function ensureGroup(name) {
            __sloppyRealtimeGroupName(name);
            let group = groups.get(name);
            if (group === undefined) {
                group = new Set();
                groups.set(name, group);
            }
            return group;
        }
        function pruneGroup(name) {
            const group = groups.get(name);
            if (group !== undefined && group.size === 0) {
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
        return Object.freeze({
            kind: "memory",
            async connect(connection) {
                ensureOpen();
                removeConnectionState(connection.connectionId);
                connections.set(connection.connectionId, { ...connection, groups: new Set() });
                return Object.freeze({ ok: true });
            },
            async disconnect(connectionId) {
                if (disposed) {
                    return Object.freeze({ ok: true });
                }
                removeConnectionState(connectionId);
                return Object.freeze({ ok: true });
            },
            async join(connectionId, groupName) {
                ensureOpen();
                __sloppyRealtimeGroupName(groupName);
                const connection = connections.get(connectionId);
                if (connection === undefined) {
                    throw new SloppyRealtimeError("SLOPPY_E_REALTIME_CLOSED_CONNECTION", "Realtime connection is closed.");
                }
                ensureGroup(groupName).add(connectionId);
                connection.groups.add(groupName);
                return Object.freeze({ ok: true });
            },
            async leave(connectionId, groupName) {
                ensureOpen();
                __sloppyRealtimeGroupName(groupName);
                groups.get(groupName)?.delete(connectionId);
                pruneGroup(groupName);
                connections.get(connectionId)?.groups.delete(groupName);
                return Object.freeze({ ok: true });
            },
            async leaveAll(connectionId) {
                ensureOpen();
                const connection = connections.get(connectionId);
                if (connection === undefined) {
                    return Object.freeze({ count: 0 });
                }
                const count = connection.groups.size;
                for (const groupName of connection.groups) {
                    groups.get(groupName)?.delete(connectionId);
                    pruneGroup(groupName);
                }
                connection.groups.clear();
                return Object.freeze({ count });
            },
            async groups(connectionId) {
                ensureOpen();
                return Object.freeze([...(connections.get(connectionId)?.groups ?? [])]);
            },
            async groupSize(groupName) {
                ensureOpen();
                __sloppyRealtimeGroupName(groupName);
                return groups.get(groupName)?.size ?? 0;
            },
            async send(connectionId, envelope) {
                ensureOpen();
                const connection = connections.get(connectionId);
                if (connection === undefined) {
                    return Object.freeze({ count: 0 });
                }
                await connection.send(envelope);
                return Object.freeze({ count: 1 });
            },
            async broadcast(groupName, envelope, options = undefined) {
                ensureOpen();
                __sloppyRealtimeGroupName(groupName);
                const except = new Set(options?.except ?? []);
                if (options?.exceptSelf === true && typeof options.senderId === "string") {
                    except.add(options.senderId);
                }
                const ids = [...(groups.get(groupName) ?? [])].filter((id) => !except.has(id));
                let count = 0;
                for (const id of ids) {
                    const connection = connections.get(id);
                    if (connection !== undefined) {
                        await connection.send(envelope);
                        count += 1;
                    } else {
                        groups.get(groupName)?.delete(id);
                        pruneGroup(groupName);
                    }
                }
                return Object.freeze({ count });
            },
            async presenceSet(connectionId, record) {
                ensureOpen();
                const connection = connections.get(connectionId);
                if (connection === undefined) {
                    throw new SloppyRealtimeError("SLOPPY_E_REALTIME_CLOSED_CONNECTION", "Realtime connection is closed.");
                }
                const metadata = record?.metadata ?? {};
                const encoded = JSON.stringify(metadata);
                if (encoded === undefined || Text.utf8.encode(encoded).byteLength > 4096) {
                    throw new TypeError("Sloppy Realtime presence metadata must be bounded JSON.");
                }
                presence.set(connectionId, __sloppyRealtimeSnapshot({
                    connectionId,
                    userId: record?.userId ?? connection.userId,
                    groups: [...connection.groups],
                    connectedAt: record?.connectedAt ?? connection.connectedAt,
                    metadata: JSON.parse(encoded),
                }));
                return presence.get(connectionId);
            },
            async presenceGet(connectionId) {
                ensureOpen();
                return presence.get(connectionId);
            },
            async presenceInGroup(groupName) {
                ensureOpen();
                __sloppyRealtimeGroupName(groupName);
                return Object.freeze([...(groups.get(groupName) ?? [])].map((id) => presence.get(id)).filter(Boolean));
            },
            async dispose() {
                disposed = true;
                connections.clear();
                groups.clear();
                presence.clear();
            },
            health() {
                return Object.freeze({ status: disposed ? "unavailable" : "ok", kind: "memory" });
            },
        });
    }

    function __sloppyRealtimeHandlerAuth(options = undefined) {
        if (options === undefined) {
            return undefined;
        }
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy Realtime ctx.on options must be a plain object.");
        }
        return Object.freeze({
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

    function __sloppyRealtimeMergeAuth(eventAuth = undefined, handlerAuth = undefined) {
        if (eventAuth === undefined && handlerAuth === undefined) {
            return undefined;
        }
        return Object.freeze({
            required: eventAuth?.required === true || handlerAuth?.required === true,
            scopes: Object.freeze([...new Set([...(eventAuth?.scopes ?? []), ...(handlerAuth?.scopes ?? [])])]),
            roles: Object.freeze([...new Set([...(eventAuth?.roles ?? []), ...(handlerAuth?.roles ?? [])])]),
            policy: eventAuth?.policy ?? handlerAuth?.policy,
        });
    }

    async function __sloppyRealtimeAuthorize(ctx, auth, eventName, resource = undefined) {
        if (auth === undefined) {
            return;
        }
        const user = ctx.user;
        if (auth.required === true && user?.authenticated !== true) {
            throw new SloppyRealtimeError("SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT", "Realtime event requires an authenticated user.", { event: eventName, closeCode: 1008 });
        }
        const scopes = Array.isArray(user?.scopes) ? user.scopes : String(user?.scope ?? "").split(/\s+/u).filter(Boolean);
        for (const scope of auth.scopes) {
            if (!scopes.includes(scope)) {
                throw new SloppyRealtimeError("SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT", "Realtime event requires a missing scope.", { event: eventName, closeCode: 1008 });
            }
        }
        const roles = Array.isArray(user?.roles) ? user.roles : [];
        for (const role of auth.roles) {
            if (!roles.includes(role)) {
                throw new SloppyRealtimeError("SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT", "Realtime event requires a missing role.", { event: eventName, closeCode: 1008 });
            }
        }
        if (auth.policy !== undefined) {
            if (typeof ctx.authorize !== "function") {
                throw new SloppyRealtimeError("SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT", "Realtime event authorization policy is unavailable in this runtime.", { event: eventName, closeCode: 1008 });
            }
            if (await ctx.authorize(auth.policy, resource) !== true) {
                throw new SloppyRealtimeError("SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT", "Realtime event authorization policy denied the message.", { event: eventName, closeCode: 1008 });
            }
        }
    }

    function __sloppyRealtimeRoute(channel, handler, options = undefined) {
        if (!__sloppyRealtimeIsChannel(channel)) {
            throw new TypeError("Sloppy app.realtime channel must come from Realtime.channel(...).");
        }
        if (typeof handler !== "function") {
            throw new TypeError("Sloppy app.realtime handler must be a function.");
        }
        if (options !== undefined && !isPlainObject(options)) {
            throw new TypeError("Sloppy Realtime route options must be a plain object.");
        }
        const routeOptions = Object.freeze({
            presence: options?.presence === true,
            backplane: options?.backplane ?? __sloppyRealtimeMemoryBackplane(),
            unknownEventPolicy: options?.unknownEventPolicy ?? "error",
            validationFailurePolicy: options?.validationFailurePolicy ?? "error",
            handlerErrorPolicy: options?.handlerErrorPolicy ?? "close",
            websocket: {
                ...(options?.websocket ?? options ?? {}),
                protocols: options?.protocols ?? options?.websocket?.protocols ?? [channel.metadata.protocol],
            },
        });
        return __sloppyRealtimeWebSocket(async (ctx, socket) => {
            const eventHandlers = new Map();
            const connectionId = socket.id ?? `${channel.name}:${Date.now()}:${Math.random()}`;
            const routePattern = ctx.routePattern ?? ctx.request?.path ?? "";
            const routeGroup = `route:${channel.name}:${routePattern}`;
            let accepted = false;
            async function sendError(error, eventName = undefined) {
                await socket.sendJson(channel.errorEnvelope(error, eventName));
            }
            async function dispatchError(error, eventName = undefined) {
                const code = error instanceof SloppyRealtimeError ? error.code : "SLOPPY_E_REALTIME_HANDLER_ERROR";
                const policy = code === "SLOPPY_E_REALTIME_UNKNOWN_EVENT"
                    ? routeOptions.unknownEventPolicy
                    : code === "SLOPPY_E_REALTIME_VALIDATION_FAILED"
                        ? routeOptions.validationFailurePolicy
                        : code === "SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT"
                            ? "error"
                            : routeOptions.handlerErrorPolicy;
                if (policy === "error") {
                    await sendError(error, eventName);
                } else {
                    await socket.close(error?.closeCode ?? 1011, code);
                }
            }
            function groupHandle(groupName) {
                __sloppyRealtimeGroupName(groupName);
                return Object.freeze({
                    async sendTo(targetConnectionId, eventName, data, sendOptions = undefined) {
                        const envelope = channel.serializeServerMessage(eventName, data, sendOptions);
                        return routeOptions.backplane.send(targetConnectionId, envelope);
                    },
                    async broadcast(eventName, data, broadcastOptions = undefined) {
                        const envelope = channel.serializeServerMessage(eventName, data, broadcastOptions);
                        return routeOptions.backplane.broadcast(groupName, envelope, { ...(broadcastOptions ?? {}), senderId: connectionId });
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
                services: ctx.services,
                requireUser() {
                    return ctx.requireUser();
                },
                async accept() {
                    if (!accepted) {
                        await socket.accept();
                        accepted = true;
                        await routeOptions.backplane.connect({
                            connectionId,
                            userId: ctx.user?.sub,
                            routePattern,
                            channel: channel.name,
                            async send(envelope) {
                                await socket.sendJson(envelope);
                            },
                        });
                        await routeOptions.backplane.join(connectionId, routeGroup);
                    }
                },
                close(code = 1000, reason = "") {
                    return socket.close(code, reason);
                },
                on(eventName, optionsOrHandler, maybeHandler = undefined) {
                    __sloppyRealtimeEventName(eventName);
                    const eventHandler = typeof optionsOrHandler === "function" ? optionsOrHandler : maybeHandler;
                    if (typeof eventHandler !== "function") {
                        throw new TypeError("Sloppy Realtime ctx.on handler must be a function.");
                    }
                    if (eventHandlers.has(eventName)) {
                        throw new TypeError(`Sloppy Realtime client event '${eventName}' already has a handler.`);
                    }
                    const handlerPolicy = typeof optionsOrHandler === "function"
                        ? undefined
                        : __sloppyRealtimeHandlerAuth(optionsOrHandler);
                    eventHandlers.set(eventName, Object.freeze({
                        handler: eventHandler,
                        policy: handlerPolicy,
                    }));
                    return realtimeContext;
                },
                async send(eventName, data, sendOptions = undefined) {
                    const envelope = channel.serializeServerMessage(eventName, data, sendOptions);
                    await socket.sendJson(envelope);
                    return envelope;
                },
                async broadcast(eventName, data, broadcastOptions = undefined) {
                    const envelope = channel.serializeServerMessage(eventName, data, broadcastOptions);
                    return routeOptions.backplane.broadcast(routeGroup, envelope, { ...(broadcastOptions ?? {}), senderId: connectionId });
                },
                group: groupHandle,
                groups: Object.freeze({
                    join(groupName) {
                        return routeOptions.backplane.join(connectionId, __sloppyRealtimeGroupName(groupName));
                    },
                    leave(groupName) {
                        return routeOptions.backplane.leave(connectionId, __sloppyRealtimeGroupName(groupName));
                    },
                    async list() {
                        return (await routeOptions.backplane.groups(connectionId)).filter((groupName) => groupName !== routeGroup);
                    },
                }),
                presence: Object.freeze({
                    set(record) {
                        if (routeOptions.presence !== true) {
                            throw new SloppyRealtimeError("SLOPPY_E_REALTIME_PRESENCE_DISABLED", "Realtime presence is not enabled for this route.");
                        }
                        return routeOptions.backplane.presenceSet(connectionId, { ...(record ?? {}), userId: record?.userId ?? ctx.user?.sub });
                    },
                    get(targetConnectionId = connectionId) {
                        return routeOptions.backplane.presenceGet(targetConnectionId);
                    },
                    inGroup(groupName) {
                        return routeOptions.backplane.presenceInGroup(__sloppyRealtimeGroupName(groupName));
                    },
                }),
                connectionId,
            });
            try {
                try {
                    await handler(realtimeContext);
                } catch (error) {
                    if (!accepted) {
                        throw error;
                    }
                    await dispatchError(error, error?.event);
                    return undefined;
                }
                if (!accepted) {
                    return undefined;
                }
                for await (const message of socket.messages()) {
                    let envelope;
                    try {
                        envelope = channel.parseClientMessage(message.kind === "json" ? message.json() : message.text);
                        const registration = eventHandlers.get(envelope.type);
                        if (registration === undefined) {
                            throw new SloppyRealtimeError("SLOPPY_E_REALTIME_UNKNOWN_EVENT", "Realtime client event has no handler.", { event: envelope.type, closeCode: 1008 });
                        }
                        await __sloppyRealtimeAuthorize(ctx, __sloppyRealtimeMergeAuth(
                            channel.client[envelope.type].metadata.auth,
                            registration.policy,
                        ), envelope.type, {
                            event: envelope.type,
                            data: envelope.data,
                            id: envelope.id,
                            connectionId,
                            channel: channel.name,
                        });
                        await registration.handler(envelope.data, Object.freeze({ id: envelope.id, event: envelope.type }));
                    } catch (error) {
                        await dispatchError(error, envelope?.type ?? error?.event);
                        if (socket.closed) {
                            break;
                        }
                    }
                }
                return undefined;
            } finally {
                await routeOptions.backplane.disconnect(connectionId);
            }
        }, routeOptions.websocket);
    }

    const Realtime = Object.freeze({
        sse: __sloppyRealtimeSse,
        websocket: __sloppyRealtimeWebSocket,
        hub: __sloppyRealtimeHub,
        channel: __sloppyRealtimeChannel,
        event: __sloppyRealtimeEvent,
        isChannel: __sloppyRealtimeIsChannel,
        backplane: Object.freeze({
            memory: __sloppyRealtimeMemoryBackplane,
        }),
        __route: __sloppyRealtimeRoute,
        textBytes(value) {
            return Text.utf8.encode(String(value));
        },
    });

    function createSloppySchemaRuntime() {
        function issue(path, code, message) {
            return Object.freeze({
                path: Object.freeze([...path]),
                code,
                message,
            });
        }

        function success(value) {
            return Object.freeze({
                ok: true,
                value,
            });
        }

        function failure(issues) {
            return Object.freeze({
                ok: false,
                issues: Object.freeze(issues),
            });
        }

        class SloppyValidationError extends Error {
            constructor(issues) {
                super("Sloppy request validation failed.");
                this.name = "SloppyValidationError";
                this.issues = Object.freeze([...issues]);
                this.__sloppyValidationError = true;
            }
        }

        function validationProblem(issues) {
            return Object.freeze({
                type: "https://sloppy.dev/problems/validation",
                title: "Validation failed",
                status: 400,
                code: "SLOPPY_E_VALIDATION_FAILED",
                errors: Object.freeze(issues.map((current) => Object.freeze({
                    path: Object.freeze([...current.path]),
                    code: current.code,
                    message: current.message,
                }))),
            });
        }

        function isValidationError(error) {
            return error !== null && typeof error === "object" && error.__sloppyValidationError === true;
        }

        function throwValidationError(issues) {
            throw new SloppyValidationError(issues);
        }

        function isPlainObject(value) {
            if (value === null || typeof value !== "object") {
                return false;
            }

            const prototype = Object.getPrototypeOf(value);
            return prototype === Object.prototype || prototype === null;
        }

        function isSchema(value) {
            return value !== null &&
                typeof value === "object" &&
                typeof value.validate === "function" &&
                typeof value.__validateAtPath === "function";
        }

        const MODIFIERS = Symbol("SloppySchemaModifiers");

        function withModifiers(schema) {
            const modifiers = schema[MODIFIERS] ?? Object.freeze([]);
            const wrapped = {
                ...schema,
                optional() {
                    return createOptionalSchema(schema);
                },
                nullable() {
                    return createNullableSchema(schema);
                },
                default(value) {
                    return createDefaultSchema(schema, value);
                },
            };

            for (const [key, value] of Object.entries(schema)) {
                if (["validate", "__validateAtPath", "optional", "nullable", "default"].includes(key) ||
                    typeof value !== "function") {
                    continue;
                }
                wrapped[key] = (...args) => {
                    const next = value(...args);
                    return isSchema(next) ? applySchemaModifiers(next, modifiers) : next;
                };
            }

            Object.defineProperty(wrapped, MODIFIERS, {
                value: modifiers,
            });
            return Object.freeze(wrapped);
        }

        function applySchemaModifiers(schema, modifiers) {
            let current = schema;
            for (const modifier of modifiers) {
                if (modifier.kind === "optional") {
                    current = createOptionalSchema(current);
                } else if (modifier.kind === "nullable") {
                    current = createNullableSchema(current);
                } else if (modifier.kind === "default") {
                    current = createDefaultSchema(current, modifier.value);
                }
            }
            return current;
        }

        function createOptionalSchema(inner) {
            function validateAtPath(value, path) {
                if (value === undefined) {
                    return success(undefined);
                }

                return inner.__validateAtPath(value, path);
            }

            return withModifiers({
                ...inner,
                kind: inner.kind,
                metadata: Object.freeze({
                    ...inner.metadata,
                    optional: true,
                }),
                validate(value) {
                    return validateAtPath(value, []);
                },
                __validateAtPath: validateAtPath,
                [MODIFIERS]: Object.freeze([...(inner[MODIFIERS] ?? []), Object.freeze({ kind: "optional" })]),
            });
        }

        function createNullableSchema(inner) {
            function validateAtPath(value, path) {
                if (value === null) {
                    return success(null);
                }

                return inner.__validateAtPath(value, path);
            }

            return withModifiers({
                ...inner,
                kind: inner.kind,
                metadata: Object.freeze({
                    ...inner.metadata,
                    nullable: true,
                }),
                validate(value) {
                    return validateAtPath(value, []);
                },
                __validateAtPath: validateAtPath,
                [MODIFIERS]: Object.freeze([...(inner[MODIFIERS] ?? []), Object.freeze({ kind: "nullable" })]),
            });
        }

        function createDefaultSchema(inner, defaultValue) {
            const defaultResult = inner.__validateAtPath(defaultValue, []);
            if (!defaultResult.ok) {
                throw new TypeError("Sloppy schema default value must satisfy the wrapped schema.");
            }
            const protectedDefault = protectDefaultValue(defaultResult.value);

            function validateAtPath(value, path) {
                if (value === undefined) {
                    return success(protectedDefault);
                }

                return inner.__validateAtPath(value, path);
            }

            return withModifiers({
                ...inner,
                kind: inner.kind,
                metadata: Object.freeze({
                    ...inner.metadata,
                    default: protectedDefault,
                }),
                validate(value) {
                    return validateAtPath(value, []);
                },
                __validateAtPath: validateAtPath,
                [MODIFIERS]: Object.freeze([...(inner[MODIFIERS] ?? []), Object.freeze({ kind: "default", value: protectedDefault })]),
            });
        }

        function protectDefaultValue(value) {
            if (value === null || typeof value !== "object") {
                return value;
            }
            if (Array.isArray(value)) {
                return Object.freeze(value.map((item) => protectDefaultValue(item)));
            }
            if (!isPlainObject(value)) {
                throw new TypeError("Sloppy schema default object values must be plain JSON-compatible objects.");
            }
            return Object.freeze(Object.fromEntries(
                Object.entries(value).map(([key, item]) => [key, protectDefaultValue(item)]),
            ));
        }

        function normalizeStringRuleValue(value, name) {
            if (!Number.isInteger(value) || value < 0) {
                throw new TypeError(`Sloppy schema.string().${name} length must be a non-negative integer.`);
            }
            return value;
        }

        function createStringSchema(rules = []) {
            function validateAtPath(value, path) {
                const issues = [];

                if (typeof value !== "string") {
                    issues.push(issue(path, "type", "Expected a string."));
                    return failure(issues);
                }

                for (const rule of rules) {
                    if ((rule.kind === "min" || rule.kind === "minLength") && value.length < rule.value) {
                        issues.push(issue(path, "string.min", `Expected at least ${rule.value} character(s).`));
                    }

                    if ((rule.kind === "max" || rule.kind === "maxLength") && value.length > rule.value) {
                        issues.push(issue(path, "string.max", `Expected at most ${rule.value} character(s).`));
                    }

                    if (rule.kind === "email" && !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value)) {
                        issues.push(issue(path, "string.email", "Expected an email address."));
                    }

                    if (rule.kind === "uuid" &&
                        !/^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu.test(value)) {
                        issues.push(issue(path, "string.uuid", "Expected a UUID."));
                    }

                    if (rule.kind === "pattern" && !rule.value.test(value)) {
                        issues.push(issue(path, "string.pattern", rule.message ?? "Expected string to match pattern."));
                    }
                }

                return issues.length === 0 ? success(value) : failure(issues);
            }

            const schema = {
                kind: "string",
                metadata: Object.freeze({
                    kind: "string",
                    rules: Object.freeze(rules.map((rule) => Object.freeze({ ...rule }))),
                }),
                min(length) {
                    return createStringSchema([...rules, Object.freeze({ kind: "min", value: normalizeStringRuleValue(length, "min") })]);
                },
                max(length) {
                    return createStringSchema([...rules, Object.freeze({ kind: "max", value: normalizeStringRuleValue(length, "max") })]);
                },
                minLength(length) {
                    return createStringSchema([...rules, Object.freeze({ kind: "minLength", value: normalizeStringRuleValue(length, "minLength") })]);
                },
                maxLength(length) {
                    return createStringSchema([...rules, Object.freeze({ kind: "maxLength", value: normalizeStringRuleValue(length, "maxLength") })]);
                },
                email() {
                    return createStringSchema([...rules, Object.freeze({ kind: "email" })]);
                },
                uuid() {
                    return createStringSchema([...rules, Object.freeze({ kind: "uuid" })]);
                },
                pattern(pattern, message) {
                    if (!(pattern instanceof RegExp)) {
                        throw new TypeError("Sloppy schema.string().pattern expects a RegExp.");
                    }
                    if (message !== undefined && typeof message !== "string") {
                        throw new TypeError("Sloppy schema.string().pattern message must be a string.");
                    }
                    const normalizedPattern = new RegExp(pattern.source, pattern.flags.replace(/[gy]/gu, ""));
                    return createStringSchema([...rules, Object.freeze({ kind: "pattern", value: normalizedPattern, message })]);
                },
                validate(value) {
                    return validateAtPath(value, []);
                },
                __validateAtPath: validateAtPath,
            };

            return withModifiers(schema);
        }

        function createNumberSchema(kind, integer, rules = []) {
            function validateAtPath(value, path) {
                if (typeof value !== "number" || !Number.isFinite(value) || (integer && !Number.isInteger(value))) {
                    return failure([issue(path, "type", integer ? "Expected an integer." : "Expected a finite number.")]);
                }

                const issues = [];
                for (const rule of rules) {
                    if (rule.kind === "min" && value < rule.value) {
                        issues.push(issue(path, "number.min", `Expected a value greater than or equal to ${rule.value}.`));
                    }
                    if (rule.kind === "max" && value > rule.value) {
                        issues.push(issue(path, "number.max", `Expected a value less than or equal to ${rule.value}.`));
                    }
                }

                return issues.length === 0 ? success(value) : failure(issues);
            }

            return withModifiers({
                kind,
                metadata: Object.freeze({
                    kind,
                    rules: Object.freeze(rules.map((rule) => Object.freeze({ ...rule }))),
                }),
                min(value) {
                    if (typeof value !== "number" || !Number.isFinite(value)) {
                        throw new TypeError(`Sloppy schema.${kind}().min value must be a finite number.`);
                    }
                    return createNumberSchema(kind, integer, [...rules, Object.freeze({ kind: "min", value })]);
                },
                max(value) {
                    if (typeof value !== "number" || !Number.isFinite(value)) {
                        throw new TypeError(`Sloppy schema.${kind}().max value must be a finite number.`);
                    }
                    return createNumberSchema(kind, integer, [...rules, Object.freeze({ kind: "max", value })]);
                },
                validate(value) {
                    return validateAtPath(value, []);
                },
                __validateAtPath: validateAtPath,
            });
        }

        function createPrimitiveSchema(kind, predicate, expected) {
            function validateAtPath(value, path) {
                if (!predicate(value)) {
                    return failure([issue(path, "type", `Expected ${expected}.`)]);
                }

                return success(value);
            }

            return withModifiers({
                kind,
                metadata: Object.freeze({ kind }),
                validate(value) {
                    return validateAtPath(value, []);
                },
                __validateAtPath: validateAtPath,
            });
        }

        function createArraySchema(itemSchema) {
            if (!isSchema(itemSchema)) {
                throw new TypeError("Sloppy schema.array item must be a schema.");
            }

            function validateAtPath(value, path) {
                if (!Array.isArray(value)) {
                    return failure([issue(path, "type", "Expected an array.")]);
                }

                const issues = [];
                const output = [];
                for (let index = 0; index < value.length; index += 1) {
                    const itemResult = itemSchema.__validateAtPath(value[index], [...path, index]);
                    if (!itemResult.ok) {
                        issues.push(...itemResult.issues);
                    } else {
                        output.push(itemResult.value);
                    }
                }

                return issues.length === 0 ? success(output) : failure(issues);
            }

            return withModifiers({
                kind: "array",
                metadata: Object.freeze({
                    kind: "array",
                    item: itemSchema.metadata,
                }),
                item: itemSchema,
                validate(value) {
                    return validateAtPath(value, []);
                },
                __validateAtPath: validateAtPath,
            });
        }

        function literal(value) {
            if (!isJsonLiteral(value)) {
                throw new TypeError("Sloppy schema.literal expects a string, finite number, boolean, or null.");
            }

            function validateAtPath(input, path) {
                return Object.is(input, value)
                    ? success(input)
                    : failure([issue(path, "literal", "Expected literal value.")]);
            }

            return withModifiers({
                kind: "literal",
                metadata: Object.freeze({ kind: "literal", value }),
                validate(input) {
                    return validateAtPath(input, []);
                },
                __validateAtPath: validateAtPath,
            });
        }

        function oneOf(values) {
            if (!Array.isArray(values) || values.length === 0 || !values.every(isJsonLiteral)) {
                throw new TypeError("Sloppy schema.enum expects a non-empty array of string, finite number, boolean, or null values.");
            }
            const variants = Object.freeze([...values]);

            function validateAtPath(input, path) {
                return variants.some((value) => Object.is(value, input))
                    ? success(input)
                    : failure([issue(path, "enum", "Expected one of the allowed values.")]);
            }

            return withModifiers({
                kind: "enum",
                metadata: Object.freeze({ kind: "enum", values: variants }),
                validate(input) {
                    return validateAtPath(input, []);
                },
                __validateAtPath: validateAtPath,
            });
        }

        function isJsonLiteral(value) {
            return value === null ||
                typeof value === "string" ||
                typeof value === "boolean" ||
                (typeof value === "number" && Number.isFinite(value));
        }

        function validateShape(shape) {
            if (!isPlainObject(shape)) {
                throw new TypeError("Sloppy schema.object shape must be a plain object.");
            }

            for (const [key, value] of Object.entries(shape)) {
                if (typeof key !== "string" || key.length === 0) {
                    throw new TypeError("Sloppy schema.object keys must be non-empty strings.");
                }

                if (!isSchema(value)) {
                    throw new TypeError(`Sloppy schema.object field '${key}' must be a schema.`);
                }
            }
        }

        function object(shape) {
            validateShape(shape);

            const fields = Object.freeze({ ...shape });
            const metadata = Object.freeze({
                kind: "object",
                shape: Object.freeze(Object.fromEntries(
                    Object.entries(fields).map(([key, value]) => [key, value.metadata]),
                )),
            });

            function validateAtPath(value, path) {
                if (value === null || typeof value !== "object" || Array.isArray(value)) {
                    return failure([issue(path, "type", "Expected an object.")]);
                }

                const issues = [];
                const output = {};

                for (const [key, fieldSchema] of Object.entries(fields)) {
                    const fieldResult = fieldSchema.__validateAtPath(value[key], [...path, key]);

                    if (!fieldResult.ok) {
                        issues.push(...fieldResult.issues);
                    } else if (fieldResult.value !== undefined || Object.prototype.hasOwnProperty.call(value, key)) {
                        output[key] = fieldResult.value;
                    }
                }

                return issues.length === 0 ? success(output) : failure(issues);
            }

            return withModifiers({
                kind: "object",
                metadata,
                shape: fields,
                validate(value) {
                    return validateAtPath(value, []);
                },
                __validateAtPath: validateAtPath,
            });
        }

        function validate(value, schemaValue) {
            if (!isSchema(schemaValue)) {
                throw new TypeError("Sloppy Schema.validate expects a schema.");
            }
            const result = schemaValue.validate(value);
            if (!result.ok) {
                throwValidationError(result.issues);
            }
            return result.value;
        }

        const schemaApi = Object.freeze({
            string() {
                return createStringSchema();
            },
            number() {
                return createNumberSchema("number", false);
            },
            int() {
                return createNumberSchema("int", true);
            },
            integer() {
                return createNumberSchema("int", true);
            },
            boolean() {
                return createPrimitiveSchema("boolean", (value) => typeof value === "boolean", "a boolean");
            },
            bool() {
                return createPrimitiveSchema("boolean", (value) => typeof value === "boolean", "a boolean");
            },
            array(itemSchema) {
                return createArraySchema(itemSchema);
            },
            enum(values) {
                return oneOf(values);
            },
            literal(value) {
                return literal(value);
            },
            object,
            validate,
            isSchema,
            validationProblem,
            isValidationError,
        });

        return Object.freeze({ schema: schemaApi, Schema: schemaApi, SloppyValidationError, isSchema, isValidationError, validationProblem });
    }

    function createSloppyOrmRuntime(dataSql, Migrations) {
        const __schemaRuntime = createSloppySchemaRuntime();
        const Schema = __schemaRuntime.Schema;
        const isSchema = __schemaRuntime.isSchema;
        const IDENTIFIER_PATTERN = /^[A-Za-z_][A-Za-z0-9_]*$/u;
        const ORM_TABLE = Symbol("SloppyOrmTable");
        const ORM_COLUMN = Symbol("SloppyOrmColumn");
        const ORM_EXPR = Symbol("SloppyOrmExpression");
        const ORM_RAW = Symbol("SloppyOrmRawSql");
        const ORM_RELATIONS = Symbol("SloppyOrmRelations");

        const COLUMN_TYPES = Object.freeze({
            text: { schema: () => Schema.string() },
            int: { schema: () => Schema.int() },
            bigint: { schema: () => Schema.string() },
            number: { schema: () => Schema.number() },
            decimal: { schema: () => Schema.string() },
            bool: { schema: () => Schema.boolean() },
            uuid: { schema: () => Schema.string().uuid() },
            instant: { schema: () => Schema.string() },
            date: { schema: () => Schema.string() },
            json: { schema: () => createJsonValueSchema() },
            blob: { schema: () => Schema.array(Schema.int()) },
            enum: { schema: (column) => Schema.enum(column.enumValues) },
        });

        const DIALECTS = Object.freeze({
            sqlite: Object.freeze({
                provider: "sqlite",
                placeholderStyle: "question",
                quote: quoteIdentifier,
                placeholder: () => "?",
                limitOffset(limit, offset) {
                    const parts = [];
                    if (limit !== null) {
                        parts.push(`limit ${limit}`);
                    }
                    if (offset !== null) {
                        parts.push(`offset ${offset}`);
                    }
                    return parts.join(" ");
                },
                returning(columns) {
                    return columns.length === 0 ? "" : ` returning ${columns.map((col) => quoteIdentifier(col.name)).join(", ")}`;
                },
                defaultNow: "CURRENT_TIMESTAMP",
                types: Object.freeze({
                    text: "text",
                    int: "integer",
                    bigint: "integer",
                    number: "real",
                    decimal: "text",
                    bool: "integer",
                    uuid: "text",
                    instant: "text",
                    date: "text",
                    json: "text",
                    blob: "blob",
                    enum: "text",
                }),
            }),
            postgres: Object.freeze({
                provider: "postgres",
                placeholderStyle: "postgres",
                quote: quoteIdentifier,
                placeholder: (index) => `$${index}`,
                limitOffset(limit, offset) {
                    const parts = [];
                    if (limit !== null) {
                        parts.push(`limit ${limit}`);
                    }
                    if (offset !== null) {
                        parts.push(`offset ${offset}`);
                    }
                    return parts.join(" ");
                },
                returning(columns) {
                    return columns.length === 0 ? "" : ` returning ${columns.map((col) => quoteIdentifier(col.name)).join(", ")}`;
                },
                defaultNow: "CURRENT_TIMESTAMP",
                types: Object.freeze({
                    text: "text",
                    int: "integer",
                    bigint: "bigint",
                    number: "double precision",
                    decimal: "numeric",
                    bool: "boolean",
                    uuid: "uuid",
                    instant: "timestamptz",
                    date: "date",
                    json: "jsonb",
                    blob: "bytea",
                    enum: "text",
                }),
            }),
            sqlserver: Object.freeze({
                provider: "sqlserver",
                placeholderStyle: "question",
                quote(name) {
                    assertIdentifier(name, "SQL Server identifier");
                    return `[${name.replaceAll("]", "]]")}]`;
                },
                placeholder: () => "?",
                limitOffset(limit, offset) {
                    if (limit === null && offset === null) {
                        return "";
                    }
                    return `offset ${offset ?? 0} rows${limit === null ? "" : ` fetch next ${limit} rows only`}`;
                },
                returning(columns) {
                    return columns.length === 0 ? "" : ` output ${columns.map((col) => `inserted.${this.quote(col.name)}`).join(", ")}`;
                },
                defaultNow: "SYSUTCDATETIME()",
                types: Object.freeze({
                    text: "nvarchar(max)",
                    int: "int",
                    bigint: "bigint",
                    number: "float",
                    decimal: "decimal(38, 18)",
                    bool: "bit",
                    uuid: "uniqueidentifier",
                    instant: "datetimeoffset",
                    date: "date",
                    json: "nvarchar(max)",
                    blob: "varbinary(max)",
                    enum: "nvarchar(255)",
                }),
            }),
        });

        const ORM_ERROR_HINTS = Object.freeze({
            SLOPPY_ORM_CONCURRENCY_CONFLICT: "Reload the row, compare the concurrency token, and retry the update or delete with the latest token.",
            SLOPPY_ORM_CURSOR_INCLUDE_UNSUPPORTED: "Run the cursor query without include(), or materialize the query with toList() before loading relations.",
            SLOPPY_ORM_DESTRUCTIVE_MIGRATION: "Review the destructive changes explicitly and pass allowDestructive: true only for an intentional migration draft.",
            SLOPPY_ORM_DUPLICATE_COLUMN: "Declare each table column once inside table(\"name\", { ... }).",
            SLOPPY_ORM_EMPTY_INSERT: "Pass at least one row object to insert() or insertMany().",
            SLOPPY_ORM_FOREIGN_KEY_VIOLATION: "Insert or update the referenced parent row first, or verify references(() => Parent.id) points at the intended table.",
            SLOPPY_ORM_GENERATED_PATCH: "Do not patch generated columns; let the database or provider produce the value.",
            SLOPPY_ORM_INVALID_CONCURRENCY_TOKEN: "Use exactly one int or bigint column marked concurrencyToken() for optimistic concurrency.",
            SLOPPY_ORM_INVALID_DEFAULT: "Use a default value that matches the column type, or use defaultNow() only on instant/date columns.",
            SLOPPY_ORM_INVALID_ENUM: "Declare enums with a non-empty array of string values, for example column.enum([\"active\", \"archived\"]).",
            SLOPPY_ORM_INVALID_EXPRESSION: "Build predicates from ORM column expressions, orm.and(), orm.or(), orm.not(), or orm.sql fragments.",
            SLOPPY_ORM_INVALID_IDENTIFIER: "Use simple SQL identifiers with letters, digits, and underscores, starting with a letter or underscore.",
            SLOPPY_ORM_INVALID_INCLUDE: "Return a relation from include(), for example include(u => u.team) or include(t => t.users.take(100)).",
            SLOPPY_ORM_INVALID_INCLUDE_STRATEGY: "Use include strategy \"join\" or \"split\".",
            SLOPPY_ORM_INVALID_LIST_EXPRESSION: "Pass a non-empty array to in() or notIn().",
            SLOPPY_ORM_INVALID_MIGRATION_SNAPSHOT: "Pass a snapshot created by orm.migrations.snapshot(...).",
            SLOPPY_ORM_INVALID_ORDER: "Return column order expressions such as u.createdAt.desc() from orderBy().",
            SLOPPY_ORM_INVALID_PATCH_OPERATION: "Use increment()/decrement() on numeric columns and setNow() on instant/date columns.",
            SLOPPY_ORM_INVALID_PROJECTION: "Return a non-empty object of columns or expressions from select().",
            SLOPPY_ORM_INVALID_REFERENCE: "Use references(() => OtherTable.id) after both tables are in scope.",
            SLOPPY_ORM_INVALID_RELATION: "Use relation(Table, ({ one, many }) => ({ name: one(Other, { local: Table.otherId, foreign: Other.id }) })).",
            SLOPPY_ORM_INVALID_SOFT_DELETE: "Use one nullable instant/date column marked softDelete().",
            SLOPPY_ORM_INVALID_TABLE: "Use table(\"users\", { id: column.uuid().primaryKey(), ... }) with at least one column.",
            SLOPPY_ORM_MULTIPLE_CONCURRENCY_TOKENS: "Keep a single concurrencyToken() column per table.",
            SLOPPY_ORM_MULTIPLE_SOFT_DELETE_COLUMNS: "Keep a single softDelete() column per table.",
            SLOPPY_ORM_NOT_NULL_PATCH: "Patch nullable columns with null, or omit non-nullable fields you do not want to change.",
            SLOPPY_ORM_NOT_NULL_VIOLATION: "Provide values for required columns or declare a default/defaultNow() in the model and database migration.",
            SLOPPY_ORM_PRIMARY_KEY_PATCH: "Primary keys are immutable; update a different column or insert a new row.",
            SLOPPY_ORM_PRIMARY_KEY_REQUIRED: "Define exactly one primaryKey() column before using by-id helpers.",
            SLOPPY_ORM_PRIVATE_COLUMN: "Use public columns in publicSchema(), public(), pick(), and projections; private columns stay server-side.",
            SLOPPY_ORM_PRIVATE_PATCH: "Do not patch private columns through public patch paths; handle sensitive writes explicitly.",
            SLOPPY_ORM_PROVIDER_ERROR: "Check provider availability, connection configuration, generated SQL, and the original provider message in details.cause.",
            SLOPPY_ORM_PROVIDER_SQL_MISMATCH: "Run provider-specific raw SQL only against the matching provider, or use provider-neutral orm.sql fragments.",
            SLOPPY_ORM_RAW_SQL_PLACEHOLDER_MISMATCH: "Keep raw SQL placeholders and interpolated parameter count aligned.",
            SLOPPY_ORM_SCHEMA_PICK_UNKNOWN_FIELD: "Pick only fields that exist on the table schema.",
            SLOPPY_ORM_SEQUENCE_EMPTY: "Use firstOrDefault() or singleOrDefault() when an empty result is valid.",
            SLOPPY_ORM_SEQUENCE_MULTIPLE: "Add a unique predicate, use first(), or call toList() when multiple rows are valid.",
            SLOPPY_ORM_SOFT_DELETE_UNAVAILABLE: "Mark a nullable instant/date column with softDelete(), or call deleteById() for hard deletes.",
            SLOPPY_ORM_TRANSACTION_UNAVAILABLE: "Use a database provider that implements transaction(callback).",
            SLOPPY_ORM_UNDEFINED_PATCH_VALUE: "Omit unchanged fields from patches; use null only for nullable columns.",
            SLOPPY_ORM_UNIQUE_VIOLATION: "Use a unique value, or query for the existing row before inserting/updating.",
            SLOPPY_ORM_UNKNOWN_COLUMN: "Check the table declaration and use one of its declared column names.",
            SLOPPY_ORM_UNSUPPORTED_COLUMN_TYPE: "Use a supported ORM column type such as text, int, bool, uuid, instant, json, blob, or enum([...]).",
            SLOPPY_ORM_UNSUPPORTED_PROVIDER: "Use provider \"sqlite\", \"postgres\", or \"sqlserver\".",
            SLOPPY_ORM_VALIDATION_FAILED: "Read details.issues and correct the row or patch before calling the provider.",
        });

        function ormErrorHint(code) {
            return ORM_ERROR_HINTS[code] ?? "Check the ORM table, query, provider, or migration input against docs/api/orm.md.";
        }

        class SloppyOrmError extends Error {
            constructor(code, message, details = undefined, hint = undefined) {
                super(message);
                this.name = "SloppyOrmError";
                this.code = code;
                this.details = details === undefined ? undefined : Object.freeze({ ...details });
                this.hint = hint ?? ormErrorHint(code);
            }
        }

        class SloppyOrmConcurrencyError extends SloppyOrmError {
            constructor(message, details = undefined, hint = undefined) {
                super("SLOPPY_ORM_CONCURRENCY_CONFLICT", message, details, hint);
                this.name = "SloppyOrmConcurrencyError";
            }
        }

        function ormError(code, message, details = undefined, hint = undefined) {
            return new SloppyOrmError(code, message, details, hint);
        }

        function classifyProviderError(error) {
            if (error instanceof SloppyOrmError) {
                return null;
            }
            const code = String(error?.code ?? error?.sqlState ?? error?.sqlstate ?? "");
            const number = error?.number ?? error?.errno;
            const message = String(error?.message ?? error ?? "");
            const lower = message.toLowerCase();
            if (
                code === "23505"
                || code === "SQLITE_CONSTRAINT_UNIQUE"
                || number === 2601
                || number === 2627
                || lower.includes("unique constraint failed")
                || lower.includes("duplicate key")
                || lower.includes("unique index")
            ) {
                return "SLOPPY_ORM_UNIQUE_VIOLATION";
            }
            if (
                code === "23503"
                || code === "SQLITE_CONSTRAINT_FOREIGNKEY"
                || number === 547
                || lower.includes("foreign key constraint failed")
                || lower.includes("foreign key")
                || lower.includes("reference constraint")
            ) {
                return "SLOPPY_ORM_FOREIGN_KEY_VIOLATION";
            }
            if (
                code === "23502"
                || code === "SQLITE_CONSTRAINT_NOTNULL"
                || number === 515
                || lower.includes("not null constraint failed")
                || lower.includes("cannot insert the value null")
                || lower.includes("null value in column")
            ) {
                return "SLOPPY_ORM_NOT_NULL_VIOLATION";
            }
            return null;
        }

        function wrapProviderError(error, operation, tableObject = undefined) {
            if (error instanceof SloppyOrmError) {
                return error;
            }
            const code = classifyProviderError(error) ?? "SLOPPY_ORM_PROVIDER_ERROR";
            const details = {
                operation,
                cause: String(error?.message ?? error),
            };
            if (tableObject !== undefined) {
                details.table = tableName(tableObject);
            }
            if (error?.code !== undefined) {
                details.providerCode = String(error.code);
            }
            if (error?.sqlState !== undefined || error?.sqlstate !== undefined) {
                details.sqlState = String(error.sqlState ?? error.sqlstate);
            }
            if (error?.number !== undefined || error?.errno !== undefined) {
                details.providerNumber = Number(error.number ?? error.errno);
            }
            return ormError(code, `Sloppy ORM ${operation} failed with a provider error.`, details);
        }

        async function withProviderErrors(operation, tableObject, callback) {
            try {
                return await callback();
            } catch (error) {
                throw wrapProviderError(error, operation, tableObject);
            }
        }

        function isPlainObject(value) {
            if (value === null || typeof value !== "object" || Array.isArray(value)) {
                return false;
            }
            const prototype = Object.getPrototypeOf(value);
            return prototype === Object.prototype || prototype === null;
        }

        function assertIdentifier(name, subject) {
            if (typeof name !== "string" || !IDENTIFIER_PATTERN.test(name)) {
                throw ormError("SLOPPY_ORM_INVALID_IDENTIFIER", `Sloppy ORM ${subject} must be a safe SQL identifier.`, { name });
            }
        }

        function quoteIdentifier(name) {
            assertIdentifier(name, "identifier");
            return `"${name.replaceAll("\"", "\"\"")}"`;
        }

        function freezeDeep(value) {
            if (value === null || typeof value !== "object") {
                return value;
            }
            if (Object.isFrozen(value)) {
                return value;
            }
            if (Array.isArray(value)) {
                for (const item of value) {
                    freezeDeep(item);
                }
                return Object.freeze(value);
            }
            for (const item of Object.values(value)) {
                freezeDeep(item);
            }
            return Object.freeze(value);
        }

        function schemaIssue(path, code, message) {
            return Object.freeze({ path: Object.freeze([...path]), code, message });
        }

        function schemaSuccess(value) {
            return Object.freeze({ ok: true, value });
        }

        function schemaFailure(path, message) {
            return Object.freeze({
                ok: false,
                issues: Object.freeze([schemaIssue(path, "type", message)]),
            });
        }

        function createJsonValueSchema() {
            function validateJsonValue(value, path) {
                if (value === undefined) {
                    return schemaFailure(path, "Expected a JSON value.");
                }
                if (typeof value === "function" || typeof value === "symbol" || typeof value === "bigint") {
                    return schemaFailure(path, "Expected a JSON value.");
                }
                try {
                    JSON.stringify(value);
                } catch {
                    return schemaFailure(path, "Expected a JSON-serializable value.");
                }
                return schemaSuccess(value);
            }
            const current = {
                kind: "json",
                metadata: Object.freeze({ kind: "json" }),
                validate(value) {
                    return validateJsonValue(value, []);
                },
                __validateAtPath: validateJsonValue,
                optional() {
                    return createOptionalSchema(current);
                },
                nullable() {
                    return createNullableSchema(current);
                },
            };
            return Object.freeze(current);
        }

        function createOptionalSchema(inner) {
            const current = {
                ...inner,
                metadata: Object.freeze({ ...inner.metadata, optional: true }),
                validate(value) {
                    return value === undefined ? schemaSuccess(undefined) : inner.validate(value);
                },
                __validateAtPath(value, path) {
                    return value === undefined ? schemaSuccess(undefined) : inner.__validateAtPath(value, path);
                },
                optional() {
                    return current;
                },
                nullable() {
                    return createNullableSchema(current);
                },
            };
            return Object.freeze(current);
        }

        function createNullableSchema(inner) {
            const current = {
                ...inner,
                metadata: Object.freeze({ ...inner.metadata, nullable: true }),
                validate(value) {
                    return value === null ? schemaSuccess(null) : inner.validate(value);
                },
                __validateAtPath(value, path) {
                    return value === null ? schemaSuccess(null) : inner.__validateAtPath(value, path);
                },
                optional() {
                    return createOptionalSchema(current);
                },
                nullable() {
                    return current;
                },
            };
            return Object.freeze(current);
        }

        function immutableRow(row) {
            if (!isPlainObject(row)) {
                return row;
            }
            return freezeDeep({ ...row });
        }

        function immutableRows(rows) {
            return Object.freeze(rows.map((row) => immutableRow(row)));
        }

        function assertTable(value, subject = "table") {
            if (value === null || typeof value !== "object" || value[ORM_TABLE] !== true) {
                throw new TypeError(`Sloppy ORM ${subject} must be a table.`);
            }
        }

        function tableName(tableObject) {
            return tableObject.metadata?.name ?? tableObject.name;
        }

        function assertColumn(value, subject = "column") {
            if (value === null || typeof value !== "object" || value[ORM_COLUMN] !== true) {
                throw new TypeError(`Sloppy ORM ${subject} must be a column.`);
            }
        }

        function columnMetadata(builder, tableName, name) {
            const reference = builder._reference === null ? null : resolveReference(builder._reference, name);
            return freezeDeep({
                name,
                table: tableName,
                type: builder.type,
                enumValues: builder.enumValues,
                primaryKey: builder._primaryKey,
                nullable: builder._nullable,
                notNull: builder._notNull,
                unique: builder._unique,
                index: builder._index,
                default: builder._default,
                defaultNow: builder._defaultNow,
                generated: builder._generated,
                reference,
                concurrencyToken: builder._concurrencyToken,
                softDelete: builder._softDelete,
                private: builder._private,
            });
        }

        function resolveReference(reference, columnName) {
            let target;
            try {
                target = reference();
            } catch (error) {
                throw ormError("SLOPPY_ORM_INVALID_REFERENCE", `Sloppy ORM column '${columnName}' reference could not be resolved.`, { cause: String(error?.message ?? error) });
            }
            assertColumn(target, `column '${columnName}' reference target`);
            return {
                table: tableName(target.table),
                column: target.name,
            };
        }

        function makeColumnBuilder(type, enumValues = null, state = {}) {
            const builder = {
                type,
                enumValues,
                _primaryKey: state._primaryKey === true,
                _nullable: state._nullable === true,
                _notNull: state._notNull === true,
                _unique: state._unique === true,
                _index: state._index === true,
                _default: state._default,
                _defaultNow: state._defaultNow === true,
                _generated: state._generated === true,
                _reference: state._reference ?? null,
                _concurrencyToken: state._concurrencyToken === true,
                _softDelete: state._softDelete === true,
                _private: state._private === true,
            };
            function next(patch) {
                return makeColumnBuilder(type, enumValues, { ...builder, ...patch });
            }
            return Object.freeze({
                __sloppyOrmColumnBuilder: true,
                ...builder,
                primaryKey() {
                    return next({ _primaryKey: true, _notNull: true, _nullable: false });
                },
                notNull() {
                    return next({ _notNull: true, _nullable: false });
                },
                nullable() {
                    return next({ _nullable: true, _notNull: false });
                },
                unique() {
                    return next({ _unique: true });
                },
                index() {
                    return next({ _index: true });
                },
                default(value) {
                    validateDefaultValue(type, enumValues, value);
                    return next({ _default: value });
                },
                defaultNow() {
                    if (type !== "instant" && type !== "date") {
                        throw ormError("SLOPPY_ORM_INVALID_DEFAULT", "Sloppy ORM defaultNow() is only valid on instant/date columns.");
                    }
                    return next({ _defaultNow: true });
                },
                generated() {
                    return next({ _generated: true });
                },
                references(callback) {
                    if (typeof callback !== "function") {
                        throw new TypeError("Sloppy ORM references() expects a callback returning a column.");
                    }
                    return next({ _reference: callback });
                },
                concurrencyToken() {
                    if (type !== "int" && type !== "bigint") {
                        throw ormError("SLOPPY_ORM_INVALID_CONCURRENCY_TOKEN", "Sloppy ORM concurrencyToken() requires an int or bigint column.");
                    }
                    return next({ _concurrencyToken: true, _notNull: true, _nullable: false });
                },
                softDelete() {
                    if (type !== "instant" && type !== "date") {
                        throw ormError("SLOPPY_ORM_INVALID_SOFT_DELETE", "Sloppy ORM softDelete() requires a nullable instant/date column.");
                    }
                    if (builder._nullable !== true || builder._notNull === true) {
                        throw ormError("SLOPPY_ORM_INVALID_SOFT_DELETE", "Sloppy ORM soft-delete column must be nullable instant/date.");
                    }
                    return next({ _softDelete: true, _nullable: true, _notNull: false, _index: true });
                },
                private() {
                    return next({ _private: true });
                },
            });
        }

        function validateDefaultValue(type, enumValues, value) {
            if (value === undefined) {
                throw ormError("SLOPPY_ORM_INVALID_DEFAULT", "Sloppy ORM default(undefined) is not allowed.");
            }
            if (value === null) {
                return;
            }
            if ((type === "text" || type === "uuid" || type === "instant" || type === "date" || type === "decimal" || type === "bigint") && typeof value !== "string") {
                throw ormError("SLOPPY_ORM_INVALID_DEFAULT", `Sloppy ORM ${type} default must be a string.`);
            }
            if ((type === "int" || type === "number") && typeof value !== "number") {
                throw ormError("SLOPPY_ORM_INVALID_DEFAULT", `Sloppy ORM ${type} default must be a number.`);
            }
            if (type === "bool" && typeof value !== "boolean") {
                throw ormError("SLOPPY_ORM_INVALID_DEFAULT", "Sloppy ORM bool default must be a boolean.");
            }
            if (type === "enum" && !enumValues.includes(value)) {
                throw ormError("SLOPPY_ORM_INVALID_DEFAULT", "Sloppy ORM enum default must be one of the enum values.");
            }
        }

        function createColumn(tableObject, metadata) {
            const columnObject = {
                [ORM_COLUMN]: true,
                table: tableObject,
                name: metadata.name,
                metadata,
                eq(value) {
                    return binaryExpr("=", columnExpr(columnObject), valueExpr(value));
                },
                ne(value) {
                    return binaryExpr("<>", columnExpr(columnObject), valueExpr(value));
                },
                gt(value) {
                    return binaryExpr(">", columnExpr(columnObject), valueExpr(value));
                },
                gte(value) {
                    return binaryExpr(">=", columnExpr(columnObject), valueExpr(value));
                },
                lt(value) {
                    return binaryExpr("<", columnExpr(columnObject), valueExpr(value));
                },
                lte(value) {
                    return binaryExpr("<=", columnExpr(columnObject), valueExpr(value));
                },
                isNull() {
                    return unarySqlExpr(columnExpr(columnObject), "is null");
                },
                isNotNull() {
                    return unarySqlExpr(columnExpr(columnObject), "is not null");
                },
                like(value) {
                    return binaryExpr("like", columnExpr(columnObject), valueExpr(value));
                },
                ilike(value) {
                    return binaryExpr("ilike", columnExpr(columnObject), valueExpr(value));
                },
                in(values) {
                    return listExpr("in", columnExpr(columnObject), values);
                },
                notIn(values) {
                    return listExpr("not in", columnExpr(columnObject), values);
                },
                startsWith(value) {
                    return binaryExpr("like", columnExpr(columnObject), valueExpr(`${value}%`));
                },
                contains(value) {
                    return binaryExpr("like", columnExpr(columnObject), valueExpr(`%${value}%`));
                },
                endsWith(value) {
                    return binaryExpr("like", columnExpr(columnObject), valueExpr(`%${value}`));
                },
                asc() {
                    return orderExpr(columnExpr(columnObject), "asc");
                },
                desc() {
                    return orderExpr(columnExpr(columnObject), "desc");
                },
            };
            return Object.freeze(columnObject);
        }

        function schemaForColumn(columnMeta, { optional = false, patch = false } = {}) {
            const factory = COLUMN_TYPES[columnMeta.type]?.schema;
            if (factory === undefined) {
                throw ormError("SLOPPY_ORM_UNSUPPORTED_COLUMN_TYPE", `Sloppy ORM column type '${columnMeta.type}' is not supported.`);
            }
            let current = factory(columnMeta);
            if (columnMeta.nullable) {
                current = current.nullable();
            }
            if (optional || patch || columnMeta.generated || columnMeta.default !== undefined || columnMeta.defaultNow) {
                current = current.optional();
            }
            return current;
        }

        function makeObjectSchema(shape) {
            return Schema.object(shape);
        }

        function validateInsertValue(tableObject, value) {
            const result = tableObject.insertSchema.validate(value);
            if (!result.ok) {
                throwValidation("insert", result.issues);
            }
            return result.value;
        }

        function validatePatchValue(tableObject, patch, options = {}) {
            if (!isPlainObject(patch)) {
                throw new TypeError("Sloppy ORM patch must be a plain object.");
            }
            const schemaPatch = {};
            for (const [key, value] of Object.entries(patch)) {
                if (value === undefined) {
                    throw ormError("SLOPPY_ORM_UNDEFINED_PATCH_VALUE", `Sloppy ORM patch field '${key}' is undefined. Omit the field or set null explicitly.`);
                }
                const columnMeta = tableObject.metadata.columns[key];
                if (columnMeta === undefined) {
                    throw ormError("SLOPPY_ORM_UNKNOWN_COLUMN", `Sloppy ORM patch field '${key}' is not a column on '${tableName(tableObject)}'.`);
                }
                if (columnMeta.primaryKey && options.allowPrimaryKey !== true) {
                    throw ormError("SLOPPY_ORM_PRIMARY_KEY_PATCH", `Sloppy ORM patch field '${key}' is a primary key.`);
                }
                if (columnMeta.generated) {
                    throw ormError("SLOPPY_ORM_GENERATED_PATCH", `Sloppy ORM patch field '${key}' is generated.`);
                }
                if (columnMeta.private && options.allowPrivate !== true) {
                    throw ormError("SLOPPY_ORM_PRIVATE_PATCH", `Sloppy ORM patch field '${key}' is private.`);
                }
                if (value === null && !columnMeta.nullable) {
                    throw ormError("SLOPPY_ORM_NOT_NULL_PATCH", `Sloppy ORM patch field '${key}' is not nullable.`);
                }
                if (isPatchOperation(value)) {
                    validatePatchOperation(columnMeta, value);
                    continue;
                }
                schemaPatch[key] = value;
            }
            const result = tableObject.patchSchema.validate(schemaPatch);
            if (!result.ok) {
                throwValidation("patch", result.issues);
            }
            return freezeDeep({ ...result.value, ...Object.fromEntries(Object.entries(patch).filter(([, value]) => isPatchOperation(value))) });
        }

        function isPatchOperation(value) {
            return value !== null && typeof value === "object" && value.__sloppyOrmOperation === true;
        }

        function validatePatchOperation(columnMeta, operationValue) {
            if ((operationValue.kind === "increment" || operationValue.kind === "decrement") && columnMeta.type !== "int" && columnMeta.type !== "bigint" && columnMeta.type !== "number" && columnMeta.type !== "decimal") {
                throw ormError("SLOPPY_ORM_INVALID_PATCH_OPERATION", `Sloppy ORM ${operationValue.kind}() requires a numeric column.`);
            }
            if (operationValue.kind === "setNow" && columnMeta.type !== "instant" && columnMeta.type !== "date") {
                throw ormError("SLOPPY_ORM_INVALID_PATCH_OPERATION", "Sloppy ORM setNow() requires an instant/date column.");
            }
        }

        function throwValidation(operation, issues) {
            throw ormError("SLOPPY_ORM_VALIDATION_FAILED", `Sloppy ORM ${operation} validation failed.`, { issues });
        }

        function table(name, definition) {
            assertIdentifier(name, "table name");
            if (!isPlainObject(definition) || Object.keys(definition).length === 0) {
                throw ormError("SLOPPY_ORM_INVALID_TABLE", "Sloppy ORM table definition must be a non-empty plain object.");
            }
            const columns = {};
            const columnMetadataEntries = {};
            const tableObject = {
                [ORM_TABLE]: true,
                name,
            };
            Object.defineProperty(tableObject, ORM_RELATIONS, {
                value: [],
                enumerable: false,
            });
            const seenColumns = new Set();
            for (const [columnName, builder] of Object.entries(definition)) {
                assertIdentifier(columnName, "column name");
                if (seenColumns.has(columnName)) {
                    throw ormError("SLOPPY_ORM_DUPLICATE_COLUMN", `Sloppy ORM column '${columnName}' is duplicated.`);
                }
                if (builder?.__sloppyOrmColumnBuilder !== true) {
                    throw new TypeError(`Sloppy ORM table column '${columnName}' must be created with column.*().`);
                }
                seenColumns.add(columnName);
                const meta = columnMetadata(builder, name, columnName);
                columnMetadataEntries[columnName] = meta;
                columns[columnName] = createColumn(tableObject, meta);
            }
            const primaryKeys = Object.values(columnMetadataEntries).filter((current) => current.primaryKey);
            const concurrencyTokens = Object.values(columnMetadataEntries).filter((current) => current.concurrencyToken);
            const softDeletes = Object.values(columnMetadataEntries).filter((current) => current.softDelete);
            if (concurrencyTokens.length > 1) {
                throw ormError("SLOPPY_ORM_MULTIPLE_CONCURRENCY_TOKENS", `Sloppy ORM table '${name}' has multiple concurrency token columns.`);
            }
            if (softDeletes.length > 1) {
                throw ormError("SLOPPY_ORM_MULTIPLE_SOFT_DELETE_COLUMNS", `Sloppy ORM table '${name}' has multiple soft-delete columns.`);
            }
            for (const meta of softDeletes) {
                if (!meta.nullable || (meta.type !== "instant" && meta.type !== "date")) {
                    throw ormError("SLOPPY_ORM_INVALID_SOFT_DELETE", "Sloppy ORM soft-delete column must be nullable instant/date.");
                }
            }

            const frozenColumns = freezeDeep(columnMetadataEntries);
            const metadata = freezeDeep({
                name,
                columns: frozenColumns,
                primaryKey: primaryKeys.map((current) => current.name),
                unique: Object.values(frozenColumns).filter((current) => current.unique).map((current) => current.name),
                indexes: Object.values(frozenColumns).filter((current) => current.index).map((current) => current.name),
                foreignKeys: Object.values(frozenColumns).filter((current) => current.reference !== null).map((current) => ({
                    column: current.name,
                    foreignTable: current.reference.table,
                    foreignColumn: current.reference.column,
                })),
                privateColumns: Object.values(frozenColumns).filter((current) => current.private).map((current) => current.name),
                softDeleteColumn: softDeletes[0]?.name ?? null,
                concurrencyTokenColumn: concurrencyTokens[0]?.name ?? null,
            });
            const rowShape = {};
            const insertShape = {};
            const patchShape = {};
            for (const [columnName, meta] of Object.entries(frozenColumns)) {
                rowShape[columnName] = schemaForColumn(meta);
                if (!meta.generated && !meta.primaryKey) {
                    insertShape[columnName] = schemaForColumn(meta, {
                        optional: meta.default !== undefined || meta.defaultNow || meta.nullable,
                    });
                } else if (meta.primaryKey && !meta.generated) {
                    insertShape[columnName] = schemaForColumn(meta, { optional: false });
                }
                if (!meta.generated && !meta.primaryKey && !meta.private) {
                    patchShape[columnName] = schemaForColumn(meta, { patch: true });
                }
            }
            Object.assign(tableObject, columns, {
                metadata,
                rowSchema: makeObjectSchema(rowShape),
                insertSchema: addPick(makeObjectSchema(insertShape)),
                patchSchema: addPick(makeObjectSchema(patchShape)),
                get primaryKey() {
                    return Object.freeze(primaryKeys.map((current) => columns[current.name]));
                },
                get privateColumns() {
                    return metadata.privateColumns;
                },
                publicSchema(names = undefined) {
                    const selected = normalizeColumnNames(tableObject, names, { includePrivate: false });
                    const shape = {};
                    for (const columnName of selected) {
                        shape[columnName] = rowShape[columnName];
                    }
                    return addPick(makeObjectSchema(shape));
                },
                public(row, names = undefined) {
                    const selected = normalizeColumnNames(tableObject, names, { includePrivate: false });
                    const output = {};
                    for (const columnName of selected) {
                        if (Object.prototype.hasOwnProperty.call(row, columnName)) {
                            output[columnName] = row[columnName];
                        }
                    }
                    return immutableRow(output);
                },
                pick(...names) {
                    return tableObject.publicSchema(names);
                },
                mapper() {
                    return (rowValue) => immutableRow(rowValue);
                },
                insert(db, values) {
                    return createInsertCommand(tableObject, db, values);
                },
                insertMany(db, rows) {
                    return insertMany(tableObject, db, rows);
                },
                updateById(db, id, patch, options = {}) {
                    return updateById(tableObject, db, id, patch, options);
                },
                deleteById(db, id, options = {}) {
                    return deleteById(tableObject, db, id, options);
                },
                softDeleteById(db, id, options = {}) {
                    return softDeleteById(tableObject, db, id, options);
                },
                findById(db, id) {
                    return orm.from(tableObject).where((t) => pkPredicate(tableObject, t, id)).singleOrDefault(db);
                },
                findOne(db, predicate) {
                    return orm.from(tableObject).where(predicate).singleOrDefault(db);
                },
                exists(db, predicate = undefined) {
                    const query = predicate === undefined ? orm.from(tableObject) : orm.from(tableObject).where(predicate);
                    return query.any(db);
                },
                count(db, predicate = undefined) {
                    const query = predicate === undefined ? orm.from(tableObject) : orm.from(tableObject).where(predicate);
                    return query.count(db);
                },
                edit(row) {
                    return createEditor(tableObject, row);
                },
            });
            Object.freeze(tableObject);
            return tableObject;
        }

        function addPick(objectSchema) {
            if (!isSchema(objectSchema) || objectSchema.kind !== "object") {
                return objectSchema;
            }
            return Object.freeze({
                ...objectSchema,
                pick(...names) {
                    const flatNames = names.flat();
                    const shape = {};
                    for (const name of flatNames) {
                        if (!Object.prototype.hasOwnProperty.call(objectSchema.shape, name)) {
                            throw ormError("SLOPPY_ORM_SCHEMA_PICK_UNKNOWN_FIELD", `Sloppy ORM schema pick field '${name}' does not exist.`);
                        }
                        shape[name] = objectSchema.shape[name];
                    }
                    return addPick(Schema.object(shape));
                },
            });
        }

        function normalizeColumnNames(tableObject, names, options = {}) {
            assertTable(tableObject);
            const allNames = Object.keys(tableObject.metadata.columns);
            const selected = names === undefined && options.includePrivate !== true
                ? allNames.filter((name) => !tableObject.metadata.columns[name].private)
                : (names === undefined ? allNames : names.flat());
            for (const name of selected) {
                if (!Object.prototype.hasOwnProperty.call(tableObject.metadata.columns, name)) {
                    throw ormError("SLOPPY_ORM_UNKNOWN_COLUMN", `Sloppy ORM column '${name}' does not exist on '${tableName(tableObject)}'.`);
                }
                if (options.includePrivate !== true && tableObject.metadata.columns[name].private) {
                    throw ormError("SLOPPY_ORM_PRIVATE_COLUMN", `Sloppy ORM column '${name}' is private.`);
                }
            }
            return Object.freeze([...selected]);
        }

        function pkPredicate(tableObject, proxy, id) {
            const keys = tableObject.primaryKey;
            if (keys.length !== 1) {
                throw ormError("SLOPPY_ORM_PRIMARY_KEY_REQUIRED", `Sloppy ORM table '${tableName(tableObject)}' requires exactly one primary key for this operation.`);
            }
            return proxy[keys[0].name].eq(id);
        }

        function columnTypeFactory(type) {
            return () => makeColumnBuilder(type);
        }

        const column = Object.freeze({
            text: columnTypeFactory("text"),
            string: columnTypeFactory("text"),
            int: columnTypeFactory("int"),
            integer: columnTypeFactory("int"),
            bigint: columnTypeFactory("bigint"),
            number: columnTypeFactory("number"),
            float: columnTypeFactory("number"),
            decimal: columnTypeFactory("decimal"),
            bool: columnTypeFactory("bool"),
            boolean: columnTypeFactory("bool"),
            uuid: columnTypeFactory("uuid"),
            instant: columnTypeFactory("instant"),
            timestamp: columnTypeFactory("instant"),
            date: columnTypeFactory("date"),
            json: columnTypeFactory("json"),
            blob: columnTypeFactory("blob"),
            bytes: columnTypeFactory("blob"),
            enum(values) {
                if (!Array.isArray(values) || values.length === 0 || !values.every((value) => typeof value === "string" && value.length > 0)) {
                    throw ormError("SLOPPY_ORM_INVALID_ENUM", "Sloppy ORM enum values must be a non-empty array of strings.");
                }
                return makeColumnBuilder("enum", Object.freeze([...values]));
            },
        });

        function expr(kind, payload) {
            return Object.freeze({ [ORM_EXPR]: true, kind, ...payload });
        }

        function isExpr(value) {
            return value !== null && typeof value === "object" && value[ORM_EXPR] === true;
        }

        function isRawSql(value) {
            return value !== null && typeof value === "object" && value[ORM_RAW] === true;
        }

        function expressionArg(value, subject) {
            if (isExpr(value)) {
                return value;
            }
            if (isRawSql(value)) {
                return expr("raw", { raw: value });
            }
            throw ormError("SLOPPY_ORM_INVALID_EXPRESSION", `Sloppy ORM ${subject} expects expressions.`);
        }

        function columnExpr(columnObject) {
            assertColumn(columnObject);
            return expr("column", { column: columnObject });
        }

        function valueExpr(value) {
            if (isExpr(value)) {
                return value;
            }
            if (value !== null && typeof value === "object" && value[ORM_COLUMN] === true) {
                return columnExpr(value);
            }
            if (value !== null && typeof value === "object" && value[ORM_RAW] === true) {
                return expr("raw", { raw: value });
            }
            return expr("value", { value });
        }

        function binaryExpr(operator, left, right) {
            return expr("binary", { operator, left, right });
        }

        function unarySqlExpr(inner, suffix) {
            return expr("unary-suffix", { inner, suffix });
        }

        function listExpr(operator, left, values) {
            if (!Array.isArray(values) || values.length === 0) {
                throw ormError("SLOPPY_ORM_INVALID_LIST_EXPRESSION", `Sloppy ORM ${operator} expression requires a non-empty array.`);
            }
            return expr("list", { operator, left, values: values.map(valueExpr) });
        }

        function orderExpr(inner, direction) {
            return expr("order", { inner, direction });
        }

        function and(...items) {
            const expressions = items.flat().filter(Boolean).map((item) => expressionArg(item, "and()"));
            if (expressions.length === 0) {
                throw ormError("SLOPPY_ORM_INVALID_EXPRESSION", "Sloppy ORM and() expects expressions.");
            }
            return expr("logical", { operator: "and", expressions });
        }

        function or(...items) {
            const expressions = items.flat().filter(Boolean).map((item) => expressionArg(item, "or()"));
            if (expressions.length === 0) {
                throw ormError("SLOPPY_ORM_INVALID_EXPRESSION", "Sloppy ORM or() expects expressions.");
            }
            return expr("logical", { operator: "or", expressions });
        }

        function not(item) {
            return expr("not", { inner: expressionArg(item, "not()") });
        }

        function rawSql(strings, ...values) {
            const lowered = dataSql(strings, ...values);
            return Object.freeze({ [ORM_RAW]: true, provider: "any", query: lowered });
        }

        rawSql.sqlite = function sqliteRaw(strings, ...values) {
            return Object.freeze({ [ORM_RAW]: true, provider: "sqlite", query: dataSql(strings, ...values) });
        };

        rawSql.postgres = function postgresRaw(strings, ...values) {
            return Object.freeze({ [ORM_RAW]: true, provider: "postgres", query: dataSql.lower(strings, values, { placeholderStyle: "postgres" }) });
        };

        rawSql.sqlserver = function sqlserverRaw(strings, ...values) {
            return Object.freeze({ [ORM_RAW]: true, provider: "sqlserver", query: dataSql(strings, ...values) });
        };

        Object.freeze(rawSql);

        function providerKind(db, fallback = "sqlite") {
            const debug = typeof db?.__debug === "function" ? db.__debug() : undefined;
            if (debug?.kind === "sqlite-connection") {
                return "sqlite";
            }
            if (debug?.kind === "postgres-connection") {
                return "postgres";
            }
            if (debug?.kind === "sqlserver-connection") {
                return "sqlserver";
            }
            if (typeof debug?.provider === "string" && DIALECTS[debug.provider] !== undefined) {
                return debug.provider;
            }
            if (debug?.placeholderStyle === "postgres") {
                return "postgres";
            }
            return fallback;
        }

        function dialectFor(db, options = {}) {
            const provider = options.provider ?? providerKind(db, options.fallbackProvider ?? "sqlite");
            const dialect = DIALECTS[provider];
            if (dialect === undefined) {
                throw ormError("SLOPPY_ORM_UNSUPPORTED_PROVIDER", `Sloppy ORM provider '${provider}' is not supported.`);
            }
            return dialect;
        }

        function compileExpression(expression, dialect, params, aliases = new Map()) {
            if (!isExpr(expression)) {
                throw ormError("SLOPPY_ORM_INVALID_EXPRESSION", "Sloppy ORM expected a query expression.");
            }
            switch (expression.kind) {
            case "column": {
                const alias = aliases.get(expression.column.table) ?? tableName(expression.column.table);
                return `${dialect.quote(alias)}.${dialect.quote(expression.column.name)}`;
            }
            case "value":
                params.push(expression.value);
                return dialect.placeholder(params.length);
            case "binary":
                if (expression.operator === "ilike" && dialect.provider !== "postgres") {
                    return `(lower(${compileExpression(expression.left, dialect, params, aliases)}) like lower(${compileExpression(expression.right, dialect, params, aliases)}))`;
                }
                return `(${compileExpression(expression.left, dialect, params, aliases)} ${expression.operator} ${compileExpression(expression.right, dialect, params, aliases)})`;
            case "unary-suffix":
                return `(${compileExpression(expression.inner, dialect, params, aliases)} ${expression.suffix})`;
            case "list":
                return `(${compileExpression(expression.left, dialect, params, aliases)} ${expression.operator} (${expression.values.map((value) => compileExpression(value, dialect, params, aliases)).join(", ")}))`;
            case "logical":
                return `(${expression.expressions.map((item) => compileExpression(item, dialect, params, aliases)).join(` ${expression.operator} `)})`;
            case "not":
                return `(not ${compileExpression(expression.inner, dialect, params, aliases)})`;
            case "raw":
                if (expression.raw.provider !== "any" && expression.raw.provider !== dialect.provider) {
                    throw ormError("SLOPPY_ORM_PROVIDER_SQL_MISMATCH", `Sloppy ORM raw SQL fragment for '${expression.raw.provider}' cannot run on '${dialect.provider}'.`);
                }
                {
                    const base = params.length;
                    let index = 0;
                    const placeholderPattern = expression.raw.query.placeholderStyle === "postgres" ? /\$\d+/gu : /\?/gu;
                    const text = expression.raw.query.text.replace(placeholderPattern, () => dialect.placeholder(base + ++index));
                    if (index !== expression.raw.query.parameters.length) {
                        throw ormError("SLOPPY_ORM_RAW_SQL_PLACEHOLDER_MISMATCH", "Sloppy ORM raw SQL fragment placeholder count does not match its parameters.");
                    }
                    for (const value of expression.raw.query.parameters) {
                        params.push(value);
                    }
                    return text;
                }
            default:
                throw ormError("SLOPPY_ORM_INVALID_EXPRESSION", `Sloppy ORM expression kind '${expression.kind}' is not supported.`);
            }
        }

        function createTableProxy(tableObject) {
            const proxy = {};
            for (const [name, value] of Object.entries(tableObject.metadata.columns)) {
                void value;
                proxy[name] = tableObject[name];
            }
            return Object.freeze(proxy);
        }

        function createRelationProxy(tableObject) {
            const proxy = {};
            for (const relationEntry of relationsFor(tableObject)) {
                Object.defineProperty(proxy, relationEntry.name, {
                    enumerable: true,
                    get() {
                        return createIncludeBuilder(relationEntry);
                    },
                });
            }
            return Object.freeze(proxy);
        }

        function joinedIncludesFor(state) {
            return Object.freeze(state.includes.filter((include) => {
                if (include.relation.kind !== "one" || include.__state.options.strategy === "split") {
                    return false;
                }
                return include.__state.where === null && include.__state.limit === null;
            }));
        }

        function splitIncludesFor(state, joinIncludes) {
            if (joinIncludes.length === 0) {
                return state.includes;
            }
            const joined = new Set(joinIncludes);
            return Object.freeze(state.includes.filter((include) => !joined.has(include)));
        }

        function joinColumnAlias(relationEntry, columnName) {
            return `${relationEntry.name}__${columnName}`;
        }

        function applyJoinedIncludes(rows, joinIncludes) {
            if (joinIncludes.length === 0) {
                return rows;
            }
            return rows.map((row) => {
                const output = {};
                for (const [key, value] of Object.entries(row)) {
                    if (!joinIncludes.some((include) => key.startsWith(`${include.relation.name}__`))) {
                        output[key] = value;
                    }
                }
                for (const include of joinIncludes) {
                    const relationEntry = include.relation;
                    const related = {};
                    let hasValue = false;
                    for (const columnName of Object.keys(relationEntry.target.metadata.columns)) {
                        const alias = joinColumnAlias(relationEntry, columnName);
                        if (!Object.prototype.hasOwnProperty.call(row, alias)) {
                            continue;
                        }
                        const value = row[alias];
                        related[columnName] = value;
                        if (value !== null && value !== undefined) {
                            hasValue = true;
                        }
                    }
                    output[relationEntry.name] = hasValue ? immutableRow(related) : null;
                }
                return output;
            });
        }

        function projectionColumns(projection, tableObject) {
            if (projection === null || typeof projection !== "object" || Array.isArray(projection)) {
                throw ormError("SLOPPY_ORM_INVALID_PROJECTION", "Sloppy ORM select() callback must return an object.");
            }
            const entries = [];
            for (const [alias, value] of Object.entries(projection)) {
                if (value !== null && typeof value === "object" && value[ORM_COLUMN] === true) {
                    entries.push({ alias, expression: columnExpr(value), column: value });
                } else if (isExpr(value)) {
                    entries.push({ alias, expression: value, column: null });
                } else {
                    throw ormError("SLOPPY_ORM_INVALID_PROJECTION", `Sloppy ORM projection field '${alias}' must be a column or expression.`);
                }
            }
            if (entries.length === 0) {
                throw ormError("SLOPPY_ORM_INVALID_PROJECTION", "Sloppy ORM projection cannot be empty.");
            }
            void tableObject;
            return Object.freeze(entries.map((entry) => Object.freeze(entry)));
        }

        function buildSelectSql(state, db, options = {}) {
            const dialect = dialectFor(db, options);
            const params = [];
            const alias = "t0";
            const aliases = new Map([[state.table, alias]]);
            const joinIncludes = joinedIncludesFor(state);
            const selectParts = state.projection === null
                ? Object.keys(state.table.metadata.columns).map((name) => `${dialect.quote(alias)}.${dialect.quote(name)} as ${dialect.quote(name)}`)
                : state.projection.map((item) => `${compileExpression(item.expression, dialect, params, aliases)} as ${dialect.quote(item.alias)}`);
            const joinParts = [];
            joinIncludes.forEach((include, index) => {
                const relationEntry = include.relation;
                const joinAlias = `t${index + 1}`;
                aliases.set(relationEntry.target, joinAlias);
                joinParts.push(
                    `left join ${dialect.quote(tableName(relationEntry.target))} ${dialect.quote(joinAlias)} on ${dialect.quote(alias)}.${dialect.quote(relationEntry.local.name)} = ${dialect.quote(joinAlias)}.${dialect.quote(relationEntry.foreign.name)}`,
                );
                for (const columnName of Object.keys(relationEntry.target.metadata.columns)) {
                    selectParts.push(`${dialect.quote(joinAlias)}.${dialect.quote(columnName)} as ${dialect.quote(joinColumnAlias(relationEntry, columnName))}`);
                }
            });
            const parts = [
                `select ${selectParts.join(", ")}`,
                `from ${dialect.quote(tableName(state.table))} ${dialect.quote(alias)}`,
            ];
            parts.push(...joinParts);
            if (state.where !== null) {
                parts.push(`where ${compileExpression(state.where, dialect, params, aliases)}`);
            }
            if (state.order.length !== 0) {
                parts.push(`order by ${state.order.map((item) => {
                    const current = item.kind === "order" ? item : orderExpr(item, "asc");
                    return `${compileExpression(current.inner, dialect, params, aliases)} ${current.direction}`;
                }).join(", ")}`);
            }
            const limitOffset = dialect.limitOffset(state.limit, state.offset);
            if (limitOffset.length !== 0) {
                if (dialect.provider === "sqlserver" && state.order.length === 0) {
                    parts.push(`order by ${dialect.quote(alias)}.${dialect.quote(primaryKeyColumn(state.table).name)}`);
                }
                parts.push(limitOffset);
            }
            return { text: parts.join(" "), params, dialect, joinIncludes };
        }

        function lowerQuery(text, params, dialect) {
            if (params.length === 0) {
                return dataSql.lower([text], [], { placeholderStyle: dialect.placeholderStyle });
            }
            const strings = [];
            let last = 0;
            const placeholderPattern = dialect.provider === "postgres" ? /\$\d+/gu : /\?/gu;
            for (const match of text.matchAll(placeholderPattern)) {
                strings.push(text.slice(last, match.index));
                last = match.index + match[0].length;
            }
            strings.push(text.slice(last));
            return dataSql.lower(strings, params, { placeholderStyle: dialect.placeholderStyle });
        }

        function callProvider(db, method, query, options) {
            const forwarded = providerOperationOptions(options);
            return forwarded === undefined
                ? db[method](query.text, [...query.parameters])
                : db[method](query.text, [...query.parameters], forwarded);
        }

        function providerOperationOptions(options) {
            if (options === undefined || options === null || typeof options !== "object") {
                return undefined;
            }
            const forwarded = {};
            for (const key of ["batchSize", "maxRows", "mode", "timeoutMs"]) {
                if (options[key] !== undefined) {
                    forwarded[key] = options[key];
                }
            }
            return Object.keys(forwarded).length === 0 ? undefined : Object.freeze(forwarded);
        }

        function createQueryBuilder(tableObject, state = undefined) {
            assertTable(tableObject);
            const current = state ?? {
                table: tableObject,
                where: null,
                projection: null,
                order: [],
                offset: null,
                limit: null,
                includes: [],
            };
            function next(patch) {
                return createQueryBuilder(tableObject, { ...current, ...patch });
            }
            async function toList(db, options = {}) {
                const compiled = buildSelectSql(current, db, options);
                const rows = await withProviderErrors("select", current.table, () =>
                    callProvider(db, "query", lowerQuery(compiled.text, compiled.params, compiled.dialect), options));
                const baseRows = immutableRows(applyJoinedIncludes(rows, compiled.joinIncludes));
                const splitIncludes = splitIncludesFor(current, compiled.joinIncludes);
                return loadIncludes(baseRows, { ...current, includes: splitIncludes }, db, options);
            }
            async function cursor(db, options = {}) {
                if (current.includes.length !== 0) {
                    throw ormError("SLOPPY_ORM_CURSOR_INCLUDE_UNSUPPORTED", "Sloppy ORM cursor() does not support includes because cursors must stay incremental.");
                }
                validateCursorOptions(options);
                const compiled = buildSelectSql(current, db, options);
                const cursorValue = await withProviderErrors("cursor", current.table, () =>
                    callProvider(db, "queryCursor", lowerQuery(compiled.text, compiled.params, compiled.dialect), options));
                return wrapOrmCursor(cursorValue, current, options);
            }
            const builder = {
                where(predicate) {
                    if (typeof predicate !== "function") {
                        throw new TypeError("Sloppy ORM where() expects a predicate callback.");
                    }
                    const expression = expressionArg(predicate(createTableProxy(tableObject), { and, or, not, sql: rawSql }), "where()");
                    return next({ where: current.where === null ? expression : and(current.where, expression) });
                },
                select(callback) {
                    if (typeof callback !== "function") {
                        throw new TypeError("Sloppy ORM select() expects a projection callback.");
                    }
                    return next({ projection: projectionColumns(callback(createTableProxy(tableObject)), tableObject) });
                },
                orderBy(...callbacks) {
                    return next({ order: normalizeOrderCallbacks(tableObject, callbacks) });
                },
                thenBy(...callbacks) {
                    return next({ order: Object.freeze([...current.order, ...normalizeOrderCallbacks(tableObject, callbacks)]) });
                },
                skip(count) {
                    assertNonNegativeInteger(count, "skip");
                    return next({ offset: count });
                },
                take(count) {
                    assertNonNegativeInteger(count, "take");
                    return next({ limit: count });
                },
                include(callback, options = {}) {
                    if (typeof callback !== "function") {
                        throw new TypeError("Sloppy ORM include() expects a relation callback.");
                    }
                    const include = callback(createRelationProxy(tableObject));
                    if (include?.__sloppyOrmInclude !== true) {
                        throw ormError("SLOPPY_ORM_INVALID_INCLUDE", "Sloppy ORM include() callback must return a relation include.");
                    }
                    return next({ includes: Object.freeze([...current.includes, include.withOptions(options)]) });
                },
                async first(db, options = {}) {
                    const rows = await this.take(1).toList(db, options);
                    if (rows.length === 0) {
                        throw ormError("SLOPPY_ORM_SEQUENCE_EMPTY", "Sloppy ORM first() expected at least one row.");
                    }
                    return rows[0];
                },
                async firstOrDefault(db, options = {}) {
                    const rows = await this.take(1).toList(db, options);
                    return rows[0] ?? null;
                },
                async single(db, options = {}) {
                    const rows = await this.take(2).toList(db, options);
                    if (rows.length === 0) {
                        throw ormError("SLOPPY_ORM_SEQUENCE_EMPTY", "Sloppy ORM single() expected one row.");
                    }
                    if (rows.length > 1) {
                        throw ormError("SLOPPY_ORM_SEQUENCE_MULTIPLE", "Sloppy ORM single() found more than one row.");
                    }
                    return rows[0];
                },
                async singleOrDefault(db, options = {}) {
                    const rows = await this.take(2).toList(db, options);
                    if (rows.length > 1) {
                        throw ormError("SLOPPY_ORM_SEQUENCE_MULTIPLE", "Sloppy ORM singleOrDefault() found more than one row.");
                    }
                    return rows[0] ?? null;
                },
                toList,
                async any(db, predicate = undefined, options = {}) {
                    const query = predicate === undefined ? builder : builder.where(predicate);
                    const rows = await query.take(1).select((t) => ({ one: primaryKeyColumn(tableObject, t) })).toList(db, options);
                    return rows.length !== 0;
                },
                async count(db, options = {}) {
                    const dialect = dialectFor(db, options);
                    const params = [];
                    const alias = "t0";
                    const aliases = new Map([[tableObject, alias]]);
                    const parts = [
                        `select count(*) as ${dialect.quote("count")}`,
                        `from ${dialect.quote(tableName(tableObject))} ${dialect.quote(alias)}`,
                    ];
                    if (current.where !== null) {
                        parts.push(`where ${compileExpression(current.where, dialect, params, aliases)}`);
                    }
                    const row = await withProviderErrors("count", tableObject, () =>
                        callProvider(db, "queryOne", lowerQuery(parts.join(" "), params, dialect), options));
                    return Number(row?.count ?? 0);
                },
                cursor,
                __debug() {
                    return Object.freeze({ ...current });
                },
            };
            return Object.freeze(builder);
        }

        function assertNonNegativeInteger(value, subject) {
            if (!Number.isInteger(value) || value < 0) {
                throw new TypeError(`Sloppy ORM ${subject} value must be a non-negative integer.`);
            }
        }

        function validateCursorOptions(options) {
            if (options.batchSize !== undefined && (!Number.isInteger(options.batchSize) || options.batchSize <= 0)) {
                throw new TypeError("Sloppy ORM cursor batchSize must be a positive integer.");
            }
            if (options.maxRows !== undefined && (!Number.isInteger(options.maxRows) || options.maxRows < 0)) {
                throw new TypeError("Sloppy ORM cursor maxRows must be a non-negative integer.");
            }
        }

        function normalizeOrderCallbacks(tableObject, callbacks) {
            const items = callbacks.flat().map((callback) => {
                const value = typeof callback === "function" ? callback(createTableProxy(tableObject)) : callback;
                if (value !== null && typeof value === "object" && value[ORM_COLUMN] === true) {
                    return orderExpr(columnExpr(value), "asc");
                }
                if (!isExpr(value)) {
                    throw ormError("SLOPPY_ORM_INVALID_ORDER", "Sloppy ORM orderBy() expects column/order expressions.");
                }
                return value;
            });
            return Object.freeze(items);
        }

        function primaryKeyColumn(tableObject, proxy = tableObject) {
            const keys = tableObject.primaryKey;
            if (keys.length !== 1) {
                throw ormError("SLOPPY_ORM_PRIMARY_KEY_REQUIRED", `Sloppy ORM table '${tableName(tableObject)}' requires exactly one primary key for this operation.`);
            }
            return proxy[keys[0].name];
        }

        function createInsertCommand(tableObject, db, values) {
            const input = validateInsertValue(tableObject, values);
            const command = {
                async execute(options = {}) {
                    const result = await executeInsert(tableObject, db, input, false, options);
                    return result;
                },
                async returning(options = {}) {
                    const rows = await executeInsert(tableObject, db, input, true, options);
                    return rows[0] ?? null;
                },
            };
            return Object.freeze(command);
        }

        async function executeInsert(tableObject, db, values, returning, options = {}) {
            const dialect = dialectFor(db, options);
            const params = [];
            const columns = Object.keys(values).filter((name) => values[name] !== undefined);
            if (columns.length === 0) {
                throw ormError("SLOPPY_ORM_EMPTY_INSERT", "Sloppy ORM insert requires at least one value.");
            }
            const placeholders = columns.map((name) => {
                params.push(values[name]);
                return dialect.placeholder(params.length);
            });
            const returningColumns = returning ? Object.values(tableObject.metadata.columns).filter((col) => !col.private) : [];
            const returningSql = returning ? dialect.returning(returningColumns) : "";
            const text = dialect.provider === "sqlserver" && returningSql.length !== 0
                ? `insert into ${dialect.quote(tableName(tableObject))} (${columns.map((name) => dialect.quote(name)).join(", ")})${returningSql} values (${placeholders.join(", ")})`
                : `insert into ${dialect.quote(tableName(tableObject))} (${columns.map((name) => dialect.quote(name)).join(", ")}) values (${placeholders.join(", ")})${returningSql}`;
            if (returning) {
                const rows = await withProviderErrors("insert returning", tableObject, () =>
                    callProvider(db, "query", lowerQuery(text, params, dialect), options));
                return immutableRows(rows);
            }
            return withProviderErrors("insert", tableObject, () =>
                callProvider(db, "exec", lowerQuery(text, params, dialect), options));
        }

        async function insertMany(tableObject, db, rows, options = {}) {
            if (!Array.isArray(rows) || rows.length === 0) {
                throw new TypeError("Sloppy ORM insertMany rows must be a non-empty array.");
            }
            return orm.transaction(db, async (tx) => {
                let affectedRows = 0;
                for (const row of rows) {
                    const result = await tableObject.insert(tx, row).execute(options);
                    affectedRows += Number(result?.affectedRows ?? 0);
                }
                return Object.freeze({ affectedRows });
            });
        }

        function updateExpressionsForPatch(tableObject, patch, dialect, params) {
            const sets = [];
            for (const [name, value] of Object.entries(patch)) {
                const expression = value !== null && typeof value === "object" && value.__sloppyOrmOperation === true
                    ? value
                    : null;
                if (expression?.kind === "increment") {
                    sets.push(`${dialect.quote(name)} = ${dialect.quote(name)} + ${dialect.placeholder(params.push(expression.value))}`);
                    continue;
                }
                if (expression?.kind === "decrement") {
                    sets.push(`${dialect.quote(name)} = ${dialect.quote(name)} - ${dialect.placeholder(params.push(expression.value))}`);
                    continue;
                }
                if (expression?.kind === "setNow") {
                    sets.push(`${dialect.quote(name)} = ${dialect.defaultNow}`);
                    continue;
                }
                if (expression?.kind === "raw") {
                    sets.push(`${dialect.quote(name)} = ${compileExpression(valueExpr(expression.value), dialect, params)}`);
                    continue;
                }
                params.push(value);
                sets.push(`${dialect.quote(name)} = ${dialect.placeholder(params.length)}`);
            }
            const tokenName = tableObject.metadata.concurrencyTokenColumn;
            if (tokenName !== null && !Object.prototype.hasOwnProperty.call(patch, tokenName)) {
                sets.push(`${dialect.quote(tokenName)} = ${dialect.quote(tokenName)} + 1`);
            }
            return sets;
        }

        async function updateById(tableObject, db, id, patch, options = {}) {
            const checked = validatePatchValue(tableObject, patch, options);
            const dialect = dialectFor(db, options);
            const params = [];
            const sets = updateExpressionsForPatch(tableObject, checked, dialect, params);
            if (sets.length === 0) {
                return Object.freeze({ affectedRows: 0 });
            }
            const pk = primaryKeyColumn(tableObject);
            params.push(id);
            const where = [`${dialect.quote(pk.name)} = ${dialect.placeholder(params.length)}`];
            const tokenName = tableObject.metadata.concurrencyTokenColumn;
            const expected = options.expected ?? {};
            if (tokenName !== null && expected[tokenName] !== undefined) {
                params.push(expected[tokenName]);
                where.push(`${dialect.quote(tokenName)} = ${dialect.placeholder(params.length)}`);
            }
            const text = `update ${dialect.quote(tableName(tableObject))} set ${sets.join(", ")} where ${where.join(" and ")}`;
            const result = await withProviderErrors("update", tableObject, () =>
                callProvider(db, "exec", lowerQuery(text, params, dialect), options));
            if (tokenName !== null && expected[tokenName] !== undefined && Number(result?.affectedRows ?? 0) === 0) {
                throw new SloppyOrmConcurrencyError(`Sloppy ORM update on '${tableName(tableObject)}' did not match the expected concurrency token.`);
            }
            return result;
        }

        async function deleteById(tableObject, db, id, options = {}) {
            const dialect = dialectFor(db, options);
            const pk = primaryKeyColumn(tableObject);
            const params = [id];
            const where = [`${dialect.quote(pk.name)} = ${dialect.placeholder(1)}`];
            const tokenName = tableObject.metadata.concurrencyTokenColumn;
            const expected = options.expected ?? {};
            if (tokenName !== null && expected[tokenName] !== undefined) {
                params.push(expected[tokenName]);
                where.push(`${dialect.quote(tokenName)} = ${dialect.placeholder(params.length)}`);
            }
            const result = await withProviderErrors("delete", tableObject, () =>
                callProvider(db, "exec", lowerQuery(`delete from ${dialect.quote(tableName(tableObject))} where ${where.join(" and ")}`, params, dialect), options));
            if (tokenName !== null && expected[tokenName] !== undefined && Number(result?.affectedRows ?? 0) === 0) {
                throw new SloppyOrmConcurrencyError(`Sloppy ORM delete on '${tableName(tableObject)}' did not match the expected concurrency token.`);
            }
            return result;
        }

        function softDeleteById(tableObject, db, id, options = {}) {
            const softDeleteColumn = tableObject.metadata.softDeleteColumn;
            if (softDeleteColumn === null) {
                throw ormError("SLOPPY_ORM_SOFT_DELETE_UNAVAILABLE", `Sloppy ORM table '${tableName(tableObject)}' has no soft-delete column.`);
            }
            return updateById(tableObject, db, id, {
                [softDeleteColumn]: operation.setNow(),
            }, { ...options, allowPrivate: true });
        }

        const operation = Object.freeze({
            increment(value = 1) {
                if (typeof value !== "number" || !Number.isFinite(value)) {
                    throw new TypeError("Sloppy ORM increment() value must be a finite number.");
                }
                return Object.freeze({ __sloppyOrmOperation: true, kind: "increment", value });
            },
            decrement(value = 1) {
                if (typeof value !== "number" || !Number.isFinite(value)) {
                    throw new TypeError("Sloppy ORM decrement() value must be a finite number.");
                }
                return Object.freeze({ __sloppyOrmOperation: true, kind: "decrement", value });
            },
            setNow() {
                return Object.freeze({ __sloppyOrmOperation: true, kind: "setNow" });
            },
            raw(value) {
                return Object.freeze({ __sloppyOrmOperation: true, kind: "raw", value });
            },
        });

        function createEditor(tableObject, row) {
            if (!isPlainObject(row)) {
                throw new TypeError("Sloppy ORM edit() row must be a plain object.");
            }
            const patch = {};
            const editor = {
                set(name, value) {
                    if (value === undefined) {
                        throw ormError("SLOPPY_ORM_UNDEFINED_PATCH_VALUE", `Sloppy ORM editor field '${name}' cannot be undefined.`);
                    }
                    validatePatchValue(tableObject, { [name]: value });
                    patch[name] = value;
                    return editor;
                },
                patch() {
                    return freezeDeep({ ...patch });
                },
                async save(db, options = {}) {
                    const pk = primaryKeyColumn(tableObject);
                    return tableObject.updateById(db, row[pk.name], patch, options);
                },
            };
            return Object.freeze(editor);
        }

        function relation(tableObject, callback) {
            assertTable(tableObject);
            if (typeof callback !== "function") {
                throw new TypeError("Sloppy ORM relation() expects a callback.");
            }
            const helpers = Object.freeze({
                one(target, options) {
                    return relationDefinition("one", tableObject, target, options);
                },
                many(target, options) {
                    return relationDefinition("many", tableObject, target, options);
                },
            });
            const definitions = callback(helpers);
            if (!isPlainObject(definitions) || Object.keys(definitions).length === 0) {
                throw ormError("SLOPPY_ORM_INVALID_RELATION", "Sloppy ORM relation() must return a non-empty object.");
            }
            const entries = tableObject[ORM_RELATIONS];
            for (const [name, definition] of Object.entries(definitions)) {
                assertIdentifier(name, "relation name");
                const next = freezeDeep({ name, ...definition });
                const existing = entries.findIndex((entry) => entry.name === name);
                if (existing >= 0) {
                    entries[existing] = next;
                } else {
                    entries.push(next);
                }
            }
            return tableObject;
        }

        function relationDefinition(kind, source, target, options) {
            assertTable(source, "relation source");
            assertTable(target, "relation target");
            if (!isPlainObject(options)) {
                throw new TypeError("Sloppy ORM relation options must be a plain object.");
            }
            assertColumn(options.local, "relation local");
            assertColumn(options.foreign, "relation foreign");
            if (options.local.table !== source) {
                throw new TypeError("Sloppy ORM relation local column must belong to the source table.");
            }
            if (options.foreign.table !== target) {
                throw new TypeError("Sloppy ORM relation foreign column must belong to the target table.");
            }
            return {
                kind,
                target,
                local: options.local,
                foreign: options.foreign,
            };
        }

        function relationsFor(tableObject) {
            return Object.freeze([...(tableObject[ORM_RELATIONS] ?? [])]);
        }

        function createIncludeBuilder(relationEntry, state = {}) {
            const current = {
                where: state.where ?? null,
                limit: state.limit ?? null,
                options: state.options ?? {},
            };
            const include = {
                __sloppyOrmInclude: true,
                relation: relationEntry,
                where(predicate) {
                    if (typeof predicate !== "function") {
                        throw new TypeError("Sloppy ORM include.where() expects a predicate callback.");
                    }
                    const expression = predicate(createTableProxy(relationEntry.target), { and, or, not, sql: rawSql });
                    const checked = expressionArg(expression, "include.where()");
                    return createIncludeBuilder(relationEntry, { ...current, where: checked });
                },
                take(count) {
                    assertNonNegativeInteger(count, "include.take");
                    return createIncludeBuilder(relationEntry, { ...current, limit: count });
                },
                withOptions(options) {
                    return createIncludeBuilder(relationEntry, { ...current, options: normalizeIncludeOptions(options) });
                },
                __state: current,
            };
            return Object.freeze(include);
        }

        function normalizeIncludeOptions(options) {
            if (options === undefined) {
                return Object.freeze({});
            }
            if (!isPlainObject(options)) {
                throw new TypeError("Sloppy ORM include options must be a plain object.");
            }
            if (options.strategy !== undefined && options.strategy !== "join" && options.strategy !== "split") {
                throw ormError("SLOPPY_ORM_INVALID_INCLUDE_STRATEGY", "Sloppy ORM include strategy must be 'join' or 'split'.");
            }
            return Object.freeze({ ...options });
        }

        async function loadIncludes(parentRows, queryState, db, options) {
            if (queryState.includes.length === 0 || parentRows.length === 0) {
                return parentRows;
            }
            let rows = parentRows.map((row) => ({ ...row }));
            for (const include of queryState.includes) {
                rows = await loadInclude(rows, include, db, options);
            }
            return immutableRows(rows);
        }

        async function loadInclude(parentRows, include, db, options) {
            const relationEntry = include.relation;
            const localName = relationEntry.local.name;
            const foreignName = relationEntry.foreign.name;
            const ids = [...new Set(parentRows.map((row) => row[localName]).filter((value) => value !== null && value !== undefined))];
            if (ids.length === 0) {
                return parentRows.map((row) => ({ ...row, [relationEntry.name]: relationEntry.kind === "many" ? Object.freeze([]) : null }));
            }
            let childQuery = orm.from(relationEntry.target).where((t) => t[foreignName].in(ids));
            if (include.__state.where !== null) {
                childQuery = childQuery.where(() => include.__state.where);
            }
            if (include.__state.limit !== null) {
                childQuery = childQuery.take(include.__state.limit);
            }
            const children = await childQuery.toList(db, options);
            const grouped = new Map();
            for (const child of children) {
                const key = child[foreignName];
                const bucket = grouped.get(key) ?? [];
                bucket.push(child);
                grouped.set(key, bucket);
            }
            return parentRows.map((row) => {
                const bucket = grouped.get(row[localName]) ?? [];
                return {
                    ...row,
                    [relationEntry.name]: relationEntry.kind === "many"
                        ? Object.freeze(bucket.map((item) => immutableRow(item)))
                        : (bucket[0] === undefined ? null : immutableRow(bucket[0])),
                };
            });
        }

        function wrapOrmCursor(cursorValue, state, options = {}) {
            let closed = false;
            let rowsSeen = 0;
            const maxRows = options.maxRows ?? null;
            const cursor = {
                provider: cursorValue.provider,
                mode: cursorValue.mode,
                columns: cursorValue.columns,
                columnNames: cursorValue.columnNames,
                selected: state.projection === null
                    ? Object.keys(state.table.metadata.columns)
                    : state.projection.map((item) => item.alias),
                get closed() {
                    return closed || cursorValue.closed === true;
                },
                async close() {
                    closed = true;
                    if (typeof cursorValue.close === "function") {
                        await cursorValue.close();
                    }
                },
                [Symbol.asyncIterator]() {
                    const iterator = cursorValue[Symbol.asyncIterator]();
                    return {
                        async next() {
                            try {
                                if (maxRows !== null && rowsSeen >= maxRows) {
                                    closed = true;
                                    await cursor.close();
                                    return { done: true };
                                }
                                const item = await iterator.next();
                                if (item.done) {
                                    closed = true;
                                    return item;
                                }
                                rowsSeen += 1;
                                return { done: false, value: immutableRow(item.value) };
                            } catch (error) {
                                closed = true;
                                await cursor.close().catch(() => {});
                                throw wrapProviderError(error, "cursor next", state.table);
                            }
                        },
                        async return() {
                            closed = true;
                            if (typeof iterator.return === "function") {
                                await iterator.return();
                            }
                            await cursor.close();
                            return { done: true };
                        },
                    };
                },
            };
            return Object.freeze(cursor);
        }

        async function transaction(db, callback) {
            if (typeof callback !== "function") {
                throw new TypeError("Sloppy ORM transaction callback must be a function.");
            }
            if (typeof db?.transaction !== "function") {
                throw ormError("SLOPPY_ORM_TRANSACTION_UNAVAILABLE", "Sloppy ORM transaction requires a database provider with transaction(callback).");
            }
            return db.transaction((tx) => callback(tx));
        }

        async function query(db, raw, mapper = undefined, options = {}) {
            if (raw === null || typeof raw !== "object" || raw[ORM_RAW] !== true) {
                throw new TypeError("Sloppy ORM query() expects a raw SQL fragment created by orm.sql.");
            }
            const dialect = dialectFor(db, options);
            if (raw.provider !== "any" && raw.provider !== dialect.provider) {
                throw ormError("SLOPPY_ORM_PROVIDER_SQL_MISMATCH", `Sloppy ORM raw SQL for '${raw.provider}' cannot run on '${dialect.provider}'.`);
            }
            const rows = await withProviderErrors("raw query", undefined, () =>
                callProvider(db, "query", raw.query, options));
            if (mapper === undefined) {
                return immutableRows(rows);
            }
            if (typeof mapper !== "function") {
                throw new TypeError("Sloppy ORM query() mapper must be a function.");
            }
            return immutableRows(rows.map((rowValue) => mapper(rowValue)));
        }

        function cursor(db, raw, options = {}) {
            if (raw === null || typeof raw !== "object" || raw[ORM_RAW] !== true) {
                throw new TypeError("Sloppy ORM cursor() expects a raw SQL fragment created by orm.sql.");
            }
            const dialect = dialectFor(db, options);
            if (raw.provider !== "any" && raw.provider !== dialect.provider) {
                throw ormError("SLOPPY_ORM_PROVIDER_SQL_MISMATCH", `Sloppy ORM raw SQL for '${raw.provider}' cannot run on '${dialect.provider}'.`);
            }
            return withProviderErrors("raw cursor", undefined, () =>
                callProvider(db, "queryCursor", raw.query, options));
        }

        function ndjson(cursorValue, mapper = undefined) {
            if (cursorValue === null || typeof cursorValue !== "object" || typeof cursorValue[Symbol.asyncIterator] !== "function") {
                throw new TypeError("Sloppy ORM ndjson() expects an ORM cursor or async iterable cursor.");
            }
            if (mapper !== undefined && typeof mapper !== "function") {
                throw new TypeError("Sloppy ORM ndjson() mapper must be a function.");
            }
            const stream = {
                contentType: "application/x-ndjson; charset=utf-8",
                selected: cursorValue.selected,
                columns: cursorValue.columns,
                columnNames: cursorValue.columnNames,
                async *[Symbol.asyncIterator]() {
                    try {
                        for await (const row of cursorValue) {
                            yield `${JSON.stringify(mapper === undefined ? row : mapper(row))}\n`;
                        }
                    } finally {
                        if (typeof cursorValue.close === "function" && cursorValue.closed !== true) {
                            await cursorValue.close();
                        }
                    }
                },
            };
            return Object.freeze(stream);
        }

        function columnSqlType(meta, dialect) {
            if (dialect.provider === "sqlserver" && meta.type === "text" && (meta.unique || meta.index)) {
                return "nvarchar(450)";
            }
            return dialect.types[meta.type];
        }

        function columnDefinitionSql(meta, dialect, options = {}) {
            const pieces = [dialect.quote(meta.name), columnSqlType(meta, dialect)];
            if (meta.primaryKey) {
                pieces.push("primary key");
            }
            if (meta.notNull && !meta.primaryKey) {
                pieces.push("not null");
            }
            if (meta.unique && !meta.primaryKey) {
                pieces.push("unique");
            }
            if (meta.defaultNow) {
                pieces.push(`default ${dialect.defaultNow}`);
            } else if (meta.default !== undefined) {
                pieces.push("default " + literalSql(meta.default));
            }
            if (meta.reference !== null && options.inlineReferences !== false) {
                pieces.push(`references ${dialect.quote(meta.reference.table)} (${dialect.quote(meta.reference.column)})`);
            }
            return pieces.join(" ");
        }

        function createIndexSql(tableObject, meta, dialect) {
            return `create index ${dialect.quote(`ix_${tableName(tableObject)}_${meta.name}`)} on ${dialect.quote(tableName(tableObject))} (${dialect.quote(meta.name)});`;
        }

        function createTableSql(tableObject, provider = "sqlite", options = {}) {
            assertTable(tableObject);
            const dialect = DIALECTS[provider];
            if (dialect === undefined) {
                throw ormError("SLOPPY_ORM_UNSUPPORTED_PROVIDER", `Sloppy ORM provider '${provider}' is not supported.`);
            }
            const name = tableName(tableObject);
            const lines = [];
            for (const meta of Object.values(tableObject.metadata.columns)) {
                lines.push(`  ${columnDefinitionSql(meta, dialect, options)}`);
            }
            const statements = [`create table ${dialect.quote(name)} (\n${lines.join(",\n")}\n);`];
            for (const meta of Object.values(tableObject.metadata.columns)) {
                if (meta.index && !meta.primaryKey && !meta.unique) {
                    statements.push(createIndexSql(tableObject, meta, dialect));
                }
            }
            return statements.join("\n");
        }

        function tableDependsOn(tableObject, targetName) {
            return tableObject.metadata.foreignKeys.some((foreignKey) => foreignKey.foreignTable === targetName && tableName(tableObject) !== targetName);
        }

        function orderMigrationTables(tables) {
            const remaining = [...tables];
            const ordered = [];
            while (remaining.length !== 0) {
                let moved = false;
                for (let index = 0; index < remaining.length; index += 1) {
                    const candidate = remaining[index];
                    const blocked = remaining.some((other) => other !== candidate && tableDependsOn(candidate, tableName(other)));
                    if (!blocked) {
                        ordered.push(candidate);
                        remaining.splice(index, 1);
                        moved = true;
                        break;
                    }
                }
                if (!moved) {
                    ordered.push(...remaining.splice(0, remaining.length));
                }
            }
            return ordered;
        }

        function constraintName(tableObject, meta) {
            return `fk_${tableName(tableObject)}_${meta.name}_${meta.reference.table}_${meta.reference.column}`;
        }

        function deferredForeignKeySql(tableObject, provider) {
            const dialect = DIALECTS[provider];
            const statements = [];
            for (const meta of Object.values(tableObject.metadata.columns)) {
                if (meta.reference === null) {
                    continue;
                }
                statements.push(`alter table ${dialect.quote(tableName(tableObject))} add constraint ${dialect.quote(constraintName(tableObject, meta))} foreign key (${dialect.quote(meta.name)}) references ${dialect.quote(meta.reference.table)} (${dialect.quote(meta.reference.column)});`);
            }
            return statements;
        }

        function literalSql(value) {
            if (value === null) {
                return "null";
            }
            if (typeof value === "number") {
                return String(value);
            }
            if (typeof value === "boolean") {
                return value ? "1" : "0";
            }
            return `'${String(value).replaceAll("'", "''")}'`;
        }

        function migrationScript(tables, options = {}) {
            const provider = options.provider ?? "sqlite";
            const tableList = orderMigrationTables(Array.isArray(tables) ? tables : [tables]);
            const inlineReferences = provider === "sqlite";
            const statements = tableList.map((tableEntry) => createTableSql(tableEntry, provider, { inlineReferences }));
            if (!inlineReferences) {
                for (const tableEntry of tableList) {
                    statements.push(...deferredForeignKeySql(tableEntry, provider));
                }
            }
            return `${statements.join("\n\n")}\n`;
        }

        function stableStringify(value) {
            if (value === null || typeof value !== "object") {
                return JSON.stringify(value);
            }
            if (Array.isArray(value)) {
                return `[${value.map((item) => stableStringify(item)).join(",")}]`;
            }
            return `{${Object.keys(value).sort().map((key) => `${JSON.stringify(key)}:${stableStringify(value[key])}`).join(",")}}`;
        }

        function migrationHash(value) {
            const text = typeof value === "string" ? value : stableStringify(value);
            let hash = 2166136261;
            for (let index = 0; index < text.length; index += 1) {
                hash ^= text.charCodeAt(index);
                hash = Math.imul(hash, 16777619) >>> 0;
            }
            return hash.toString(16).padStart(8, "0");
        }

        function migrationSnapshot(tables) {
            const tableList = (Array.isArray(tables) ? tables : [tables]).map((tableEntry) => {
                assertTable(tableEntry);
                return tableEntry;
            });
            const snapshotTables = tableList
                .map((tableObject) => {
                    const columns = Object.values(tableObject.metadata.columns).map((meta) => Object.freeze({
                        name: meta.name,
                        type: meta.type,
                        nullable: meta.nullable,
                        notNull: meta.notNull,
                        primaryKey: meta.primaryKey,
                        unique: meta.unique,
                        index: meta.index,
                        generated: meta.generated,
                        default: meta.default,
                        defaultNow: meta.defaultNow,
                        private: meta.private,
                        softDelete: meta.softDelete,
                        concurrencyToken: meta.concurrencyToken,
                        enumValues: meta.enumValues,
                        reference: meta.reference,
                    })).sort((left, right) => left.name.localeCompare(right.name));
                    return Object.freeze({
                        name: tableName(tableObject),
                        columns: Object.freeze(columns),
                        primaryKey: Object.freeze([...tableObject.metadata.primaryKey]),
                        unique: Object.freeze([...tableObject.metadata.unique].sort()),
                        indexes: Object.freeze([...tableObject.metadata.indexes].sort()),
                        foreignKeys: Object.freeze([...tableObject.metadata.foreignKeys].sort((left, right) => left.column.localeCompare(right.column))),
                        privateColumns: Object.freeze([...tableObject.metadata.privateColumns].sort()),
                        softDeleteColumn: tableObject.metadata.softDeleteColumn,
                        concurrencyTokenColumn: tableObject.metadata.concurrencyTokenColumn,
                    });
                })
                .sort((left, right) => left.name.localeCompare(right.name));
            const payload = Object.freeze({
                format: "sloppy.orm.snapshot.v1",
                tables: Object.freeze(snapshotTables),
            });
            return freezeDeep({ ...payload, checksum: migrationHash(payload) });
        }

        function snapshotTableMap(snapshot, subject) {
            if (!isPlainObject(snapshot) || snapshot.format !== "sloppy.orm.snapshot.v1" || !Array.isArray(snapshot.tables)) {
                throw ormError("SLOPPY_ORM_INVALID_MIGRATION_SNAPSHOT", `Sloppy ORM ${subject} snapshot is invalid.`);
            }
            return new Map(snapshot.tables.map((entry) => [entry.name, entry]));
        }

        function snapshotColumnMap(snapshotTable) {
            return new Map(snapshotTable.columns.map((entry) => [entry.name, entry]));
        }

        function migrationDiff(previousSnapshot, nextTables, options = {}) {
            const provider = options.provider ?? "sqlite";
            const dialect = DIALECTS[provider];
            if (dialect === undefined) {
                throw ormError("SLOPPY_ORM_UNSUPPORTED_PROVIDER", `Sloppy ORM provider '${provider}' is not supported.`);
            }
            const nextSnapshot = migrationSnapshot(nextTables);
            const previousTables = snapshotTableMap(previousSnapshot, "previous");
            const nextTablesByName = snapshotTableMap(nextSnapshot, "next");
            const statements = [];
            const destructiveChanges = [];
            const nextTableObjects = new Map((Array.isArray(nextTables) ? nextTables : [nextTables]).map((tableEntry) => [tableName(tableEntry), tableEntry]));

            for (const [name] of previousTables) {
                if (!nextTablesByName.has(name)) {
                    destructiveChanges.push(`drop table ${name}`);
                }
            }
            for (const [name, nextTable] of nextTablesByName) {
                const previousTable = previousTables.get(name);
                const nextTableObject = nextTableObjects.get(name);
                if (previousTable === undefined) {
                    statements.push(migrationScript(nextTableObject, { provider }).trimEnd());
                    continue;
                }
                const previousColumns = snapshotColumnMap(previousTable);
                const nextColumns = snapshotColumnMap(nextTable);
                for (const [columnName, previousColumn] of previousColumns) {
                    const nextColumn = nextColumns.get(columnName);
                    if (nextColumn === undefined) {
                        destructiveChanges.push(`drop column ${name}.${columnName}`);
                    } else if (stableStringify(previousColumn) !== stableStringify(nextColumn)) {
                        destructiveChanges.push(`alter column ${name}.${columnName}`);
                    }
                }
                for (const [columnName, nextColumn] of nextColumns) {
                    if (!previousColumns.has(columnName)) {
                        statements.push(`alter table ${dialect.quote(name)} add ${columnDefinitionSql(nextColumn, dialect)};`);
                    }
                }
                for (const indexName of nextTable.indexes) {
                    if (!previousTable.indexes.includes(indexName)) {
                        statements.push(createIndexSql(nextTableObject, nextTableObject.metadata.columns[indexName], dialect));
                    }
                }
            }
            if (destructiveChanges.length !== 0 && options.allowDestructive !== true) {
                throw ormError("SLOPPY_ORM_DESTRUCTIVE_MIGRATION", "Sloppy ORM migration diff contains destructive changes. Pass allowDestructive: true to inspect them explicitly.", {
                    changes: destructiveChanges,
                });
            }
            return freezeDeep({
                provider,
                fromChecksum: previousSnapshot.checksum,
                toChecksum: nextSnapshot.checksum,
                destructive: destructiveChanges.length !== 0,
                destructiveChanges,
                statements,
                sql: statements.length === 0 ? "" : `${statements.join("\n\n")}\n`,
                snapshot: nextSnapshot,
            });
        }

        const migrations = Object.freeze({
            script: migrationScript,
            createTableSql,
            snapshot: migrationSnapshot,
            diff: migrationDiff,
            hash: migrationHash,
            apply: Migrations.apply,
            status: Migrations.status,
        });

        const orm = Object.freeze({
            from: createQueryBuilder,
            transaction,
            query,
            cursor,
            sql: rawSql,
            and,
            or,
            not,
            op: operation,
            operation,
            migrations,
            stream: Object.freeze({ ndjson }),
            dialects: DIALECTS,
        });

        return Object.freeze({ SloppyOrmConcurrencyError, SloppyOrmError, column, orm, sql: rawSql, relation, table });
    }
    const __sloppyOrmRuntime = createSloppyOrmRuntime(sql, DataMigrations);
    const {
        SloppyOrmConcurrencyError,
        SloppyOrmError,
        column,
        orm,
        relation,
        table,
    } = __sloppyOrmRuntime;
    const __sloppyTestServices = (() => {

    const DEFAULT_POSTGRES_IMAGE = "postgres:17";
    const DEFAULT_SQLSERVER_IMAGE = "mcr.microsoft.com/mssql/server:2022-latest";
    const DEFAULT_SQLSERVER_ODBC_DRIVER = "ODBC Driver 17 for SQL Server";
        const LOCALHOST = "127.0.0.1";
        const POSTGRES_PORT = 5432;
        const SQLSERVER_PORT = 1433;
        const DEFAULT_STARTUP_TIMEOUT_MS = 30000;
        const DEFAULT_SQLSERVER_STARTUP_TIMEOUT_MS = 60000;
        const DEFAULT_STOP_TIMEOUT_MS = 10000;
        const DEFAULT_LOG_TAIL = 120;
        const SECRET_REDACTION = "[REDACTED]";
        const ASYNC_DISPOSE = typeof Symbol === "function" ? Symbol.asyncDispose : undefined;

        function isPlainObject(value) {
            if (value === null || typeof value !== "object" || Array.isArray(value)) {
                return false;
            }
            const prototype = Object.getPrototypeOf(value);
            return prototype === Object.prototype || prototype === null;
        }

        function sleep(ms) {
            return Time.delay(ms);
        }

        function processOutputText(value) {
            if (value === undefined || value === null) {
                return "";
            }
            if (typeof value === "string") {
                return value;
            }
            if (value instanceof Uint8Array) {
                return Text.utf8.decode(value);
            }
            return String(value);
        }

        function boundedText(value, max = 12000) {
            const text = String(value ?? "");
            if (text.length <= max) {
                return text;
            }
            return text.slice(text.length - max);
        }

        function normalizeTimeout(value, fallback, subject) {
            if (value === undefined) {
                return fallback;
            }
            if (!Number.isFinite(value) || value < 1) {
                throw new TypeError(`Sloppy TestServices ${subject} must be a positive finite number.`);
            }
            return Math.ceil(value);
        }

        function normalizePort(value, fallback, subject) {
            if (value === undefined) {
                return fallback;
            }
            if (!Number.isInteger(value) || value < 1 || value > 65535) {
                throw new TypeError(`Sloppy TestServices ${subject} must be an integer from 1 to 65535.`);
            }
            return value;
        }

        function normalizeNonEmptyString(value, fallback, subject) {
            const selected = value ?? fallback;
            if (typeof selected !== "string" || selected.length === 0 || selected.includes("\0")) {
                throw new TypeError(`Sloppy TestServices ${subject} must be a non-empty string without NUL.`);
            }
            return selected;
        }

        function randomHex(length) {
            return Array.from(Random.bytes(length), (byte) => byte.toString(16).padStart(2, "0")).join("");
        }

        function randomContainerName(kind) {
            return `sloppy-testservices-${kind}-${randomHex(6)}`;
        }

        function generatedSqlServerPassword() {
            return `Sloppy_${randomHex(10)}_Aa1!`;
        }

        function redactPostgresTestServiceConnectionString(value) {
            return String(value ?? "")
                .replace(
                    /(^|[\s&])(password=)(?:'(?:\\.|[^'])*'|"(?:\\.|[^"])*"|[^\s&]*)/gi,
                    (_match, prefix, key) => `${prefix}${key}<redacted>`,
                )
                .replace(/(postgres(?:ql)?:\/\/[^:\s/@]+:)[^@\s/]+(@)/gi, "$1<redacted>$2");
        }

        function redactSqlServerTestServiceConnectionString(value) {
            return String(value ?? "").replace(
                /(^|;)(\s*)(password|pwd|access token|accesstoken)(\s*)=(\s*)({(?:}}|[^}])*}|[^;]*)/gi,
                (_match, prefix, leading, key, beforeEquals, afterEquals) =>
                    `${prefix}${leading}${key}${beforeEquals}=${afterEquals}<redacted>`,
            );
        }

        function redactWithSecrets(value, secrets) {
            let text = String(value ?? "");
            for (const secret of secrets) {
                if (typeof secret === "string" && secret.length > 0) {
                    text = text.replaceAll(secret, SECRET_REDACTION);
                }
            }
            text = redactPostgresTestServiceConnectionString(text);
            text = redactSqlServerTestServiceConnectionString(text);
            return text;
        }

        function safeObject(value, secrets) {
            if (value === null || value === undefined) {
                return value;
            }
            if (typeof value === "string") {
                return redactWithSecrets(value, secrets);
            }
            if (typeof value === "number" || typeof value === "boolean") {
                return value;
            }
            if (Array.isArray(value)) {
                return Object.freeze(value.map((entry) => safeObject(entry, secrets)));
            }
            if (isPlainObject(value)) {
                const safe = {};
                for (const [key, entryValue] of Object.entries(value)) {
                    if (/password|secret|token|connectionstring|connectionString|pwd/iu.test(key)) {
                        safe[key] = SECRET_REDACTION;
                    } else {
                        safe[key] = safeObject(entryValue, secrets);
                    }
                }
                return Object.freeze(safe);
            }
            return redactWithSecrets(value, secrets);
        }

        function createDiagnosticState(kind, image, name, secrets) {
            return {
                kind,
                image,
                name,
                containerId: undefined,
                host: LOCALHOST,
                port: undefined,
                startupState: "created",
                readinessAttempts: 0,
                lastReadinessError: undefined,
                logTail: "",
                timings: {
                    createdAt: new Date().toISOString(),
                    startedAt: undefined,
                    readyAt: undefined,
                    disposedAt: undefined,
                },
                cleanupErrors: [],
                secrets,
            };
        }

        function dockerUnavailableError(reason) {
            const error = new Error(`SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE: Docker is unavailable for Sloppy TestServices.

        Reason:
          ${reason}

        Fix:
          Start Docker Desktop or a compatible Docker daemon, ensure the docker CLI is on PATH, then rerun the opt-in TestServices lane.`);
            error.code = "SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE";
            return error;
        }

        function providerUnavailableError(kind) {
            const provider = kind === "postgres" ? "PostgreSQL" : "SQL Server";
            const error = new Error(`SLOPPY_E_TESTSERVICES_PROVIDER_UNAVAILABLE: ${provider} TestServices require the matching Sloppy data provider bridge.

        Provider:
          ${kind}

        Reason:
          The active runtime does not expose the native ${kind} provider bridge, so TestServices cannot prove real database readiness.

        Fix:
          Run this lane under a V8/native-provider build, or skip the TestServices container lane with this reason.`);
            error.code = "SLOPPY_E_TESTSERVICES_PROVIDER_UNAVAILABLE";
            return error;
        }

        const TestServicesProcess = Process;

        class DockerCliBackend {
            constructor(options = {}) {
                this.command = options.command ?? "docker";
                this.cwd = options.cwd;
                this.env = options.env;
            }

            async run(args, options = {}) {
                const result = await TestServicesProcess.run(this.command, args, {
                    cwd: options.cwd ?? this.cwd,
                    env: options.env ?? this.env,
                    capture: "bytes",
                    timeoutMs: options.timeoutMs ?? 30000,
                    maxStdoutBytes: options.maxStdoutBytes ?? 1024 * 1024,
                    maxStderrBytes: options.maxStderrBytes ?? 1024 * 1024,
                });
                return Object.freeze({
                    exitCode: result.exitCode,
                    stdout: processOutputText(result.stdout),
                    stderr: processOutputText(result.stderr),
                    timedOut: result.timedOut === true,
                });
            }
        }

        function dockerBackend(options = undefined) {
            if (options?.dockerBackend !== undefined) {
                return options.dockerBackend;
            }
            return new DockerCliBackend(options?.docker);
        }

        async function dockerRunOk(backend, args, options = {}) {
            const result = await backend.run(args, options);
            if (result.exitCode !== 0 || result.timedOut === true) {
                const stderr = boundedText(result.stderr || result.stdout);
                throw new Error(`docker ${args[0]} failed with exit code ${result.exitCode}.${stderr.length === 0 ? "" : `\n${stderr}`}`);
            }
            return result;
        }

        async function dockerAvailable(options = {}) {
            const backend = dockerBackend(options);
            try {
                const result = await backend.run(["version", "--format", "{{json .}}"], {
                    timeoutMs: options.timeoutMs ?? 5000,
                    maxStdoutBytes: 64 * 1024,
                    maxStderrBytes: 64 * 1024,
                });
                if (result.exitCode !== 0 || result.timedOut === true) {
                    const reason = result.timedOut === true
                        ? "docker version timed out"
                        : boundedText(result.stderr || result.stdout || "docker version failed", 1000);
                    return Object.freeze({ ok: false, available: false, reason });
                }
                let version = undefined;
                try {
                    version = JSON.parse(result.stdout);
                } catch {
                    version = result.stdout.trim();
                }
                return Object.freeze({ ok: true, available: true, reason: undefined, version });
            } catch (error) {
                return Object.freeze({
                    ok: false,
                    available: false,
                    reason: String(error?.message ?? error),
                });
            }
        }

        async function dockerRequire(options = {}) {
            const available = await dockerAvailable(options);
            if (available.ok) {
                return available;
            }
            throw dockerUnavailableError(available.reason);
        }

        async function ensureImage(backend, image, options) {
            const inspect = await backend.run(["image", "inspect", image], {
                timeoutMs: options.dockerTimeoutMs ?? 15000,
                maxStdoutBytes: 64 * 1024,
                maxStderrBytes: 64 * 1024,
            });
            if (inspect.exitCode === 0) {
                return;
            }
            await dockerRunOk(backend, ["pull", image], {
                timeoutMs: options.pullTimeoutMs ?? 120000,
                maxStdoutBytes: 256 * 1024,
                maxStderrBytes: 256 * 1024,
            });
        }

        function parseInspectJson(text) {
            const parsed = JSON.parse(text);
            if (!Array.isArray(parsed) || parsed.length === 0 || parsed[0] === null) {
                throw new Error("docker inspect returned no container metadata.");
            }
            return parsed[0];
        }

        function mappedPortFromInspect(metadata, internalPort) {
            const ports = metadata?.NetworkSettings?.Ports;
            const entries = ports?.[`${internalPort}/tcp`];
            if (!Array.isArray(entries) || entries.length === 0) {
                throw new Error(`docker inspect did not report a mapped host port for ${internalPort}/tcp.`);
            }
            const hostPort = Number(entries[0].HostPort);
            if (!Number.isInteger(hostPort) || hostPort < 1 || hostPort > 65535) {
                throw new Error(`docker inspect returned an invalid host port for ${internalPort}/tcp.`);
            }
            return hostPort;
        }

        async function inspectContainer(backend, containerId, internalPort, options) {
            const result = await dockerRunOk(backend, ["inspect", containerId], {
                timeoutMs: options.dockerTimeoutMs ?? 15000,
                maxStdoutBytes: 256 * 1024,
                maxStderrBytes: 64 * 1024,
            });
            const metadata = parseInspectJson(result.stdout);
            return { metadata, port: mappedPortFromInspect(metadata, internalPort) };
        }

        async function dockerLogs(backend, containerId, tail, options) {
            if (containerId === undefined) {
                return "";
            }
            try {
                const result = await backend.run(["logs", "--tail", String(tail), containerId], {
                    timeoutMs: options.dockerTimeoutMs ?? 15000,
                    maxStdoutBytes: 256 * 1024,
                    maxStderrBytes: 256 * 1024,
                });
                return boundedText(`${result.stdout}${result.stderr}`);
            } catch {
                return "";
            }
        }

        function cleanupFailure(operation, args, resultOrError, secrets) {
            const details = resultOrError instanceof Error
                ? resultOrError.message
                : `${resultOrError.timedOut === true ? "timed out" : `exit code ${resultOrError.exitCode}`}: ${resultOrError.stderr || resultOrError.stdout}`;
            return Object.freeze({
                operation,
                command: `docker ${args.join(" ")}`,
                message: redactWithSecrets(boundedText(details, 2000), secrets),
            });
        }

        async function cleanupDockerCommand(backend, args, options, state, required) {
            try {
                const result = await backend.run(args, options);
                if (result.exitCode === 0 && result.timedOut !== true) {
                    return;
                }
                const failure = cleanupFailure(args[0], args, result, state.secrets);
                state.cleanupErrors.push(failure);
                if (required) {
                    throw new Error(`SLOPPY_E_TESTSERVICES_CLEANUP_FAILED: ${failure.message}`);
                }
            } catch (error) {
                if (String(error?.message ?? error).startsWith("SLOPPY_E_TESTSERVICES_CLEANUP_FAILED:")) {
                    throw error;
                }
                const failure = cleanupFailure(args[0], args, error, state.secrets);
                state.cleanupErrors.push(failure);
                if (required) {
                    throw new Error(`SLOPPY_E_TESTSERVICES_CLEANUP_FAILED: ${failure.message}`, { cause: error });
                }
            }
        }

        async function removeContainer(backend, containerId, options, state) {
            if (containerId === undefined) {
                return;
            }
            await cleanupDockerCommand(backend, ["stop", "--time", String(Math.ceil((options.stopTimeoutMs ?? DEFAULT_STOP_TIMEOUT_MS) / 1000)), containerId], {
                timeoutMs: options.stopTimeoutMs ?? DEFAULT_STOP_TIMEOUT_MS,
                maxStdoutBytes: 64 * 1024,
                maxStderrBytes: 64 * 1024,
            }, state, false);
            await cleanupDockerCommand(backend, ["rm", "--force", containerId], {
                timeoutMs: options.rmTimeoutMs ?? 15000,
                maxStdoutBytes: 64 * 1024,
                maxStderrBytes: 64 * 1024,
            }, state, options.keepContainerOnFailure !== true);
        }

        function postgresConnectionString(options, host, port) {
            const user = encodeURIComponent(options.username);
            const password = encodeURIComponent(options.password);
            const database = encodeURIComponent(options.database);
            return `postgresql://${user}:${password}@${host}:${port}/${database}`;
        }

        function odbcEscapeValue(value) {
            const text = String(value);
            const escaped = text.replaceAll("}", "}}");
            if (/[;{}]/u.test(text) || /^\s|\s$/u.test(text)) {
                return "{" + escaped + "}";
            }
            return text;
        }

        function odbcBraceValue(value) {
            return "{" + String(value).replaceAll("}", "}}") + "}";
        }

        function sqlServerConnectionString(options, host, port) {
            return [
                `Driver=${odbcBraceValue(options.driver)}`,
                `Server=${host},${port}`,
                `Database=${odbcEscapeValue(options.database)}`,
                `UID=${odbcEscapeValue(options.username)}`,
                `PWD=${odbcEscapeValue(options.password)}`,
                "Encrypt=yes",
                "TrustServerCertificate=yes",
            ].join(";");
        }

        function normalizedPostgresOptions(options = {}) {
            if (!isPlainObject(options)) {
                throw new TypeError("Sloppy TestServices.postgres options must be a plain object.");
            }
            const username = normalizeNonEmptyString(options.username, "sloppy", "PostgreSQL username");
            const password = normalizeNonEmptyString(options.password, "sloppy", "PostgreSQL password");
            const database = normalizeNonEmptyString(options.database, "app_test", "PostgreSQL database");
            const image = normalizeNonEmptyString(options.image, DEFAULT_POSTGRES_IMAGE, "PostgreSQL image");
            const hostPort = normalizePort(options.hostPort, undefined, "PostgreSQL hostPort");
            const startupTimeoutMs = normalizeTimeout(options.startupTimeoutMs, DEFAULT_STARTUP_TIMEOUT_MS, "PostgreSQL startupTimeoutMs");
            const stopTimeoutMs = normalizeTimeout(options.stopTimeoutMs, DEFAULT_STOP_TIMEOUT_MS, "PostgreSQL stopTimeoutMs");
            const dockerTimeoutMs = normalizeTimeout(options.dockerTimeoutMs, undefined, "PostgreSQL dockerTimeoutMs");
            const pullTimeoutMs = normalizeTimeout(options.pullTimeoutMs, undefined, "PostgreSQL pullTimeoutMs");
            const rmTimeoutMs = normalizeTimeout(options.rmTimeoutMs, undefined, "PostgreSQL rmTimeoutMs");
            const name = options.containerName === undefined
                ? randomContainerName("postgres")
                : normalizeNonEmptyString(options.containerName, undefined, "PostgreSQL containerName");
            return Object.freeze({
                kind: "postgres",
                image,
                username,
                password,
                database,
                host: LOCALHOST,
                containerName: name,
                hostPort,
                startupTimeoutMs,
                stopTimeoutMs,
                dockerTimeoutMs,
                pullTimeoutMs,
                rmTimeoutMs,
                keepContainerOnFailure: options.keepContainerOnFailure === true,
                migrations: options.migrations,
                dockerBackend: options.dockerBackend,
                docker: options.docker,
            });
        }

        function normalizedSqlServerOptions(options = {}) {
            if (!isPlainObject(options)) {
                throw new TypeError("Sloppy TestServices.sqlServer options must be a plain object.");
            }
            const database = normalizeNonEmptyString(options.database, "app_test", "SQL Server database");
            const driver = normalizeNonEmptyString(options.driver, DEFAULT_SQLSERVER_ODBC_DRIVER, "SQL Server ODBC driver");
            const username = normalizeNonEmptyString(options.username, "sa", "SQL Server username");
            if (username !== "sa") {
                throw new TypeError('Sloppy TestServices SQL Server currently supports only username "sa".');
            }
            const image = normalizeNonEmptyString(options.image, DEFAULT_SQLSERVER_IMAGE, "SQL Server image");
            const hostPort = normalizePort(options.hostPort, undefined, "SQL Server hostPort");
            const startupTimeoutMs = normalizeTimeout(options.startupTimeoutMs, DEFAULT_SQLSERVER_STARTUP_TIMEOUT_MS, "SQL Server startupTimeoutMs");
            const stopTimeoutMs = normalizeTimeout(options.stopTimeoutMs, DEFAULT_STOP_TIMEOUT_MS, "SQL Server stopTimeoutMs");
            const dockerTimeoutMs = normalizeTimeout(options.dockerTimeoutMs, undefined, "SQL Server dockerTimeoutMs");
            const pullTimeoutMs = normalizeTimeout(options.pullTimeoutMs, undefined, "SQL Server pullTimeoutMs");
            const rmTimeoutMs = normalizeTimeout(options.rmTimeoutMs, undefined, "SQL Server rmTimeoutMs");
            const password = options.password === undefined
                ? generatedSqlServerPassword()
                : normalizeNonEmptyString(options.password, undefined, "SQL Server password");
            const name = options.containerName === undefined
                ? randomContainerName("sqlserver")
                : normalizeNonEmptyString(options.containerName, undefined, "SQL Server containerName");
            return Object.freeze({
                kind: "sqlserver",
                image,
                username,
                password,
                database,
                driver,
                host: LOCALHOST,
                containerName: name,
                hostPort,
                startupTimeoutMs,
                stopTimeoutMs,
                dockerTimeoutMs,
                pullTimeoutMs,
                rmTimeoutMs,
                keepContainerOnFailure: options.keepContainerOnFailure === true,
                migrations: options.migrations,
                dockerBackend: options.dockerBackend,
                docker: options.docker,
            });
        }

        function providerBridgeAvailable(kind) {
            return kind === "postgres"
                ? globalThis.__sloppy?.data?.postgres !== undefined
                : globalThis.__sloppy?.data?.sqlserver !== undefined;
        }

        function openProvider(kind, connectionString) {
            if (!providerBridgeAvailable(kind)) {
                throw providerUnavailableError(kind);
            }
            return kind === "postgres"
                ? data.postgres.open({ connectionString })
                : data.sqlserver.open({ connectionString });
        }

        async function withDb(kind, connectionString, callback) {
            const db = openProvider(kind, connectionString);
            try {
                return await callback(db);
            } finally {
                await Promise.resolve(db.close?.()).catch(() => {});
            }
        }

        async function waitForReady(kind, state, connectionString, options) {
            const startedAt = Date.now();
            state.startupState = "waiting";
            while (Date.now() - startedAt < options.startupTimeoutMs) {
                const remainingMs = options.startupTimeoutMs - (Date.now() - startedAt);
                state.readinessAttempts += 1;
                try {
                    if (kind === "sqlserver") {
                        const masterConnectionString = sqlServerConnectionString({
                            ...options,
                            database: "master",
                        }, LOCALHOST, state.port);
                        await withDb(kind, masterConnectionString, async (db) => {
                            await db.exec(`if db_id(N'${options.database.replaceAll("'", "''")}') is null create database [${options.database.replaceAll("]", "]]")}]`, [], { timeoutMs: remainingMs });
                        });
                    }
                    await withDb(kind, connectionString, async (db) => {
                        await db.queryOne("select 1 as ok", [], { timeoutMs: remainingMs });
                    });
                    state.startupState = "ready";
                    state.timings.readyAt = new Date().toISOString();
                    return;
                } catch (error) {
                    state.lastReadinessError = String(error?.message ?? error);
                    const retryDelayMs = Math.min(1000, 100 + state.readinessAttempts * 100, Math.max(0, options.startupTimeoutMs - (Date.now() - startedAt)));
                    if (retryDelayMs > 0) {
                        await sleep(retryDelayMs);
                    }
                }
            }
            throw new Error(`readiness timed out after ${options.startupTimeoutMs}ms: ${state.lastReadinessError ?? "no readiness result"}`);
        }

        function containerCreateArgs(kind, options) {
            const port = kind === "postgres" ? POSTGRES_PORT : SQLSERVER_PORT;
            const publish = options.hostPort === undefined
                ? `${LOCALHOST}::${port}`
                : `${LOCALHOST}:${options.hostPort}:${port}`;
            if (kind === "postgres") {
                return [
                    "create",
                    "--name",
                    options.containerName,
                    "-e",
                    `POSTGRES_USER=${options.username}`,
                    "-e",
                    `POSTGRES_PASSWORD=${options.password}`,
                    "-e",
                    `POSTGRES_DB=${options.database}`,
                    "-p",
                    publish,
                    options.image,
                ];
            }
            return [
                "create",
                "--name",
                options.containerName,
                "-e",
                "ACCEPT_EULA=Y",
                "-e",
                `MSSQL_SA_PASSWORD=${options.password}`,
                "-e",
                "MSSQL_PID=Developer",
                "-p",
                publish,
                options.image,
            ];
        }

        function providerPlaceholder(kind) {
            return kind === "postgres" ? "postgres" : "named";
        }

        function envWithPrefix(entries, prefix) {
            if (prefix === undefined || prefix === "") {
                return Object.freeze(entries);
            }
            const normalizedPrefix = String(prefix).replace(/_$/u, "");
            return Object.freeze(Object.fromEntries(
                Object.entries(entries).map(([key, value]) => [`${normalizedPrefix}_${key}`, value]),
            ));
        }

        function normalizeMigrationList(pathOrGlob) {
            if (typeof pathOrGlob === "string") {
                if (pathOrGlob.length === 0 || pathOrGlob.includes("\0")) {
                    throw new TypeError("Sloppy TestServices migration path must be a non-empty string without NUL.");
                }
                return [pathOrGlob];
            }
            if (!Array.isArray(pathOrGlob) || pathOrGlob.length === 0) {
                throw new TypeError("Sloppy TestServices migrate expects a path string or non-empty path array.");
            }
            return pathOrGlob.map((entry) => {
                if (typeof entry !== "string" || entry.length === 0 || entry.includes("\0")) {
                    throw new TypeError("Sloppy TestServices migration paths must be non-empty strings without NUL.");
                }
                return entry;
            });
        }

        function isSqlGlob(path) {
            return /(?:^|[\\/])\*\.sql$/u.test(path);
        }

        async function applyMigrationPath(db, kind, path) {
            if (isSqlGlob(path)) {
                await DataMigrations.apply(db, { provider: kind, path });
                return;
            }
            if (!path.endsWith(".sql")) {
                throw new Error(`SLOPPY_E_TESTSERVICES_MIGRATION_PATH: migration path must be a .sql file or directory glob ending in *.sql.

        Path:
          ${path}`);
            }
            let sqlText;
            try {
                sqlText = await File.readText(path);
            } catch (error) {
                throw new Error(`SLOPPY_E_TESTSERVICES_MIGRATION_MISSING: migration file is missing or unreadable.

        Migration:
          ${path}`, { cause: error });
            }
            try {
                await db.exec(sqlText, []);
            } catch (error) {
                throw new Error(`SLOPPY_E_TESTSERVICES_MIGRATION_FAILED: migration failed.

        Migration:
          ${path}

        Reason:
          ${String(error?.message ?? error)}`, { cause: error });
            }
        }

        function resetSql(kind) {
            if (kind === "postgres") {
                return Object.freeze([
                    "drop schema if exists public cascade",
                    "create schema public",
                ]);
            }
            throw new Error("SQL Server reset uses database recreation from master.");
        }

        function sqlServerIdentifier(value) {
            return `[${String(value).replaceAll("]", "]]")}]`;
        }

        function sqlServerStringLiteral(value) {
            return `N'${String(value).replaceAll("'", "''")}'`;
        }

        function resetSqlServerDatabaseSql(database) {
            const name = sqlServerIdentifier(database);
            const literal = sqlServerStringLiteral(database);
            return `
        if db_id(${literal}) is not null
        begin
            alter database ${name} set single_user with rollback immediate;
            drop database ${name};
        end;
        create database ${name};`;
        }

        async function resetSqlServerDatabase(options, port) {
            const masterConnectionString = sqlServerConnectionString({
                ...options,
                database: "master",
            }, LOCALHOST, port);
            await withDb("sqlserver", masterConnectionString, (db) =>
                db.exec(resetSqlServerDatabaseSql(options.database), []));
        }

        function createService(kind, options, backend, state, connectionString, port) {
            const ownedProviders = new Set();
            const migrations = [];
            let disposed = false;
            state.port = port;

            const service = {
                kind,
                id: state.containerId,
                image: options.image,
                host: LOCALHOST,
                port,
                connectionString,
                async start() {
                    return service;
                },
                async stop() {
                    await service.dispose();
                },
                async dispose() {
                    if (disposed) {
                        return;
                    }
                    state.startupState = "disposing";
                    for (const provider of ownedProviders) {
                        await Promise.resolve(provider.close?.()).catch(() => {});
                    }
                    ownedProviders.clear();
                    await removeContainer(backend, state.containerId, options, state);
                    disposed = true;
                    state.startupState = "disposed";
                    state.timings.disposedAt = new Date().toISOString();
                },
                exec(sql, params = []) {
                    if (typeof sql !== "string" || sql.length === 0) {
                        throw new TypeError("Sloppy TestServices exec SQL must be a non-empty string.");
                    }
                    if (!Array.isArray(params)) {
                        throw new TypeError("Sloppy TestServices exec params must be an array.");
                    }
                    return withDb(kind, connectionString, (db) => db.exec(sql, params));
                },
                async migrate(pathOrGlob) {
                    const paths = normalizeMigrationList(pathOrGlob);
                    await withDb(kind, connectionString, async (db) => {
                        for (const path of paths) {
                            await applyMigrationPath(db, kind, path);
                            migrations.push(path);
                        }
                    });
                },
                async seed(fn) {
                    if (typeof fn !== "function") {
                        throw new TypeError("Sloppy TestServices seed callback must be a function.");
                    }
                    await withDb(kind, connectionString, (db) => fn(db));
                },
                async reset(resetOptions = {}) {
                    if (!isPlainObject(resetOptions)) {
                        throw new TypeError("Sloppy TestServices reset options must be a plain object.");
                    }
                    const rerun = resetOptions.rerunMigrations === true || resetOptions.migrate === true;
                    const selectedMigrations = resetOptions.migrations === undefined
                        ? migrations
                        : normalizeMigrationList(resetOptions.migrations);
                    if (kind === "sqlserver") {
                        await resetSqlServerDatabase(options, port);
                        if (rerun) {
                            await withDb(kind, connectionString, async (db) => {
                                for (const path of selectedMigrations) {
                                    await applyMigrationPath(db, kind, path);
                                }
                            });
                        }
                        return;
                    }
                    await withDb(kind, connectionString, async (db) => {
                        for (const sqlText of resetSql(kind)) {
                            await db.exec(sqlText, []);
                        }
                        if (rerun) {
                            for (const path of selectedMigrations) {
                                await applyMigrationPath(db, kind, path);
                            }
                        }
                    });
                },
                provider() {
                    const provider = openProvider(kind, connectionString);
                    ownedProviders.add(provider);
                    return provider;
                },
                env(prefix = undefined) {
                    if (kind === "postgres") {
                        return envWithPrefix({
                            POSTGRES_HOST: LOCALHOST,
                            POSTGRES_PORT: String(port),
                            POSTGRES_USER: options.username,
                            POSTGRES_PASSWORD: options.password,
                            POSTGRES_DB: options.database,
                            DATABASE_URL: connectionString,
                        }, prefix);
                    }
                    return envWithPrefix({
                        SQLSERVER_HOST: LOCALHOST,
                        SQLSERVER_PORT: String(port),
                        SQLSERVER_USER: options.username,
                        SQLSERVER_PASSWORD: options.password,
                        SQLSERVER_DATABASE: options.database,
                        SQLSERVER_DRIVER: options.driver,
                        SQLSERVER_CONNECTION_STRING: connectionString,
                    }, prefix);
                },
                async logs(logOptions = {}) {
                    const tail = normalizePort(logOptions.tail, DEFAULT_LOG_TAIL, "logs tail");
                    const logs = await dockerLogs(backend, state.containerId, tail, options);
                    state.logTail = redactWithSecrets(logs, state.secrets);
                    return state.logTail;
                },
                diagnostics() {
                    return safeObject({
                        kind,
                        image: options.image,
                        containerId: state.containerId?.slice(0, 12),
                        containerName: options.containerName,
                        host: LOCALHOST,
                        port,
                        startupState: state.startupState,
                        readinessAttempts: state.readinessAttempts,
                        lastReadinessError: state.lastReadinessError,
                        cleanupErrors: state.cleanupErrors,
                        logTail: state.logTail,
                        timings: state.timings,
                        provider: {
                            kind,
                            placeholderStyle: providerPlaceholder(kind),
                            nativeStdlibBridge: providerBridgeAvailable(kind),
                        },
                    }, state.secrets);
                },
            };
            if (ASYNC_DISPOSE !== undefined) {
                service[ASYNC_DISPOSE] = service.dispose;
            }
            return Object.freeze(service);
        }

        function startupFailureMessage(kind, options, state, reason) {
            const provider = kind === "postgres" ? "PostgreSQL" : "SQL Server";
            const logTail = redactWithSecrets(state.logTail, state.secrets);
            const lastReadinessError = redactWithSecrets(state.lastReadinessError ?? reason, state.secrets);
            return `SLOPPY_E_TESTSERVICES_STARTUP_FAILED: ${provider} TestServices container did not become ready.

        Image:
          ${options.image}

        Container:
          ${state.containerId === undefined ? options.containerName : `${options.containerName} (${state.containerId.slice(0, 12)})`}

        Mapped port:
          ${state.port ?? "unknown"}

        Reason:
          ${lastReadinessError}

        Cleanup failures:
        ${state.cleanupErrors.length === 0 ? "  <none>" : state.cleanupErrors.map((entry) => `  - ${entry.operation}: ${entry.message}`).join("\n")}

        Docker logs tail:
        ${logTail.length === 0 ? "  <empty>" : logTail}

        Suggested checks:
          - Docker is running.
          - The image can be pulled.
          - The mapped port is not blocked.
          - SQL Server startup can be slow on cold machines.`;
        }

        async function startService(kind, rawOptions) {
            const options = kind === "postgres"
                ? normalizedPostgresOptions(rawOptions)
                : normalizedSqlServerOptions(rawOptions);
            const backend = dockerBackend(options);
            const state = createDiagnosticState(kind, options.image, options.containerName, [options.password]);
            let connectionString = undefined;
            try {
                await dockerRequire({ dockerBackend: backend });
                if (!providerBridgeAvailable(kind)) {
                    throw providerUnavailableError(kind);
                }
                await ensureImage(backend, options.image, options);
                const create = await dockerRunOk(backend, containerCreateArgs(kind, options), {
                    timeoutMs: options.dockerTimeoutMs ?? 30000,
                    maxStdoutBytes: 64 * 1024,
                    maxStderrBytes: 64 * 1024,
                });
                state.containerId = create.stdout.trim();
                state.startupState = "starting";
                state.timings.startedAt = new Date().toISOString();
                await dockerRunOk(backend, ["start", state.containerId], {
                    timeoutMs: options.dockerTimeoutMs ?? 30000,
                    maxStdoutBytes: 64 * 1024,
                    maxStderrBytes: 64 * 1024,
                });
                const inspected = await inspectContainer(
                    backend,
                    state.containerId,
                    kind === "postgres" ? POSTGRES_PORT : SQLSERVER_PORT,
                    options,
                );
                state.port = inspected.port;
                connectionString = kind === "postgres"
                    ? postgresConnectionString(options, LOCALHOST, inspected.port)
                    : sqlServerConnectionString(options, LOCALHOST, inspected.port);
                await waitForReady(kind, state, connectionString, options);
                if (options.migrations !== undefined) {
                    const pendingService = createService(kind, options, backend, state, connectionString, inspected.port);
                    await pendingService.migrate(options.migrations);
                    return pendingService;
                }
                return createService(kind, options, backend, state, connectionString, inspected.port);
            } catch (error) {
                state.startupState = "failed";
                state.lastReadinessError = String(error?.message ?? error);
                state.logTail = await dockerLogs(backend, state.containerId, DEFAULT_LOG_TAIL, options);
                if (!options.keepContainerOnFailure) {
                    await removeContainer(backend, state.containerId, options, state).catch(() => {});
                }
                const startupError = error?.code === "SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE" ||
                    error?.code === "SLOPPY_E_TESTSERVICES_PROVIDER_UNAVAILABLE"
                    ? error
                    : new Error(startupFailureMessage(kind, options, state, error?.message ?? error), { cause: error });
                throw startupError;
            }
        }

        const TestServices = Object.freeze({
            docker: Object.freeze({
                available: dockerAvailable,
                require: dockerRequire,
            }),
            postgres(options = {}) {
                return startService("postgres", options);
            },
            sqlServer(options = {}) {
                return startService("sqlserver", options);
            },
        });


        return Object.freeze({ DockerCliBackend, TestServices });
    })();
    const DockerCliBackend = __sloppyTestServices.DockerCliBackend;
    const TestServices = __sloppyTestServices.TestServices;
    class SloppyWebhookError extends Error {
        constructor(code, message, options = undefined) {
            super(`${code}: ${message}`);
            this.name = "SloppyWebhookError";
            this.code = code;
            if (options?.cause !== undefined) {
                this.cause = options.cause;
            }
        }
    }
    function __sloppyWebhookError(code, message, options = undefined) {
        return new SloppyWebhookError(code, message, options);
    }
    function __sloppyWebhookEvent(name, options) {
        if (typeof name !== "string" || !/^[a-z][a-z0-9]*(?:\.[a-z][a-z0-9]*)+$/u.test(name)) {
            throw new TypeError("Sloppy Webhooks event name must be a stable dotted identifier.");
        }
        if (!options || typeof options !== "object" || !Number.isInteger(options.version) || options.version <= 0) {
            throw new TypeError("Sloppy Webhooks.event requires a positive integer version.");
        }
        if (!options.schema || typeof options.schema.validate !== "function") {
            throw new TypeError("Sloppy Webhooks event schema must be a Sloppy schema.");
        }
        const descriptor = {
            __sloppyWebhookEvent: true,
            name,
            version: options.version,
            schema: options.schema,
            validate(payload) {
                const result = options.schema.validate(payload);
                if (!result.ok) {
                    throw __sloppyWebhookError("SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED", `Webhook event '${name}' payload failed validation.`);
                }
                return result.value;
            },
        };
        return Object.freeze(descriptor);
    }
    function __sloppyWebhookRetryPositiveInteger(value, fallback, subject) {
        const resolved = value ?? fallback;
        if (!Number.isInteger(resolved) || resolved < 1) {
            throw new TypeError(`Sloppy Webhooks ${subject} must be an integer greater than or equal to 1.`);
        }
        return resolved;
    }
    function __sloppyWebhookRetryNonNegativeInteger(value, fallback, subject) {
        const resolved = value ?? fallback;
        if (!Number.isInteger(resolved) || resolved < 0) {
            throw new TypeError(`Sloppy Webhooks ${subject} must be a non-negative integer.`);
        }
        return resolved;
    }
    function __sloppyWebhookRetryStatusCodes(value) {
        const codes = value ?? [408, 425, 429, 500, 502, 503, 504];
        if (!Array.isArray(codes) || codes.length === 0 ||
            !codes.every((code) => Number.isInteger(code) && code >= 100 && code <= 599)) {
            throw new TypeError("Sloppy Webhooks retryOnStatus must be a non-empty array of HTTP status codes.");
        }
        return Object.freeze([...codes]);
    }
    function __sloppyWebhookRetryJitter(value) {
        if (value !== undefined && typeof value !== "boolean") {
            throw new TypeError("Sloppy Webhooks retry jitter must be a boolean when provided.");
        }
        return value !== false;
    }
    function __sloppyWebhookRetryExponential(options = {}) {
        const maxAttempts = __sloppyWebhookRetryPositiveInteger(options.maxAttempts, 8, "exponential retry maxAttempts");
        const initialDelayMs = __sloppyWebhookRetryNonNegativeInteger(options.initialDelayMs, 1000, "exponential retry initialDelayMs");
        const maxDelayMs = __sloppyWebhookRetryNonNegativeInteger(options.maxDelayMs, 300000, "exponential retry maxDelayMs");
        if (maxDelayMs < initialDelayMs) {
            throw new TypeError("Sloppy Webhooks exponential retry maxDelayMs must be at least initialDelayMs.");
        }
        return Object.freeze({
            kind: "exponential",
            maxAttempts,
            initialDelayMs,
            maxDelayMs,
            retryOnStatus: __sloppyWebhookRetryStatusCodes(options.retryOnStatus),
            jitter: __sloppyWebhookRetryJitter(options.jitter),
        });
    }
    function __sloppyWebhookRetryFixed(options = {}) {
        const maxAttempts = __sloppyWebhookRetryPositiveInteger(options.maxAttempts, 3, "fixed retry maxAttempts");
        const delayMs = __sloppyWebhookRetryNonNegativeInteger(options.delayMs, 1000, "fixed retry delayMs");
        return Object.freeze({
            kind: "fixed",
            maxAttempts,
            delayMs,
            retryOnStatus: __sloppyWebhookRetryStatusCodes(options.retryOnStatus),
            jitter: __sloppyWebhookRetryJitter(options.jitter),
        });
    }
    async function __sloppyWebhookSign(payload, options) {
        let body;
        if (typeof payload === "string") {
            body = payload;
        } else {
            try {
                body = JSON.stringify(payload);
            } catch (cause) {
                throw __sloppyWebhookError(
                    "SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED",
                    "Webhook signing payload must be JSON-serializable.",
                    { cause },
                );
            }
            if (typeof body !== "string") {
                throw __sloppyWebhookError(
                    "SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED",
                    "Webhook signing payload must be a string or JSON-serializable value.",
                );
            }
        }
        const timestamp = String(options?.timestamp ?? Math.floor(Date.now() / 1000));
        const id = options?.id ?? `whdel_${Random.uuid()}`;
        const eventName = options?.event ?? options?.eventName;
        let ownedSecret;
        const secret = typeof options?.secret === "string" ? (ownedSecret = Secret.fromUtf8(options.secret)) : options?.secret;
        if (secret === undefined) {
            throw __sloppyWebhookError("SLOPPY_E_WEBHOOK_SECRET_UNAVAILABLE", "Webhook signing secret is required.");
        }
        try {
            const digest = await Hmac.sha256(secret, `${timestamp}.${body}`);
            const signature = `v1=${Hex.encode(digest)}`;
            return Object.freeze({
                id,
                event: eventName,
                timestamp,
                attempt: options?.attempt ?? 1,
                signature,
                headers: Object.freeze({
                    "Sloppy-Webhook-Id": id,
                    "Sloppy-Webhook-Event": eventName,
                    "Sloppy-Webhook-Timestamp": timestamp,
                    "Sloppy-Webhook-Signature": signature,
                    "Sloppy-Webhook-Attempt": String(options?.attempt ?? 1),
                }),
            });
        } finally {
            ownedSecret?.dispose();
        }
    }
    const Webhooks = Object.freeze({
        event: __sloppyWebhookEvent,
        sign: __sloppyWebhookSign,
        outbox(options) {
            return Object.freeze({
                __sloppyWebhooksOutboxRegistration: true,
                token: "webhooks",
                provider: options?.provider ?? "main",
                createService() {
                    throw __sloppyWebhookError("SLOPPY_E_WEBHOOK_OUTBOX_UNAVAILABLE", "Webhook service registration requires the bootstrap module service runtime.");
                },
            });
        },
        token(name = undefined) {
            return name === undefined || name === "" ? "webhooks" : `webhooks.${name}`;
        },
        retry: Object.freeze({
            fixed: __sloppyWebhookRetryFixed,
            exponential: __sloppyWebhookRetryExponential,
        }),
    });
    globalThis.__sloppy_runtime = Object.freeze({
        Results,
        Cache,
        SloppyCacheError,
        Realtime,
        SloppyRealtimeError,
        schema: __sloppyRealtimeSchemaRuntime.schema,
        Schema: __sloppyRealtimeSchemaRuntime.Schema,
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
        sql,
        Migrations: DataMigrations,
        ProviderHealth: DataProviderHealth,
        orm,
        table,
        column,
        relation,
        SloppyOrmError,
        SloppyOrmConcurrencyError,
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
        DockerCliBackend,
        TestServices,
        Webhooks,
        SloppyWebhookError,
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
