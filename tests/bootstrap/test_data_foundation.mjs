import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

import { Sloppy, data, sql } from "../../stdlib/sloppy/index.js";

function assertThrowsMessage(fn, expected) {
    assert.throws(fn, (error) => {
        assert.match(String(error.message), expected);
        return true;
    });
}

function assertThrowsSnapshot(fn, path) {
    assert.throws(fn, (error) => {
        const expected = readFileSync(path, "utf8");
        assert.equal(`${error.message}\n`, expected);
        return true;
    });
}

async function assertRejectsMessage(fn, expected) {
    await assert.rejects(fn, (error) => {
        assert.match(String(error.message), expected);
        return true;
    });
}

function createForgedLoweredQuery() {
    return {
        __sloppyQuery: true,
        text: "select unsafe",
        parameters: [],
        parameterCount: 0,
        placeholderStyle: "question",
        placeholders: [],
    };
}

{
    const decimal = sql.decimal("12345678901234567890.1234");
    const uuid = sql.uuid("00000000-0000-4000-8000-000000000001");
    const date = sql.date("2026-05-08");
    const time = sql.time("12:34:56.123");
    const timestamp = sql.timestamp("2026-05-08 12:34:56");
    const instant = sql.instant("2026-05-08T12:34:56Z");
    const offset = sql.offsetDateTime("2026-05-08T12:34:56+04:00");
    const json = sql.json({ ok: true });
    const rawJson = sql.rawJson("{\"ok\":true}");
    const sourceBytes = new Uint8Array([0, 1, 255]);
    const bytes = sql.bytes(sourceBytes);
    sourceBytes[0] = 99;

    assert.equal(decimal.__sloppyDbValue, true);
    assert.equal(decimal.kind, "decimal");
    assert.equal(decimal.toString(), "12345678901234567890.1234");
    assert.equal(Object.isFrozen(decimal), true);
    assert.equal(data.values.isDbValue(uuid), true);
    assert.equal(date.toString(), "2026-05-08");
    assert.equal(time.toString(), "12:34:56.123");
    assert.equal(timestamp.kind, "localDateTime");
    assert.equal(instant.kind, "instant");
    assert.equal(offset.kind, "offsetDateTime");
    assert.equal(json.toString(), "{\"ok\":true}");
    assert.equal(rawJson.toString(), "{\"ok\":true}");
    assert.equal(data.values.isDbValue(bytes), true);
    assert.equal(Object.isFrozen(bytes), true);
    assert.equal(bytes.kind, "bytes");
    assert.deepEqual(Array.from(bytes.value), [0, 1, 255]);
    bytes.value[0] = 42;
    assert.deepEqual(Array.from(bytes.value), [0, 1, 255]);
    assertThrowsMessage(() => sql.decimal("NaN"), /finite decimal/);
    assert.equal(sql.uuid("00000000-0000-7000-8000-000000000001").kind, "uuid");
    assertThrowsMessage(() => sql.uuid("not-a-uuid"), /canonical UUID/);
    assertThrowsMessage(() => sql.date("2026/05/08"), /YYYY-MM-DD/);
    assertThrowsMessage(() => sql.json(undefined), /JSON-serializable/);
    assertThrowsMessage(() => sql.rawJson("{"), /valid JSON/);
    assert.equal(data.values.isDbValue({
        __sloppyDbValue: true,
        kind: "decimal",
        value: "1",
    }), false);
}

{
    const builder = Sloppy.createBuilder();

    assert.equal(builder.capabilities.addDatabase("data.main", {
        provider: "sqlite",
        access: "readwrite",
    }), builder.capabilities);

    assert.equal(builder.capabilities.has("data.main"), true);
    assert.equal(builder.capabilities.has("data.audit"), false);
    assert.deepEqual(builder.capabilities.get("data.main"), {
        token: "data.main",
        kind: "database",
        provider: "sqlite",
        access: "readwrite",
        module: null,
        metadata: {
            provider: "sqlite",
            access: "readwrite",
        },
    });
    assert.equal(builder.capabilities.list().length, 1);

    assertThrowsMessage(() => builder.capabilities.addDatabase("", {
        provider: "sqlite",
        access: "readwrite",
    }), /non-empty string/);
    assertThrowsMessage(() => builder.capabilities.addDatabase("data.bad", {
        provider: "",
        access: "readwrite",
    }), /provider/);
    assertThrowsMessage(() => builder.capabilities.addDatabase("data.bad", {
        provider: "sqlite",
        access: "admin",
    }), /access/);
    assertThrowsMessage(() => builder.capabilities.addDatabase("data.main", {
        provider: "sqlite",
        access: "readwrite",
    }), /already declared[\s\S]*data\.main/);
    assertThrowsMessage(() => builder.capabilities.get("data.missing"), /not declared/);

    const app = builder.build();
    assert.equal(app.capabilities.has("data.main"), true);
    assert.equal(app.capabilities.get("data.main").provider, "sqlite");
    assert.deepEqual(app.__getPlanContributions().capabilities.map((cap) => cap.token), [
        "data.main",
    ]);
    assertThrowsMessage(() => builder.capabilities.addDatabase("data.late", {
        provider: "sqlite",
        access: "read",
    }), /builder is frozen/);
}

