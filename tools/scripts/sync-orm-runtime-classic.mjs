import { readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "..", "..");
const schemaPath = path.join(repoRoot, "stdlib", "sloppy", "schema.js");
const ormPath = path.join(repoRoot, "stdlib", "sloppy", "orm.js");
const runtimePath = path.join(repoRoot, "stdlib", "sloppy", "internal", "runtime-classic.js");

function normalizeNewlines(text) {
    return text.replace(/\r\n/gu, "\n");
}

function indent(text) {
    return text
        .split("\n")
        .map((line) => line.length === 0 ? "" : `    ${line}`)
        .join("\n");
}

function replaceRequired(source, pattern, replacement, label) {
    if (!pattern.test(source)) {
        throw new Error(`sync-orm-runtime-classic: required pattern not found (${label}).`);
    }
    return source.replace(pattern, replacement);
}

function transformSchema(source) {
    const body = replaceRequired(
            normalizeNewlines(source),
            /\nexport const schema = schemaApi;\nexport const Schema = schemaApi;\nexport \{ SloppyValidationError, isSchema, isValidationError, validationProblem \};\s*$/u,
            "\nreturn Object.freeze({ schema: schemaApi, Schema: schemaApi, SloppyValidationError, isSchema, isValidationError, validationProblem });\n",
            "schema exports",
        )
        .trimEnd();
    return `function createSloppySchemaRuntime() {\n${indent(body)}\n}`;
}

function transformOrm(source) {
    let normalized = normalizeNewlines(source);
    normalized = replaceRequired(
        normalized,
        /^import \{ Migrations, sql as dataSql \} from "\.\/data\.js";\n/u,
        "",
        "orm data import",
    );
    normalized = replaceRequired(
        normalized,
        /^import \{ schema as Schema, isSchema \} from "\.\/schema\.js";\n\n/u,
        "",
        "orm schema import",
    );
    const body = replaceRequired(
            normalized,
            /\nexport \{\n    SloppyOrmConcurrencyError,\n    SloppyOrmError,\n    column,\n    orm,\n    rawSql as sql,\n    relation,\n    table,\n\};\s*$/u,
            "\nreturn Object.freeze({ SloppyOrmConcurrencyError, SloppyOrmError, column, orm, sql: rawSql, relation, table });\n",
            "orm exports",
        )
        .trimEnd();
    const prelude = [
        "const __schemaRuntime = createSloppySchemaRuntime();",
        "const Schema = __schemaRuntime.Schema;",
        "const isSchema = __schemaRuntime.isSchema;",
    ].join("\n");
    return `function createSloppyOrmRuntime(dataSql, Migrations) {\n${indent(`${prelude}\n${body}`)}\n}`;
}

function buildChunk(schemaSource, ormSource) {
    return `${indent(transformSchema(schemaSource))}\n\n${indent(transformOrm(ormSource))}\n`;
}

function replaceChunk(runtimeSource, chunk) {
    const normalized = normalizeNewlines(runtimeSource);
    const startMarker = "    function createSloppySchemaRuntime() {";
    const endMarker = "    const __sloppyOrmRuntime = createSloppyOrmRuntime(sql, DataMigrations);";
    const start = normalized.indexOf(startMarker);
    const end = normalized.indexOf(endMarker);
    if (start === -1 || end === -1 || end <= start) {
        throw new Error("runtime-classic.js ORM runtime markers were not found.");
    }
    return `${normalized.slice(0, start)}${chunk}${normalized.slice(end)}`;
}

const check = process.argv.includes("--check");
const [schemaSource, ormSource, runtimeSource] = await Promise.all([
    readFile(schemaPath, "utf8"),
    readFile(ormPath, "utf8"),
    readFile(runtimePath, "utf8"),
]);
const nextRuntime = replaceChunk(runtimeSource, buildChunk(schemaSource, ormSource));
const currentRuntime = normalizeNewlines(runtimeSource);

if (check) {
    if (nextRuntime !== currentRuntime) {
        console.error("runtime-classic.js embedded ORM runtime is stale. Run:");
        console.error("  node tools/scripts/sync-orm-runtime-classic.mjs");
        process.exit(1);
    }
} else if (nextRuntime !== currentRuntime) {
    await writeFile(runtimePath, nextRuntime, "utf8");
}
