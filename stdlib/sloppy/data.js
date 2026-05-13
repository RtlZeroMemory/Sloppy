import { isPlainObject } from "./internal/validation.js";
import { Directory, File } from "./fs.js";

const QUERY_MARKER = "__sloppyQuery";
const DB_VALUE_MARKER = Symbol("sloppyDbValue");
const DB_BRIDGE_VALUE_MARKER = "__sloppyDbValue";
const DB_RESULT_MODES = Object.freeze({
    object: true,
    raw: true,
});
const POSTGRES_MAX_POOL_CONNECTIONS = 256;
const SQLSERVER_MAX_POOL_CONNECTIONS = 256;
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
const REAL_PROVIDER_HANDLES = new WeakMap();
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
const MIGRATIONS_TABLE = "_sloppy_migrations";
const MIGRATION_HASH_PREFIX = "fnv1a32:";
const MIGRATION_PROVIDER_KINDS = Object.freeze({
    sqlite: true,
    postgres: true,
    sqlserver: true,
});

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

function normalizeMigrationOptions(options) {
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

function migrationFsPath(path) {
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

function migrationHash(text) {
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

function migrationProviderKind(db) {
    return connectionProviderKind(db, "Migrations");
}

function resolveMigrationProviderKind(db, options) {
    const providerKind = migrationProviderKind(db);
    if (options.provider !== providerKind) {
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
        entries = await Directory.list(migrationFsPath(options.directory));
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

async function ensureMigrationsTable(db, providerKind) {
    await db.exec(MIGRATION_SQL[providerKind].ensure, []);
}

function isMissingMigrationsTableError(error) {
    const message = String(error?.message ?? error).toLowerCase();
    return message.includes("_sloppy_migrations")
        && (message.includes("no such table")
            || message.includes("does not exist")
            || message.includes("invalid object name")
            || message.includes("undefined_table")
            || message.includes("42p01"));
}

async function readAppliedMigrations(db, providerKind, options = {}) {
    if (options.ensure !== false) {
        await ensureMigrationsTable(db, providerKind);
    }
    let rows;
    try {
        rows = await db.query(MIGRATION_SQL[providerKind].select, []);
    } catch (error) {
        if (options.ensure === false && isMissingMigrationsTableError(error)) {
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
        const sqlText = await File.readText(migrationFsPath(file.path));
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
    const applied = await readAppliedMigrations(db, providerKind, { ensure: false });
    const migrations = migrationStatusFor(files, applied);
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
        let didApply = false;
        try {
            didApply = await db.transaction(async (tx) => {
                const current = await readAppliedMigrations(tx, providerKind);
                const record = migrationStatusFor([file], current)[0];
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
            const current = await readAppliedMigrations(db, providerKind);
            const record = migrationStatusFor([file], current)[0];
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

async function checkProviderHealth(db, options = {}) {
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

function normalizeOperationOptions(
    options,
    operation,
    allowResultMode = false,
    allowMaxRows = false,
    allowCursorOptions = false,
) {
    if (options === undefined) {
        return undefined;
    }
    if (!isPlainObject(options)) {
        throw new TypeError(`Sloppy data ${operation} options must be a plain object.`);
    }
    const allowedKeys = new Set(["deadline", "signal", "timeoutMs"]);
    if (allowResultMode) {
        allowedKeys.add("mode");
    }
    if (allowMaxRows) {
        allowedKeys.add("maxRows");
    }
    if (allowCursorOptions) {
        allowedKeys.add("batchSize");
    }
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

    const maxRows = options.maxRows;
    if (maxRows !== undefined) {
        if (!Number.isInteger(maxRows) || maxRows < 1 || maxRows > 0xffffffff) {
            throw new TypeError(
                `Sloppy data ${operation} maxRows option must be an integer from 1 to 4294967295.`,
            );
        }
    }

    const batchSize = options.batchSize;
    if (batchSize !== undefined) {
        if (!Number.isInteger(batchSize) || batchSize < 1 || batchSize > 4096) {
            throw new TypeError(
                `Sloppy data ${operation} batchSize option must be an integer from 1 to 4096.`,
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

    const mode = allowResultMode ? normalizeResultMode(options.mode, operation) : undefined;
    const normalized = Object.freeze({
        deadline: deadline ?? undefined,
        batchSize,
        maxRows,
        mode: mode === "raw" ? mode : undefined,
        signal: signal ?? undefined,
        timeoutMs,
    });
    return normalized.deadline === undefined
        && normalized.batchSize === undefined
        && normalized.maxRows === undefined
        && normalized.mode === undefined
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
        if (options.signal.aborted === true) {
            throw createOperationCancelledError(operation, options.signal.reason);
        }
        if (typeof options.signal.throwIfAborted === "function") {
            try {
                options.signal.throwIfAborted();
            } catch {
                throw createOperationCancelledError(operation, options.signal.reason);
            }
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

function operationAllowsResultMode(operation) {
    return operation === "query"
        || operation === "queryCursor"
        || operation === "stream"
        || operation.endsWith(".query")
        || operation.endsWith(".queryCursor")
        || operation.endsWith(".transaction.query");
}

function operationAllowsMaxRows(operation) {
    return operation === "query"
        || operation === "queryRaw"
        || operation === "queryCursor"
        || operation === "queryRawCursor"
        || operation === "stream"
        || operation.endsWith(".query")
        || operation.endsWith(".queryRaw")
        || operation.endsWith(".queryCursor")
        || operation.endsWith(".queryRawCursor");
}

function operationAllowsCursorOptions(operation) {
    return operation === "queryCursor"
        || operation === "queryRawCursor"
        || operation === "stream"
        || operation.endsWith(".queryCursor")
        || operation.endsWith(".queryRawCursor");
}

function nativeQueryOptions(options, includeTimeout = false, includeCursorOptions = false) {
    if (
        options?.maxRows === undefined
        && (!includeTimeout || options?.timeoutMs === undefined)
        && (!includeCursorOptions || options?.batchSize === undefined)
    ) {
        return undefined;
    }
    const native = {};
    if (includeCursorOptions && options.batchSize !== undefined) {
        native.batchSize = options.batchSize;
    }
    if (options.maxRows !== undefined) {
        native.maxRows = options.maxRows;
    }
    if (includeTimeout && options.timeoutMs !== undefined) {
        native.timeoutMs = options.timeoutMs;
    }
    return Object.freeze(native);
}

function invokeNativeQuery(method, handle, query, includeTimeout = false) {
    const nativeOptions = nativeQueryOptions(query.options, includeTimeout);
    return nativeOptions === undefined
        ? method(handle, query.text, query.parameters)
        : method(handle, query.text, query.parameters, nativeOptions);
}

function invokeNativeCursorOpen(method, handle, query, includeTimeout = false) {
    if (typeof method !== "function") {
        throw new Error("sloppy: provider cursor bridge is unavailable");
    }
    const nativeOptions = nativeQueryOptions(query.options, includeTimeout, true);
    return nativeOptions === undefined
        ? method(handle, query.text, query.parameters)
        : method(handle, query.text, query.parameters, nativeOptions);
}

function requireCursorBridgeMethod(nativeBridge, method, provider) {
    if (typeof nativeBridge?.[method] !== "function") {
        throw new Error(`sloppy: ${provider} cursor bridge is unavailable`);
    }
    return nativeBridge[method];
}

function createDataCursor(provider, nativeBridge, nativeCursor, mode, operationOptions, registry) {
    if (!isPlainObject(nativeCursor)) {
        throw new TypeError(`Sloppy ${provider} cursor bridge returned an invalid cursor handle.`);
    }

    let closed = nativeCursor.closed === true;
    let started = false;
    let rowsSeen = 0;
    let cursor = null;
    const metadata = Object.freeze({
        columns: Array.isArray(nativeCursor.columns) ? Object.freeze([...nativeCursor.columns]) : Object.freeze([]),
        mode,
        provider,
    });

    async function close() {
        if (closed) {
            registry?.delete(cursor);
            return;
        }
        closed = true;
        registry?.delete(cursor);
        await requireCursorBridgeMethod(nativeBridge, "cursorClose", provider)(nativeCursor);
    }

    async function next() {
        if (closed) {
            throw new Error(`sloppy: ${provider} cursor is closed`);
        }
        throwIfOperationCancelled(operationOptions, `${provider}.cursor.next`);
        started = true;
        let result;
        try {
            result = await requireCursorBridgeMethod(nativeBridge, "cursorNext", provider)(nativeCursor);
        } catch (error) {
            try {
                await close();
            } catch {
                // Preserve the provider error while still making a best-effort cleanup attempt.
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
            return metadata.columns;
        },
        get mode() {
            return metadata.mode;
        },
        get provider() {
            return metadata.provider;
        },
        async close() {
            await close();
        },
        async next() {
            return next();
        },
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
            return Object.freeze({
                kind: "data-cursor",
                closed,
                mode,
                provider,
                rowsSeen,
            });
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

function normalizeResultMode(value, operation) {
    if (value === undefined) {
        return "object";
    }
    if (!Object.prototype.hasOwnProperty.call(DB_RESULT_MODES, value)) {
        throw new TypeError(`Sloppy data ${operation} mode must be object or raw.`);
    }
    return value;
}

function hasInlineOperationOptions(args) {
    return args.length === 2 && isPlainObject(args[1]);
}

function normalizeProviderCallArguments(operation, placeholderStyle, args) {
    const allowResultMode = operationAllowsResultMode(operation);
    const allowMaxRows = operationAllowsMaxRows(operation);
    const allowCursorOptions = operationAllowsCursorOptions(operation);
    if (args.length === 2 && isLoweredQuery(args[0])) {
        const options = normalizeOperationOptions(
            args[1],
            operation,
            allowResultMode,
            allowMaxRows,
            allowCursorOptions,
        );
        return {
            query: args[0],
            options,
            mode: allowResultMode ? options?.mode ?? "object" : undefined,
        };
    }
    if (args.length === 1 && isLoweredQuery(args[0])) {
        return {
            query: args[0],
            options: undefined,
            mode: allowResultMode ? "object" : undefined,
        };
    }
    return {
        query: normalizeQueryArguments(operation, placeholderStyle, args),
        options: undefined,
        mode: allowResultMode ? "object" : undefined,
    };
}

function validateOperationOptions(
    options,
    operation,
    allowResultMode = false,
    allowMaxRows = false,
) {
    const normalized = normalizeOperationOptions(
        options,
        operation,
        allowResultMode,
        allowMaxRows,
    );
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

    for (const method of ["query", "queryRaw", "queryOne", "exec"]) {
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
    if (typeof database !== "string" || database.length === 0 || database.includes("\0")) {
        throw new TypeError("Sloppy sqlite.open database must be a non-empty string without NUL.");
    }

    if (
        typeof options.database === "string"
        && typeof options.path === "string"
        && options.database !== options.path
    ) {
        throw new TypeError("Sloppy sqlite.open database and path must match when both are supplied.");
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
        placeholderStyle: "question",
    });
}

function normalizeSqliteProviderToken(name) {
    if (typeof name !== "string" || name.length === 0 || name.includes("\0")) {
        throw new TypeError("Sloppy data.sqlite provider name must be a non-empty string without NUL.");
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
    const allowResultMode = operation === "query" || operation === "queryCursor";
    const allowMaxRows = operation === "query" || operation === "queryRaw"
        || operation === "queryCursor" || operation === "queryRawCursor";
    const allowCursorOptions = operation === "queryCursor" || operation === "queryRawCursor";
    if (args.length === 1 && isLoweredQuery(args[0])) {
        return {
            text: args[0].text,
            parameters: validateSqliteParams(args[0].parameters, operation),
            options: undefined,
            mode: allowResultMode ? "object" : undefined,
        };
    }
    if (args.length === 2 && isLoweredQuery(args[0])) {
        const options = normalizeOperationOptions(
            args[1],
            `sqlite.${operation}`,
            allowResultMode,
            allowMaxRows,
            allowCursorOptions,
        );
        return {
            text: args[0].text,
            parameters: validateSqliteParams(args[0].parameters, operation),
            options,
            mode: allowResultMode ? options?.mode ?? "object" : undefined,
        };
    }

    if (typeof args[0] === "string") {
        if (args.length > 3) {
            throw new TypeError(`Sloppy sqlite.${operation} accepts sql, optional params, and optional options.`);
        }
        const inlineOptions = hasInlineOperationOptions(args);
        const params = inlineOptions ? undefined : args[1];
        const options = normalizeOperationOptions(
            inlineOptions ? args[1] : args[2],
            `sqlite.${operation}`,
            allowResultMode,
            allowMaxRows,
            allowCursorOptions,
        );

        if (args[0].length === 0) {
            throw new TypeError(`Sloppy sqlite.${operation} SQL must be a non-empty string.`);
        }

        return {
            text: args[0],
            parameters: validateSqliteParams(params, operation),
            options,
            mode: allowResultMode ? options?.mode ?? "object" : undefined,
        };
    }

    const call = normalizeProviderCallArguments(`sqlite.${operation}`, "question", args);
    return {
        text: call.query.text,
        parameters: validateSqliteParams(call.query.parameters, operation),
        options: call.options,
        mode: allowResultMode ? call.mode ?? "object" : undefined,
    };
}

async function openProviderCursor(provider, nativeBridge, handle, query, mode, methodName, registry) {
    const method = requireCursorBridgeMethod(nativeBridge, methodName, provider);
    const nativeCursor = await invokeNativeCursorOpen(method, handle, query, true);
    return createDataCursor(provider, nativeBridge, nativeCursor, mode, query.options, registry);
}

function createSqliteConnection(nativeBridge, handle) {
    const state = {
        closed: false,
        handle,
        transactionActive: false,
        activeCursors: new Set(),
    };

    function assertOpen(operation) {
        if (state.closed) {
            throw createSqliteClosedError(operation);
        }
    }

    function createSqliteTransaction() {
        const txState = {
            closed: false,
            activeCursors: new Set(),
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
                const method = query.mode === "raw"
                    ? nativeBridge.transactionQueryRaw
                    : nativeBridge.transactionQuery;
                return invokeProviderOperation("sqlite.transaction.query", query.options, () =>
                    invokeNativeQuery(method, state.handle, query, true));
            },

            queryRaw(...args) {
                assertTransactionOpen("transaction.queryRaw");
                const query = normalizeSqliteOperation("queryRaw", args);
                return invokeProviderOperation("sqlite.transaction.queryRaw", query.options, () =>
                    invokeNativeQuery(nativeBridge.transactionQueryRaw, state.handle, query, true));
            },

            async queryCursor(...args) {
                assertTransactionOpen("transaction.queryCursor");
                const query = normalizeSqliteOperation("queryCursor", args);
                const methodName = query.mode === "raw"
                    ? "transactionQueryRawCursor"
                    : "transactionQueryCursor";
                return invokeProviderOperation("sqlite.transaction.queryCursor", query.options, () =>
                    openProviderCursor("sqlite", nativeBridge, state.handle, query, query.mode, methodName, txState.activeCursors));
            },

            async queryRawCursor(...args) {
                assertTransactionOpen("transaction.queryRawCursor");
                const query = normalizeSqliteOperation("queryRawCursor", args);
                return invokeProviderOperation("sqlite.transaction.queryRawCursor", query.options, () =>
                    openProviderCursor("sqlite", nativeBridge, state.handle, query, "raw", "transactionQueryRawCursor", txState.activeCursors));
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
                closeActiveCursors(txState.activeCursors);
                txState.closed = true;
            },
        };
    }

    async function rollbackAfterCallbackError(error, transaction) {
        try {
            if (transaction !== undefined) {
                transaction.close();
            }
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
        state.transactionActive = false;
        throw error;
    }

    async function commitTransaction(transaction) {
        try {
            transaction.close();
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
        state.transactionActive = false;
    }

    const connection = {
        query(...args) {
            assertOpen("query");
            const query = normalizeSqliteOperation("query", args);
            const method = query.mode === "raw" ? nativeBridge.queryRaw : nativeBridge.query;
            return invokeProviderOperation("sqlite.query", query.options, () =>
                invokeNativeQuery(method, state.handle, query, true));
        },

        queryRaw(...args) {
            assertOpen("queryRaw");
            const query = normalizeSqliteOperation("queryRaw", args);
            return invokeProviderOperation("sqlite.queryRaw", query.options, () =>
                invokeNativeQuery(nativeBridge.queryRaw, state.handle, query, true));
        },

        async queryCursor(...args) {
            assertOpen("queryCursor");
            const query = normalizeSqliteOperation("queryCursor", args);
            const methodName = query.mode === "raw" ? "queryRawCursor" : "queryCursor";
            return invokeProviderOperation("sqlite.queryCursor", query.options, () =>
                openProviderCursor("sqlite", nativeBridge, state.handle, query, query.mode, methodName, state.activeCursors));
        },

        async queryRawCursor(...args) {
            assertOpen("queryRawCursor");
            const query = normalizeSqliteOperation("queryRawCursor", args);
            return invokeProviderOperation("sqlite.queryRawCursor", query.options, () =>
                openProviderCursor("sqlite", nativeBridge, state.handle, query, "raw", "queryRawCursor", state.activeCursors));
        },

        stream(...args) {
            return this.queryCursor(...args);
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

            closeActiveCursors(state.activeCursors);
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
    };
    return Object.freeze(markRealDataProvider(connection, "sqlite"));
}

function redactConnectionString(value) {
    return value
        .replace(
            /(^|[\s;?&])(password\s*=\s*)(?:'(?:\\.|[^'])*'|"(?:\\.|[^"])*"|[^\s;?&]*)/gi,
            (_match, prefix, key) => `${prefix}${key}<redacted>`,
        )
        .replace(/(postgres(?:ql)?:\/\/[^:\s/@]+:)[^@\s/]+(@)/gi, "$1<redacted>$2");
}

function validatePostgresOpenOptions(options) {
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
    if (access !== "read" && access !== "readwrite") {
        throw new TypeError("Sloppy postgres.open access must be read or readwrite.");
    }

    const maxConnections = options.maxConnections ?? 1;
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
        redactedConnectionString: redactConnectionString(options.connectionString),
        access,
        maxConnections,
        capability,
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

    if (typeof options.connectionString !== "string" || options.connectionString.length === 0 || options.connectionString.includes("\0")) {
        throw new TypeError("Sloppy sqlserver.open connectionString must be a non-empty string without NUL.");
    }
    const capability = options.capability ?? "data.sqlserver";
    if (typeof capability !== "string" || capability.length === 0 || capability.includes("\0")) {
        throw new TypeError("Sloppy sqlserver.open capability must be a non-empty string without NUL.");
    }

    const access = options.access ?? "readwrite";
    if (access !== "read" && access !== "readwrite") {
        throw new TypeError("Sloppy sqlserver.open access must be read or readwrite.");
    }

    const maxConnections = options.maxConnections ?? 1;
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
        redactedConnectionString: redactOdbcConnectionString(options.connectionString),
        driver: extractOdbcDriverName(options.connectionString),
        capability,
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
    const provider = normalizeSqliteProviderToken(name);
    const nativeBridge = sqliteNativeBridge();

    if (nativeBridge === null) {
        throw createSqliteUnavailableError("open");
    }

    return createSqliteConnection(nativeBridge, nativeBridge.open({
        provider,
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
    const allowResultMode = operation === "query" || operation === "queryCursor";
    const allowMaxRows = operation === "query" || operation === "queryRaw"
        || operation === "queryCursor" || operation === "queryRawCursor";
    const allowCursorOptions = operation === "queryCursor" || operation === "queryRawCursor";
    if (args.length === 1 && isLoweredQuery(args[0])) {
        return {
            text: args[0].text,
            parameters: args[0].parameters,
            options: undefined,
            mode: allowResultMode ? "object" : undefined,
        };
    }
    if (args.length === 2 && isLoweredQuery(args[0])) {
        const options = normalizeOperationOptions(
            args[1],
            `postgres.${operation}`,
            allowResultMode,
            allowMaxRows,
            allowCursorOptions,
        );
        return {
            text: args[0].text,
            parameters: args[0].parameters,
            options,
            mode: allowResultMode ? options?.mode ?? "object" : undefined,
        };
    }

    if (typeof args[0] === "string") {
        if (args.length > 3) {
            throw new TypeError(`Sloppy postgres.${operation} accepts sql, optional params, and optional options.`);
        }
        const inlineOptions = hasInlineOperationOptions(args);
        const params = inlineOptions ? undefined : args[1];
        const options = normalizeOperationOptions(
            inlineOptions ? args[1] : args[2],
            `postgres.${operation}`,
            allowResultMode,
            allowMaxRows,
            allowCursorOptions,
        );
        if (args[0].length === 0) {
            throw new TypeError(`Sloppy postgres.${operation} SQL must be a non-empty string.`);
        }
        if (params !== undefined && !Array.isArray(params)) {
            throw new TypeError(`Sloppy postgres.${operation} parameters must be an array.`);
        }
        return {
            text: args[0],
            parameters: params ?? [],
            options,
            mode: allowResultMode ? options?.mode ?? "object" : undefined,
        };
    }

    const call = normalizeProviderCallArguments(`postgres.${operation}`, "postgres", args);
    return {
        text: call.query.text,
        parameters: call.query.parameters,
        options: call.options,
        mode: allowResultMode ? call.mode ?? "object" : undefined,
    };
}

function createPostgresConnection(nativeBridge, handle) {
    const state = {
        closed: false,
        handle,
        transactionActive: false,
        activeCursors: new Set(),
    };

    function assertOpen(operation) {
        if (state.closed) {
            throw createPostgresClosedError(operation);
        }
    }

    function createTransaction() {
        const txState = { closed: false, activeCursors: new Set() };
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
                const method = query.mode === "raw"
                    ? nativeBridge.transactionQueryRaw
                    : nativeBridge.transactionQuery;
                return invokeProviderOperation("postgres.transaction.query", query.options, () =>
                    invokeNativeQuery(method, state.handle, query, true));
            },
            queryRaw(...args) {
                assertTransactionOpen("transaction.queryRaw");
                const query = normalizePostgresOperation("queryRaw", args);
                return invokeProviderOperation("postgres.transaction.queryRaw", query.options, () =>
                    invokeNativeQuery(nativeBridge.transactionQueryRaw, state.handle, query, true));
            },
            async queryCursor(...args) {
                assertTransactionOpen("transaction.queryCursor");
                const query = normalizePostgresOperation("queryCursor", args);
                const methodName = query.mode === "raw"
                    ? "transactionQueryRawCursor"
                    : "transactionQueryCursor";
                return invokeProviderOperation("postgres.transaction.queryCursor", query.options, () =>
                    openProviderCursor("postgres", nativeBridge, state.handle, query, query.mode, methodName, txState.activeCursors));
            },
            async queryRawCursor(...args) {
                assertTransactionOpen("transaction.queryRawCursor");
                const query = normalizePostgresOperation("queryRawCursor", args);
                return invokeProviderOperation("postgres.transaction.queryRawCursor", query.options, () =>
                    openProviderCursor("postgres", nativeBridge, state.handle, query, "raw", "transactionQueryRawCursor", txState.activeCursors));
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
                closeActiveCursors(txState.activeCursors);
                txState.closed = true;
            },
        };
    }

    async function rollbackAfterCallbackError(error, transaction) {
        try {
            transaction.close();
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
        state.transactionActive = false;
        throw error;
    }

    async function commitTransaction(transaction) {
        try {
            transaction.close();
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
        state.transactionActive = false;
    }

    const connection = {
        query(...args) {
            assertOpen("query");
            const query = normalizePostgresOperation("query", args);
            const method = query.mode === "raw" ? nativeBridge.queryRaw : nativeBridge.query;
            return invokeProviderOperation("postgres.query", query.options, () =>
                invokeNativeQuery(method, state.handle, query, true));
        },
        queryRaw(...args) {
            assertOpen("queryRaw");
            const query = normalizePostgresOperation("queryRaw", args);
            return invokeProviderOperation("postgres.queryRaw", query.options, () =>
                invokeNativeQuery(nativeBridge.queryRaw, state.handle, query, true));
        },
        async queryCursor(...args) {
            assertOpen("queryCursor");
            const query = normalizePostgresOperation("queryCursor", args);
            const methodName = query.mode === "raw" ? "queryRawCursor" : "queryCursor";
            return invokeProviderOperation("postgres.queryCursor", query.options, () =>
                openProviderCursor("postgres", nativeBridge, state.handle, query, query.mode, methodName, state.activeCursors));
        },
        async queryRawCursor(...args) {
            assertOpen("queryRawCursor");
            const query = normalizePostgresOperation("queryRawCursor", args);
            return invokeProviderOperation("postgres.queryRawCursor", query.options, () =>
                openProviderCursor("postgres", nativeBridge, state.handle, query, "raw", "queryRawCursor", state.activeCursors));
        },
        stream(...args) {
            return this.queryCursor(...args);
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
            closeActiveCursors(state.activeCursors);
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
    };
    return Object.freeze(markRealDataProvider(connection, "postgres"));
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
    const allowResultMode = operation === "query" || operation === "queryCursor";
    const allowMaxRows = operation === "query" || operation === "queryRaw"
        || operation === "queryCursor" || operation === "queryRawCursor";
    const allowCursorOptions = operation === "queryCursor" || operation === "queryRawCursor";
    if (args.length === 1 && isLoweredQuery(args[0])) {
        return {
            text: args[0].text,
            parameters: args[0].parameters,
            options: undefined,
            mode: allowResultMode ? "object" : undefined,
        };
    }
    if (args.length === 2 && isLoweredQuery(args[0])) {
        const options = normalizeOperationOptions(
            args[1],
            `sqlserver.${operation}`,
            allowResultMode,
            allowMaxRows,
            allowCursorOptions,
        );
        return {
            text: args[0].text,
            parameters: args[0].parameters,
            options,
            mode: allowResultMode ? options?.mode ?? "object" : undefined,
        };
    }

    if (typeof args[0] === "string") {
        if (args.length > 3) {
            throw new TypeError(`Sloppy sqlserver.${operation} accepts sql, optional params, and optional options.`);
        }
        const inlineOptions = hasInlineOperationOptions(args);
        const params = inlineOptions ? undefined : args[1];
        const options = normalizeOperationOptions(
            inlineOptions ? args[1] : args[2],
            `sqlserver.${operation}`,
            allowResultMode,
            allowMaxRows,
            allowCursorOptions,
        );
        if (args[0].length === 0) {
            throw new TypeError(`Sloppy sqlserver.${operation} SQL must be a non-empty string.`);
        }
        if (params !== undefined && !Array.isArray(params)) {
            throw new TypeError(`Sloppy sqlserver.${operation} parameters must be an array.`);
        }
        return {
            text: args[0],
            parameters: params ?? [],
            options,
            mode: allowResultMode ? options?.mode ?? "object" : undefined,
        };
    }

    const call = normalizeProviderCallArguments(`sqlserver.${operation}`, "question", args);
    return {
        text: call.query.text,
        parameters: call.query.parameters,
        options: call.options,
        mode: allowResultMode ? call.mode ?? "object" : undefined,
    };
}

function createSqlServerConnection(nativeBridge, handle) {
    const state = {
        closed: false,
        handle,
        transactionActive: false,
        activeCursors: new Set(),
    };

    function assertOpen(operation) {
        if (state.closed) {
            throw createSqlServerClosedError(operation);
        }
    }

    function createTransaction() {
        const txState = { closed: false, activeCursors: new Set() };
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
                const method = query.mode === "raw"
                    ? nativeBridge.transactionQueryRaw
                    : nativeBridge.transactionQuery;
                return invokeProviderOperation("sqlserver.transaction.query", query.options, () =>
                    invokeNativeQuery(method, state.handle, query, true));
            },
            queryRaw(...args) {
                assertTransactionOpen("transaction.queryRaw");
                const query = normalizeSqlServerOperation("queryRaw", args);
                return invokeProviderOperation("sqlserver.transaction.queryRaw", query.options, () =>
                    invokeNativeQuery(nativeBridge.transactionQueryRaw, state.handle, query, true));
            },
            async queryCursor(...args) {
                assertTransactionOpen("transaction.queryCursor");
                const query = normalizeSqlServerOperation("queryCursor", args);
                const methodName = query.mode === "raw"
                    ? "transactionQueryRawCursor"
                    : "transactionQueryCursor";
                return invokeProviderOperation("sqlserver.transaction.queryCursor", query.options, () =>
                    openProviderCursor("sqlserver", nativeBridge, state.handle, query, query.mode, methodName, txState.activeCursors));
            },
            async queryRawCursor(...args) {
                assertTransactionOpen("transaction.queryRawCursor");
                const query = normalizeSqlServerOperation("queryRawCursor", args);
                return invokeProviderOperation("sqlserver.transaction.queryRawCursor", query.options, () =>
                    openProviderCursor("sqlserver", nativeBridge, state.handle, query, "raw", "transactionQueryRawCursor", txState.activeCursors));
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
                closeActiveCursors(txState.activeCursors);
                txState.closed = true;
            },
        };
    }

    async function rollbackAfterCallbackError(error, transaction) {
        try {
            transaction.close();
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
        state.transactionActive = false;
        throw error;
    }

    async function commitTransaction(transaction) {
        try {
            transaction.close();
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
        state.transactionActive = false;
    }

    const connection = {
        query(...args) {
            assertOpen("query");
            const query = normalizeSqlServerOperation("query", args);
            const method = query.mode === "raw" ? nativeBridge.queryRaw : nativeBridge.query;
            return invokeProviderOperation("sqlserver.query", query.options, () =>
                invokeNativeQuery(method, state.handle, query, true));
        },
        queryRaw(...args) {
            assertOpen("queryRaw");
            const query = normalizeSqlServerOperation("queryRaw", args);
            return invokeProviderOperation("sqlserver.queryRaw", query.options, () =>
                invokeNativeQuery(nativeBridge.queryRaw, state.handle, query, true));
        },
        async queryCursor(...args) {
            assertOpen("queryCursor");
            const query = normalizeSqlServerOperation("queryCursor", args);
            const methodName = query.mode === "raw" ? "queryRawCursor" : "queryCursor";
            return invokeProviderOperation("sqlserver.queryCursor", query.options, () =>
                openProviderCursor("sqlserver", nativeBridge, state.handle, query, query.mode, methodName, state.activeCursors));
        },
        async queryRawCursor(...args) {
            assertOpen("queryRawCursor");
            const query = normalizeSqlServerOperation("queryRawCursor", args);
            return invokeProviderOperation("sqlserver.queryRawCursor", query.options, () =>
                openProviderCursor("sqlserver", nativeBridge, state.handle, query, "raw", "queryRawCursor", state.activeCursors));
        },
        stream(...args) {
            return this.queryCursor(...args);
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
            closeActiveCursors(state.activeCursors);
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
    };
    return Object.freeze(markRealDataProvider(connection, "sqlserver"));
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
    cursors: true,
    cursorModes: Object.freeze(["object", "raw"]),
    responseStreamingAdapter: false,
    preparedStatements: false,
    pooling: false,
    migrations: true,
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
    cursors: true,
    cursorModes: Object.freeze(["object", "raw"]),
    responseStreamingAdapter: false,
    pooling: true,
    maxPoolConnections: POSTGRES_MAX_POOL_CONNECTIONS,
    executionMode: "TRUE_ASYNC",
    migrations: true,
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
    cursors: true,
    cursorModes: Object.freeze(["object", "raw"]),
    responseStreamingAdapter: false,
    pooling: true,
    maxPoolConnections: SQLSERVER_MAX_POOL_CONNECTIONS,
    executionMode: "TRUE_ASYNC",
    migrations: true,
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
            maxPoolConnections: POSTGRES_MAX_POOL_CONNECTIONS,
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
            maxPoolConnections: SQLSERVER_MAX_POOL_CONNECTIONS,
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
            const method = call.mode === "raw" ? state.provider.queryRaw : state.provider.query;
            return invokeProviderOperation("query", call.options, () =>
                method(call.query, call.options));
        },

        queryRaw(...args) {
            assertTransactionOpen(state, "queryRaw");
            const call = normalizeProviderCallArguments("queryRaw", placeholderStyle, args);
            return invokeProviderOperation("queryRaw", call.options, () =>
                state.provider.queryRaw(call.query, call.options));
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

        queryRaw(query, options) {
            if (definition.queryRaw === undefined) {
                missingProviderMethod("queryRaw");
            }

            return definition.queryRaw(query, options);
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
            const method = call.mode === "raw" ? backend.queryRaw : backend.query;
            return invokeProviderOperation("query", call.options, () =>
                method(call.query, call.options));
        },

        queryRaw(...args) {
            const call = normalizeProviderCallArguments("queryRaw", placeholderStyle, args);
            return invokeProviderOperation("queryRaw", call.options, () =>
                backend.queryRaw(call.query, call.options));
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

const Migrations = Object.freeze({
    apply: applyMigrations,
    status: migrationStatus,
});

const ProviderHealth = Object.freeze({
    check: checkProviderHealth,
});

export { Migrations, ProviderHealth, isRealDataProvider, sql };

export const data = Object.freeze({
    createFakeProvider,
    lowerQueryTemplate: createLoweredQuery,
    isQuery: isLoweredQuery,
    values,
    migrations: Migrations,
    providerHealth: ProviderHealth,
    sqlite,
    postgres,
    sqlserver,
});