{
    const calls = [];
    const DataModule = Sloppy.module("data")
        .capabilities((caps) => {
            calls.push("capabilities");
            caps.addDatabase("data.main", {
                provider: "sqlite",
                access: "readwrite",
            });
        })
        .services((services) => {
            calls.push("services");
            services.addSingleton("data.capability", (scope) => scope.capabilities.get("data.main"));
        });

    const builder = Sloppy.createBuilder();
    builder.addModule(DataModule);
    const app = builder.build();

    assert.deepEqual(calls, ["capabilities", "services"]);
    assert.equal(app.capabilities.get("data.main").module, "data");
    assert.equal(app.services.get("data.capability").token, "data.main");
    assert.deepEqual(app.__debug().modules[0].capabilities, ["data.main"]);
    assert.deepEqual(app.__debug().modules[0].contributes, ["capabilities", "services"]);
}

{
    const query = sql`select id, name from users where id = ${42}`;

    assert.equal(query.__sloppyQuery, true);
    assert.equal(query.text, "select id, name from users where id = ?");
    assert.deepEqual(query.parameters, [42]);
    assert.equal(query.parameterCount, 1);
    assert.equal(query.placeholderStyle, "question");
    assert.deepEqual(query.placeholders, [{
        index: 0,
        text: "?",
        name: null,
        position: 1,
    }]);
}

{
    assert.deepEqual(sql`select 1`, {
        __sloppyQuery: true,
        text: "select 1",
        parameters: [],
        parameterCount: 0,
        placeholderStyle: "question",
        placeholders: [],
    });

    assert.equal(sql`a ${1} b ${2} c`.text, "a ? b ? c");
    assert.deepEqual(sql`a ${1} b ${2} c`.parameters, [1, 2]);
    assert.equal(sql.lower(["a ", " b ", " c"], ["x", "y"], {
        placeholderStyle: "postgres",
    }).text, "a $1 b $2 c");
    assert.equal(sql.lower(["a ", " b"], ["x"], {
        placeholderStyle: "named",
    }).text, "a @p1 b");
    assert.deepEqual(sql.lower(["a ", " b"], ["x"], {
        placeholderStyle: "named",
    }).placeholders[0], {
        index: 0,
        text: "@p1",
        name: "p1",
        position: 1,
    });
    assert.equal(sql`select ${"not interpolated"}`.text, "select ?");
    assertThrowsMessage(() => sql("select 1"), /tagged template/);
    assertThrowsMessage(() => sql.lower(["a"], "not array"), /values must be an array/);
    assert.equal(data.isQuery({ __sloppyQuery: true }), false);
}

{
    assert.equal(data.sqlite.provider, "sqlite");
    assert.equal(data.sqlite.placeholderStyle, "question");
    assert.equal(data.sqlite.supports.memory, true);
    assert.equal(data.sqlite.supports.transactions, true);
    assert.equal(data.sqlite.supports.transactionsMode, "callback");
    assert.equal(data.sqlite.supports.preparedStatements, false);
    assert.equal(data.sqlite.supports.pooling, false);
    assert.deepEqual(data.sqlite.supports.parameters, [
        "null",
        "string",
        "integer",
        "bigint",
        "float",
        "boolean",
        "bytes",
        "explicit-json-text",
        "explicit-date-time-text",
    ]);
    assert.equal(data.sqlite.supports.nativeStdlibBridge, false);
    assert.equal(data.sqlite.__debug().nativeStdlibBridge, false);
    assertThrowsMessage(() => data.sqlite.open(":memory:"), /options must be a plain object/);
    assertThrowsMessage(() => data.sqlite.open({}), /database must be a non-empty string/);
    assertThrowsMessage(() => data.sqlite.open({
        database: ":memory:",
        capability: "data.main",
        timeoutMs: 100,
    }), /option 'timeoutMs' is not supported/);
    assertThrowsMessage(() => data.sqlite.open({
        database: ":memory:",
        path: "other.db",
        capability: "data.main",
    }), /database and path must match/);
    assertThrowsMessage(() => data.sqlite.open({
        database: ":memory:",
        capability: "data.main",
        access: "admin",
    }), /access must be read, write, or readwrite/);
    assertThrowsMessage(() => data.sqlite.open({
        database: ":memory:",
    }), /capability must be a non-empty string/);
    assertThrowsSnapshot(() => data.sqlite.open({
        database: ":memory:",
        capability: "data.main",
    }), "tests/golden/diagnostics/runtime_feature_inactive_sqlite_intrinsic.snap");
    assertThrowsMessage(() => data.sqlite.open({
        path: ":memory:",
        capability: "data.main",
    }), /SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE[\s\S]*Feature:[\s\S]*provider\.sqlite[\s\S]*Operation:[\s\S]*open/);
    assertThrowsMessage(() => data.sqlite("main"), /SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE[\s\S]*Feature:[\s\S]*provider\.sqlite[\s\S]*Operation:[\s\S]*open/);
}

