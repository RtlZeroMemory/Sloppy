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
}
