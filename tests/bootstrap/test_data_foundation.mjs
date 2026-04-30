import assert from "node:assert/strict";

import { Sloppy, data, sql } from "../../stdlib/sloppy/index.js";

function assertThrowsMessage(fn, expected) {
    assert.throws(fn, (error) => {
        assert.match(String(error.message), expected);
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
    assert.equal(data.sqlite.supports.transactionsMode, "native-provider-only");
    assert.equal(data.sqlite.supports.pooling, false);
    assert.equal(data.sqlite.supports.nativeStdlibBridge, false);
    assert.equal(data.sqlite.__debug().nativeStdlibBridge, false);
    assertThrowsMessage(() => data.sqlite.open(":memory:"), /options must be a plain object/);
    assertThrowsMessage(() => data.sqlite.open({}), /database must be a non-empty string/);
    assertThrowsMessage(() => data.sqlite.open({
        database: ":memory:",
        capability: "data.main",
        access: "admin",
    }), /access must be read, write, or readwrite/);
    assertThrowsMessage(() => data.sqlite.open({
        database: ":memory:",
    }), /capability must be a non-empty string/);
    assertThrowsMessage(() => data.sqlite.open({
        database: ":memory:",
        capability: "data.main",
    }), /sqlite provider native bridge unavailable[\s\S]*Provider:[\s\S]*sqlite[\s\S]*Operation:[\s\S]*open/);
    assertThrowsMessage(() => data.sqlite.open({
        path: ":memory:",
        capability: "data.main",
    }), /sqlite provider native bridge unavailable[\s\S]*Provider:[\s\S]*sqlite[\s\S]*Operation:[\s\S]*open/);
    assertThrowsMessage(() => data.sqlite("main"), /sqlite provider native bridge unavailable[\s\S]*Provider:[\s\S]*sqlite[\s\S]*Operation:[\s\S]*open/);
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
                queryOne(handle, text, params) {
                    calls.push(["queryOne", handle.kind, text, params]);
                    return { name: "Ada" };
                },
                close(handle) {
                    calls.push(["close", handle.slot]);
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
        assert.deepEqual(db.query("select name from users", []), [{ name: "Ada" }]);
        assert.deepEqual(db.queryOne(sql`select name from users where id = ${1}`), { name: "Ada" });
        assert.equal(db.__debug().resource.kind, "sqlite.connection");
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
            ["query", 1, "select name from users", []],
            ["queryOne", "sqlite.connection", "select name from users where id = ?", [1]],
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
    assert.equal(data.postgres.supports.pooling, "skeleton");
    assert.equal(data.postgres.supports.nativeStdlibBridge, false);
    assert.equal(data.postgres.__debug().nativeStdlibBridge, false);
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
        connectionString: "postgres://ada:secret@localhost/sloppy",
    }), /postgres provider native bridge unavailable[\s\S]*postgres:\/\/ada:<redacted>@localhost/);
}

{
    assert.equal(data.sqlserver.provider, "sqlserver");
    assert.equal(data.sqlserver.placeholderStyle, "question");
    assert.equal(data.sqlserver.supports.connectionString, true);
    assert.equal(data.sqlserver.supports.odbc, true);
    assert.equal(data.sqlserver.supports.transactions, true);
    assert.equal(data.sqlserver.supports.pooling, "skeleton");
    assert.equal(data.sqlserver.supports.nativeStdlibBridge, false);
    assert.equal(data.sqlserver.__debug().nativeStdlibBridge, false);
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
        connectionString: "Driver={ODBC Driver 18 for SQL Server};Server=localhost;UID=sa;PWD=secret",
    }), /sqlserver provider native bridge unavailable[\s\S]*PWD=<redacted>/);
}

{
    const received = [];
    const fakeDb = data.createFakeProvider({
        query(lowered) {
            received.push(["query", lowered]);
            return [{ id: lowered.parameters[0], name: "Ada" }];
        },
        exec(lowered) {
            received.push(["exec", lowered]);
            return { affectedRows: 1 };
        },
    });

    const rows = await fakeDb.query`select id from users where id = ${1}`;
    assert.deepEqual(rows, [{ id: 1, name: "Ada" }]);
    assert.equal(received[0][1].text, "select id from users where id = ?");
    assert.deepEqual(received[0][1].parameters, [1]);

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
    assertThrowsMessage(() => app.services.get("data.main"), /native bridge unavailable/);
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