{
    const calls = [];
    const previousSloppy = globalThis.__sloppy;
    globalThis.__sloppy = {
        data: {
            sqlite: {
                open(options) {
                    calls.push([
                        "open",
                        options.database,
                        options.capability,
                        options.access,
                        options.provider,
                    ]);
                    return { slot: 1, generation: 1, kind: "sqlite.connection" };
                },
                exec(handle, text, params) {
                    calls.push(["exec", handle.slot, text, params]);
                    return { affectedRows: 1 };
                },
                query(handle, text, params) {
                    calls.push(["query", handle.generation, text, params]);
                    return [{ name: "Ada" }];
                },
                queryRaw(handle, text, params) {
                    calls.push(["queryRaw", handle.generation, text, params]);
                    return { mode: "raw", columnNames: ["name"], rows: [["Ada"]] };
                },
                queryOne(handle, text, params) {
                    calls.push(["queryOne", handle.kind, text, params]);
                    return { name: "Ada" };
                },
                close(handle) {
                    calls.push(["close", handle.slot]);
                },
                transactionBegin(handle) {
                    calls.push(["begin", handle.slot]);
                },
                transactionCommit(handle) {
                    calls.push(["commit", handle.slot]);
                },
                transactionRollback(handle) {
                    calls.push(["rollback", handle.slot]);
                },
                transactionExec(handle, text, params) {
                    calls.push(["txExec", handle.slot, text, params]);
                    return { affectedRows: 1 };
                },
                transactionQuery(handle, text, params) {
                    calls.push(["txQuery", handle.slot, text, params]);
                    return [{ tx: true, text }];
                },
                transactionQueryRaw(handle, text, params) {
                    calls.push(["txQueryRaw", handle.slot, text, params]);
                    return { mode: "raw", columnNames: ["tx"], rows: [[true]] };
                },
                transactionQueryOne(handle, text, params) {
                    calls.push(["txQueryOne", handle.slot, text, params]);
                    return { tx: true, text };
                },
            },
            postgres: {
                open(options) {
                    calls.push(["pgOpen", options.connectionString]);
                    return { slot: 2, kind: "postgres.connection" };
                },
                query(handle, text, params) {
                    calls.push(["pgQuery", handle.slot, text, params]);
                    return [{ pg: true }];
                },
                queryRaw(handle, text, params) {
                    calls.push(["pgQueryRaw", handle.slot, text, params]);
                    return { mode: "raw", rows: [[true]] };
                },
                close(handle) {
                    calls.push(["pgClose", handle.slot]);
                },
            },
            sqlserver: {
                open(options) {
                    calls.push(["mssqlOpen", options.connectionString]);
                    return { slot: 3, kind: "sqlserver.connection" };
                },
                query(handle, text, params) {
                    calls.push(["mssqlQuery", handle.slot, text, params]);
                    return [{ mssql: true }];
                },
                queryRaw(handle, text, params) {
                    calls.push(["mssqlQueryRaw", handle.slot, text, params]);
                    return { mode: "raw", rows: [[true]] };
                },
                close(handle) {
                    calls.push(["mssqlClose", handle.slot]);
                },
            },
        },
    };

    try {
        const db = data.sqlite.open({
            database: ":memory:",
            capability: "data.main",
        });
        const byProvider = data.sqlite("main");
        byProvider.close();
        const writeDb = data.sqlite.open({
            database: ":memory:",
            capability: "data.main",
            access: "write",
        });
        writeDb.close();
        assert.equal(data.sqlite.supports.nativeStdlibBridge, true);
        assert.equal(data.sqlite.__debug().nativeStdlibBridge, true);
        assert.deepEqual(db.exec("insert into users (name) values (?)", ["Ada"]), {
            affectedRows: 1,
        });
        assert.deepEqual(
            db.exec("insert into blobs (raw) values (?)", [new Uint8Array([0, 1, 255])]),
            { affectedRows: 1 },
        );
        assert.deepEqual(db.query("select name from users", []), [{ name: "Ada" }]);
        assert.deepEqual(db.query("select name from users", { timeoutMs: 500 }), [{ name: "Ada" }]);
        assert.deepEqual(db.query("select name from users", { mode: "raw" }), {
            mode: "raw",
            columnNames: ["name"],
            rows: [["Ada"]],
        });
        assert.deepEqual(db.queryRaw("select name from users", []), {
            mode: "raw",
            columnNames: ["name"],
            rows: [["Ada"]],
        });
        assertThrowsMessage(
            () => db.query("select name from users", { mode: "facade" }),
            /mode must be object or raw/,
        );
        assertThrowsMessage(
            () => db.exec("select 1", [], { mode: "raw" }),
            /option 'mode' is not supported/,
        );
        assert.deepEqual(db.queryOne(sql`select name from users where id = ${1}`), { name: "Ada" });
        assert.deepEqual(
            db.query(sql`select name from users`, {
                deadline: { remainingMs: () => 1000 },
                signal: { aborted: false, throwIfAborted() {} },
                timeoutMs: 500,
            }),
            [{ name: "Ada" }],
        );
        assertThrowsMessage(
            () => db.query(sql`select name from users`, { deadline: { remainingMs: () => 0 } }),
            /SLOPPY_E_DEADLINE_EXCEEDED/,
        );
        assertThrowsMessage(
            () => db.query(sql`select name from users`, { signal: { aborted: true, reason: "client cancelled" } }),
            /SLOPPY_E_CANCELLED[\s\S]*client cancelled/,
        );
        assertThrowsMessage(
            () => db.query("select name from users", {
                signal: {
                    aborted: false,
                    reason: "timeout signal",
                    throwIfAborted() {
                        throw new Error("raw abort");
                    },
                },
            }),
            /SLOPPY_E_CANCELLED[\s\S]*timeout signal/,
        );
        const pgDb = data.postgres.open({ connectionString: "postgres://localhost/sloppy" });
        assert.deepEqual(pgDb.query("select id from users", { timeoutMs: 250 }), [{ pg: true }]);
        assert.deepEqual(pgDb.query("select id from users", { mode: "raw" }), {
            mode: "raw",
            rows: [[true]],
        });
        pgDb.close();
        const sqlServerDb = data.sqlserver.open({ connectionString: "Driver={ODBC Driver 18 for SQL Server};Server=localhost;" });
        assert.deepEqual(sqlServerDb.query("select id from users", { timeoutMs: 250 }), [{ mssql: true }]);
        assert.deepEqual(sqlServerDb.queryRaw("select id from users", []), {
            mode: "raw",
            rows: [[true]],
        });
        sqlServerDb.close();
        let capturedTx;
        const txResult = await db.transaction(async (tx) => {
            capturedTx = tx;
            assert.deepEqual(tx.exec("insert into users (name) values (?)", ["Grace"]), {
                affectedRows: 1,
            });
            assert.deepEqual(tx.query("select name from users", []), [{
                tx: true,
                text: "select name from users",
            }]);
            assert.deepEqual(tx.query("select name from users", [], { mode: "raw" }), {
                mode: "raw",
                columnNames: ["tx"],
                rows: [[true]],
            });
            assert.deepEqual(tx.queryOne(sql`select name from users where id = ${2}`), {
                tx: true,
                text: "select name from users where id = ?",
            });
            assertThrowsMessage(() => tx.transaction(() => {}), /nested transactions/);
            return "committed";
        });
        assert.equal(txResult, "committed");
        assertThrowsMessage(() => capturedTx.exec("select 1"), /transaction scope is closed/);
        await assertRejectsMessage(() => db.transaction(async () => {
            throw new Error("rollback requested");
        }), /rollback requested/);
        await assertRejectsMessage(() => db.transaction(async () => {
            await db.transaction(async () => {});
        }), /nested transactions/);
        assert.equal(db.prepare, undefined);
        assert.equal(db.__debug().resource, "opaque");
        assert.equal(db.__debug().transactionActive, false);
        db.close();
        db.close();
        assertThrowsMessage(() => db.query("select 1"), /sqlite connection is closed/);
        assert.deepEqual(calls, [
            ["open", ":memory:", "data.main", "readwrite", "sqlite"],
            ["open", undefined, undefined, undefined, "data.main"],
            ["close", 1],
            ["open", ":memory:", "data.main", "write", "sqlite"],
            ["close", 1],
            ["exec", 1, "insert into users (name) values (?)", ["Ada"]],
            ["exec", 1, "insert into blobs (raw) values (?)", [new Uint8Array([0, 1, 255])]],
            ["query", 1, "select name from users", []],
            ["query", 1, "select name from users", []],
            ["queryRaw", 1, "select name from users", []],
            ["queryRaw", 1, "select name from users", []],
            ["queryOne", "sqlite.connection", "select name from users where id = ?", [1]],
            ["query", 1, "select name from users", []],
            ["pgOpen", "postgres://localhost/sloppy"],
            ["pgQuery", 2, "select id from users", []],
            ["pgQueryRaw", 2, "select id from users", []],
            ["pgClose", 2],
            ["mssqlOpen", "Driver={ODBC Driver 18 for SQL Server};Server=localhost;"],
            ["mssqlQuery", 3, "select id from users", []],
            ["mssqlQueryRaw", 3, "select id from users", []],
            ["mssqlClose", 3],
            ["begin", 1],
            ["txExec", 1, "insert into users (name) values (?)", ["Grace"]],
            ["txQuery", 1, "select name from users", []],
            ["txQueryRaw", 1, "select name from users", []],
            ["txQueryOne", 1, "select name from users where id = ?", [2]],
            ["commit", 1],
            ["begin", 1],
            ["rollback", 1],
            ["begin", 1],
            ["rollback", 1],
            ["close", 1],
        ]);
    } finally {
        if (previousSloppy === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previousSloppy;
        }
    }
}

