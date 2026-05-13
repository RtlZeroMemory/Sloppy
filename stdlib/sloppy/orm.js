import { Migrations, sql as dataSql } from "./data.js";
import { schema as Schema, isSchema } from "./schema.js";

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

export {
    SloppyOrmConcurrencyError,
    SloppyOrmError,
    column,
    orm,
    rawSql as sql,
    relation,
    table,
};