{
    assert.equal(data.postgres.provider, "postgres");
    assert.equal(data.postgres.placeholderStyle, "postgres");
    assert.equal(data.postgres.supports.connectionString, true);
    assert.equal(data.postgres.supports.transactions, true);
    assert.equal(data.postgres.supports.pooling, true);
    assert.equal(data.postgres.supports.maxPoolConnections, 256);
    assert.equal(data.postgres.supports.executionMode, "TRUE_ASYNC");
    assert.equal(data.postgres.supports.nativeStdlibBridge, false);
    assert.equal(data.postgres.__debug().nativeStdlibBridge, false);
    assert.equal(data.postgres.__debug().maxPoolConnections, 256);
    assert.deepEqual(data.postgres.supports.parameters, [
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
    ]);
    assert.equal(sql.lower(["a ", " b ", " c"], ["x", "y"], {
        placeholderStyle: data.postgres.placeholderStyle,
    }).text, "a $1 b $2 c");
    assert.equal(
        data.postgres.redactConnectionString("postgres://ada:secret@localhost/db"),
        "postgres://ada:<redacted>@localhost/db",
    );
    assert.equal(
        data.postgres.redactConnectionString("host=localhost password=secret user=ada"),
        "host=localhost password=<redacted> user=ada",
    );
    assert.equal(
        data.postgres.redactConnectionString("host=localhost Password='secret value' user=ada"),
        "host=localhost Password=<redacted> user=ada",
    );
    assert.equal(
        data.postgres.redactConnectionString("password='secret\\' value' user=ada"),
        "password=<redacted> user=ada",
    );
    assertThrowsMessage(() => data.postgres.open({}), /connectionString must be a non-empty string/);
    assertThrowsMessage(() => data.postgres.open({
        connectionString: "postgres://localhost/sloppy",
        access: "admin",
    }), /access must be read or readwrite/);
    assertThrowsMessage(() => data.postgres.open({
        connectionString: "postgres://localhost/sloppy",
        maxConnections: 0,
    }), /maxConnections must be an integer from 1 to 256/);
    assertThrowsMessage(() => data.postgres.open({
        connectionString: "postgres://localhost/sloppy",
        maxConnections: 1.5,
    }), /maxConnections must be an integer from 1 to 256/);
    assertThrowsMessage(() => data.postgres.open({
        connectionString: "postgres://localhost/sloppy",
        maxConnections: 257,
    }), /maxConnections must be an integer from 1 to 256/);
    assertThrowsMessage(() => data.postgres.open({
        connectionString: "postgres://ada:secret@localhost/sloppy",
        maxConnections: 256,
    }), /SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE[\s\S]*Feature:[\s\S]*provider\.postgres[\s\S]*postgres:\/\/ada:<redacted>@localhost/);
}

{
    assert.equal(data.sqlserver.provider, "sqlserver");
    assert.equal(data.sqlserver.placeholderStyle, "question");
    assert.equal(data.sqlserver.supports.connectionString, true);
    assert.equal(data.sqlserver.supports.odbc, true);
    assert.equal(data.sqlserver.supports.transactions, true);
    assert.equal(data.sqlserver.supports.pooling, true);
    assert.equal(data.sqlserver.supports.maxPoolConnections, 256);
    assert.equal(data.sqlserver.supports.executionMode, "TRUE_ASYNC");
    assert.equal(data.sqlserver.supports.nativeStdlibBridge, false);
    assert.equal(data.sqlserver.__debug().nativeStdlibBridge, false);
    assert.equal(data.sqlserver.__debug().maxPoolConnections, 256);
    assert.deepEqual(data.sqlserver.supports.parameters, [
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
    ]);
    assert.equal(sql.lower(["a ", " b"], ["x"], {
        placeholderStyle: data.sqlserver.placeholderStyle,
    }).text, "a ? b");
    assert.equal(
        data.sqlserver.redactConnectionString("Driver={ODBC Driver 18 for SQL Server};UID=sa;PWD=secret;Password={top;secret};Access Token=abc"),
        "Driver={ODBC Driver 18 for SQL Server};UID=sa;PWD=<redacted>;Password=<redacted>;Access Token=<redacted>",
    );
    assert.equal(
        data.sqlserver.redactConnectionString("Driver = {ODBC Driver 18 for SQL Server};UID=sa;PWD = secret;Password ={top;secret};Access Token = abc"),
        "Driver = {ODBC Driver 18 for SQL Server};UID=sa;PWD = <redacted>;Password =<redacted>;Access Token = <redacted>",
    );
    const doctor = data.sqlserver.doctor({
        connectionString: "Driver = {ODBC Driver 18 for SQL Server};Server=localhost;PWD = secret",
    });
    assert.equal(doctor.ok, false);
    assert.equal(doctor.provider, "sqlserver");
    assert.equal(doctor.driverManager, "native-check-unavailable");
    assert.equal(doctor.driver, "unchecked");
    assert.equal(doctor.connectionString.includes("secret"), false);
    assertThrowsMessage(() => data.sqlserver.open({}), /connectionString must be a non-empty string/);
    assertThrowsMessage(() => data.sqlserver.open({
        connectionString: "Driver={ODBC Driver 18 for SQL Server};Server=localhost",
        access: "admin",
    }), /access must be read or readwrite/);
    assertThrowsMessage(() => data.sqlserver.open({
        connectionString: "Driver={ODBC Driver 18 for SQL Server};Server=localhost",
        maxConnections: 0,
    }), /maxConnections must be an integer from 1 to 256/);
    assertThrowsMessage(() => data.sqlserver.open({
        connectionString: "Driver={ODBC Driver 18 for SQL Server};Server=localhost",
        maxConnections: 1.5,
    }), /maxConnections must be an integer from 1 to 256/);
    assertThrowsMessage(() => data.sqlserver.open({
        connectionString: "Driver={ODBC Driver 18 for SQL Server};Server=localhost",
        maxConnections: 257,
    }), /maxConnections must be an integer from 1 to 256/);
    assertThrowsMessage(() => data.sqlserver.open({
        connectionString: "Driver={ODBC Driver 18 for SQL Server};Server=localhost;UID=sa;PWD=secret",
        maxConnections: 256,
    }), /SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE[\s\S]*Feature:[\s\S]*provider\.sqlserver[\s\S]*PWD=<redacted>/);
}

{
    const received = [];
    const fakeDb = data.createFakeProvider({
        query(lowered, options) {
            received.push(["query", lowered, options]);
            return [{ id: lowered.parameters[0], name: "Ada" }];
        },
        exec(lowered, options) {
            received.push(["exec", lowered, options]);
            return { affectedRows: 1 };
        },
    });

    const rows = await fakeDb.query`select id from users where id = ${1}`;
    assert.deepEqual(rows, [{ id: 1, name: "Ada" }]);
    assert.equal(received[0][1].text, "select id from users where id = ?");
    assert.deepEqual(received[0][1].parameters, [1]);
    const lowered = sql`select id from users where id = ${3}`;
    assert.deepEqual(await fakeDb.query(lowered, {
        deadline: { remainingMs: () => 1000 },
        signal: { aborted: false },
        timeoutMs: 50,
    }), [{ id: 3, name: "Ada" }]);
    assert.equal(received[1][2].timeoutMs, 50);
    assert.equal(received[1][2].signal.aborted, false);
    assertThrowsMessage(() => fakeDb.query(lowered, {
        deadline: { expired: true },
    }), /SLOPPY_E_DEADLINE_EXCEEDED/);

    const one = await fakeDb.queryOne`select id from users where id = ${2}`;
    assert.deepEqual(one, { id: 2, name: "Ada" });
    assert.deepEqual(await fakeDb.exec`delete from users where id = ${2}`, {
        affectedRows: 1,
    });
    assertThrowsMessage(() => fakeDb.query("select 1"), /tagged template/);
    assertThrowsMessage(() => fakeDb.query(createForgedLoweredQuery()), /tagged template/);
    assertThrowsMessage(() => fakeDb.queryOne(createForgedLoweredQuery()), /tagged template/);
    assertThrowsMessage(() => fakeDb.exec(createForgedLoweredQuery()), /tagged template/);
    assert.equal(data.isQuery(sql`select 1`), true);
}

{
    const broken = data.createFakeProvider({});

    assertThrowsMessage(() => broken.query`select 1`, /method missing[\s\S]*query/);
    assertThrowsMessage(() => broken.queryOne`select 1`, /method missing[\s\S]*queryOne/);
    assertThrowsMessage(() => broken.exec`select 1`, /method missing[\s\S]*exec/);
}

{
    const fakeDb = data.createFakeProvider({
        query(lowered) {
            return [{ text: lowered.text }];
        },
        exec(lowered) {
            return { text: lowered.text, affectedRows: 1 };
        },
    });
    let capturedTx;

    const result = await fakeDb.transaction(async (tx) => {
        capturedTx = tx;
        assert.deepEqual(await tx.exec`insert into users (name) values (${"Ada"})`, {
            text: "insert into users (name) values (?)",
            affectedRows: 1,
        });
        assertThrowsMessage(() => tx.query(createForgedLoweredQuery()), /tagged template/);
        assertThrowsMessage(() => tx.exec(createForgedLoweredQuery()), /tagged template/);
        return "done";
    });

    assert.equal(result, "done");
    assert.deepEqual(fakeDb.__debug().events, ["begin", "commit"]);
    assertThrowsMessage(() => capturedTx.exec`select 1`, /transaction scope is closed/);
    await assertRejectsMessage(() => fakeDb.transaction(async (tx) => {
        await tx.transaction(async () => {});
    }), /nested transactions/);
    assert.deepEqual(fakeDb.__debug().events, ["begin", "commit", "begin", "rollback"]);

    await assertRejectsMessage(() => fakeDb.transaction(async () => {
        await fakeDb.transaction(async () => {});
    }), /nested transactions/);
    assert.deepEqual(fakeDb.__debug().events, [
        "begin",
        "commit",
        "begin",
        "rollback",
        "begin",
        "rollback",
    ]);
}

{
    const fakeDb = data.createFakeProvider({
        exec() {
            return { affectedRows: 1 };
        },
    });

    await assertRejectsMessage(() => fakeDb.transaction(() => {
        throw new Error("boom");
    }), /boom/);
    assert.deepEqual(fakeDb.__debug().events, ["begin", "rollback"]);

    await assertRejectsMessage(() => fakeDb.transaction(async () => {
        throw new Error("async boom");
    }), /async boom/);
    assert.deepEqual(fakeDb.__debug().events, ["begin", "rollback", "begin", "rollback"]);
    await assertRejectsMessage(() => fakeDb.transaction("bad"), /callback must be a function/);
}

{
    const fakeDb = data.createFakeProvider({
        query() {
            return [];
        },
        exec() {
            return { affectedRows: 1 };
        },
    });

    await assertRejectsMessage(() => data.migrations.status(fakeDb, {
        provider: "sqlite",
        path: "migrations/*.sql",
    }), /only supports sqlite, postgres, and sqlserver connections/);
    await assertRejectsMessage(() => data.migrations.status(fakeDb, {
        provider: "sqltie",
        path: "migrations/*.sql",
    }), /provider must be sqlite, postgres, or sqlserver/);
    await assertRejectsMessage(() => data.providerHealth.check(fakeDb), /Sloppy ProviderHealth only supports/);
}

{
    const DataModule = Sloppy.module("data")
        .capabilities((caps) => {
            caps.addDatabase("data.main", {
                provider: "sqlite",
                database: ":memory:",
                access: "readwrite",
            });
        })
        .services((services) => {
            services.addSingleton("data.main", () => data.createFakeProvider({
                query() {
                    return [{ id: 1 }];
                },
                exec() {
                    return { affectedRows: 1 };
                },
            }));
        });

    const app = Sloppy.createBuilder()
        .addModule(DataModule)
        .build();

    const db = app.services.get("data.main");
    assert.deepEqual(await db.query`select id from users`, [{ id: 1 }]);
    assert.equal(app.capabilities.get("data.main").provider, "sqlite");
    assert.equal(app.capabilities.get("data.main").metadata.database, ":memory:");
}

{
    const previousSloppy = globalThis.__sloppy;
    const calls = [];
    let commitShouldFail = false;
    let rollbackShouldFail = false;

    globalThis.__sloppy = {
        data: {
            sqlite: {
                open() {
                    calls.push(["open"]);
                    return { slot: calls.length, generation: 1, kind: "sqlite.connection" };
                },
                exec(handle, text, params) {
                    calls.push(["exec", handle.slot, text, params]);
                    return { affectedRows: 1 };
                },
                query(handle, text, params) {
                    calls.push(["query", handle.slot, text, params]);
                    return [];
                },
                queryOne(handle, text, params) {
                    calls.push(["queryOne", handle.slot, text, params]);
                    return null;
                },
                close(handle) {
                    calls.push(["close", handle.slot]);
                },
                transactionBegin(handle) {
                    calls.push(["begin", handle.slot]);
                },
                transactionCommit(handle) {
                    calls.push(["commit", handle.slot]);
                    if (commitShouldFail) {
                        throw new Error("commit failed");
                    }
                },
                transactionRollback(handle) {
                    calls.push(["rollback", handle.slot]);
                    if (rollbackShouldFail) {
                        throw new Error("rollback failed");
                    }
                },
                transactionExec(handle, text, params) {
                    calls.push(["txExec", handle.slot, text, params]);
                    return { affectedRows: 1 };
                },
                transactionQuery() {
                    return [];
                },
                transactionQueryOne() {
                    return null;
                },
            },
        },
    };

    try {
        const db = data.sqlite.open({
            database: ":memory:",
            capability: "data.main",
        });
        let releaseTransaction;
        const pending = new Promise((resolve) => {
            releaseTransaction = resolve;
        });
        const pendingTransaction = db.transaction(() => pending);
        assertThrowsMessage(() => db.close(), /transaction is active/);
        releaseTransaction("settled");
        assert.equal(await pendingTransaction, "settled");
        assert.equal(db.__debug().transactionActive, false);
        db.close();

        const thenGetterDb = data.sqlite.open({
            database: ":memory:",
            capability: "data.main",
        });
        await assertRejectsMessage(() => thenGetterDb.transaction(() => ({
            get then() {
                throw new Error("then getter failed");
            },
        })), /then getter failed/);
        assert.equal(thenGetterDb.__debug().transactionActive, false);
        thenGetterDb.close();

        const commitFailureDb = data.sqlite.open({
            database: ":memory:",
            capability: "data.main",
        });
        commitShouldFail = true;
        await assertRejectsMessage(() => commitFailureDb.transaction(() => "commit"), /commit failed/);
        assert.equal(commitFailureDb.__debug().closed, true);
        assert.equal(commitFailureDb.__debug().transactionActive, false);
        assertThrowsMessage(() => commitFailureDb.query("select 1"), /sqlite connection is closed/);
        commitShouldFail = false;

        const rollbackFailureDb = data.sqlite.open({
            database: ":memory:",
            capability: "data.main",
        });
        rollbackShouldFail = true;
        await assertRejectsMessage(() => rollbackFailureDb.transaction(async () => {
            throw new Error("original callback error");
        }), /original callback error/);
        assert.equal(rollbackFailureDb.__debug().closed, true);
        assert.equal(rollbackFailureDb.__debug().transactionActive, true);
        rollbackShouldFail = false;
    } finally {
        if (previousSloppy === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previousSloppy;
        }
    }

    assert.deepEqual(calls, [
        ["open"],
        ["begin", 1],
        ["commit", 1],
        ["close", 1],
        ["open"],
        ["begin", 5],
        ["rollback", 5],
        ["close", 5],
        ["open"],
        ["begin", 9],
        ["commit", 9],
        ["close", 9],
        ["open"],
        ["begin", 13],
        ["rollback", 13],
        ["close", 13],
    ]);
}

{
    const SqliteModule = Sloppy.module("data.sqlite")
        .capabilities((caps) => {
            caps.addDatabase("data.main", {
                provider: "sqlite",
                database: ":memory:",
                access: "readwrite",
            });
        })
        .services((services) => {
            services.addSingleton("data.main", () => data.sqlite.open({
                database: ":memory:",
                capability: "data.main",
            }));
        });

    const app = Sloppy.createBuilder()
        .addModule(SqliteModule)
        .build();

    assert.equal(app.capabilities.get("data.main").metadata.database, ":memory:");
    assert.deepEqual(app.__debug().modules[0].services, ["data.main"]);
    assertThrowsMessage(() => app.services.get("data.main"), /SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE[\s\S]*provider\.sqlite/);
}

{
    const PostgresModule = Sloppy.module("data.postgres")
        .capabilities((caps) => {
            caps.addDatabase("data.main", {
                provider: "postgres",
                configKey: "SLOPPY_POSTGRES_TEST_URL",
                access: "readwrite",
            });
        })
        .services((services) => {
            services.addSingleton("data.main", () => data.postgres.open({
                connectionString: "postgres://ada:secret@localhost/sloppy_test",
                maxConnections: 2,
            }));
        });

    const app = Sloppy.createBuilder()
        .addModule(PostgresModule)
        .build();

    assert.equal(app.capabilities.get("data.main").provider, "postgres");
    assert.equal(app.capabilities.get("data.main").metadata.configKey, "SLOPPY_POSTGRES_TEST_URL");
    assert.equal(app.capabilities.get("data.main").metadata.connectionString, undefined);
    assert.deepEqual(app.__debug().modules[0].services, ["data.main"]);
    assertThrowsMessage(() => app.services.get("data.main"), /ada:<redacted>@localhost/);
}

{
    const SqlServerModule = Sloppy.module("data.sqlserver")
        .capabilities((caps) => {
            caps.addDatabase("data.main", {
                provider: "sqlserver",
                configKey: "SLOPPY_SQLSERVER_TEST_CONNECTION_STRING",
                access: "readwrite",
            });
        })
        .services((services) => {
            services.addSingleton("data.main", () => data.sqlserver.open({
                connectionString: "Driver={ODBC Driver 18 for SQL Server};Server=localhost;UID=sa;PWD=secret;TrustServerCertificate=yes",
                maxConnections: 2,
            }));
        });

    const app = Sloppy.createBuilder()
        .addModule(SqlServerModule)
        .build();

    assert.equal(app.capabilities.get("data.main").provider, "sqlserver");
    assert.equal(
        app.capabilities.get("data.main").metadata.configKey,
        "SLOPPY_SQLSERVER_TEST_CONNECTION_STRING",
    );
    assert.equal(app.capabilities.get("data.main").metadata.connectionString, undefined);
    assert.deepEqual(app.__debug().modules[0].services, ["data.main"]);
    assertThrowsMessage(() => app.services.get("data.main"), /PWD=<redacted>/);
}
