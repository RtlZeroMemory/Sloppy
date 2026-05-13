import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

import { SloppyOrmError, column, orm, relation, table } from "../../stdlib/sloppy/orm.js";

function assertThrowsMessage(fn, expected) {
    assert.throws(fn, (error) => {
        assert.match(String(error.message), expected);
        return true;
    });
}

function assertThrowsOrmHint(fn, expectedCode, expectedHint) {
    assert.throws(fn, (error) => {
        assert.equal(error instanceof SloppyOrmError, true);
        assert.equal(error.code, expectedCode);
        assert.match(error.hint, expectedHint);
        return true;
    });
}

function goldenText(path) {
    return readFileSync(new URL(path, import.meta.url), "utf8").replace(/\r\n/gu, "\n");
}

async function assertRejectsMessage(fn, expected) {
    await assert.rejects(fn, (error) => {
        assert.match(String(error.message), expected);
        return true;
    });
}

function normalizeProviderCall(sqlOrQuery, paramsOrOptions, options) {
    if (typeof sqlOrQuery === "string") {
        return {
            text: sqlOrQuery,
            parameters: Array.isArray(paramsOrOptions) ? [...paramsOrOptions] : [],
            options: Array.isArray(paramsOrOptions) ? options : paramsOrOptions,
        };
    }
    return {
        text: sqlOrQuery.text,
        parameters: [...sqlOrQuery.parameters],
        options: paramsOrOptions,
    };
}

function createFakeDb(rowsByText = new Map()) {
    const calls = [];
    const db = {
        calls,
        query(sqlOrQuery, paramsOrOptions, options) {
            const call = normalizeProviderCall(sqlOrQuery, paramsOrOptions, options);
            calls.push(["query", call.text, call.parameters, call.options]);
            return rowsByText.get(call.text) ?? [];
        },
        queryOne(sqlOrQuery, paramsOrOptions, options) {
            const call = normalizeProviderCall(sqlOrQuery, paramsOrOptions, options);
            calls.push(["queryOne", call.text, call.parameters, call.options]);
            return (rowsByText.get(call.text) ?? [])[0] ?? null;
        },
        exec(sqlOrQuery, paramsOrOptions, options) {
            const call = normalizeProviderCall(sqlOrQuery, paramsOrOptions, options);
            calls.push(["exec", call.text, call.parameters, call.options]);
            return { affectedRows: 1 };
        },
        transaction(callback) {
            calls.push(["begin"]);
            return Promise.resolve()
                .then(() => callback(db))
                .then((value) => {
                    calls.push(["commit"]);
                    return value;
                }, (error) => {
                    calls.push(["rollback"]);
                    throw error;
                });
        },
        async queryCursor(sqlOrQuery, paramsOrOptions, options) {
            const call = normalizeProviderCall(sqlOrQuery, paramsOrOptions, options);
            calls.push(["cursor", call.text, call.parameters, call.options]);
            let index = 0;
            let closed = false;
            const rows = rowsByText.get(call.text) ?? [];
            return Object.freeze({
                provider: "sqlite",
                mode: "object",
                columnNames: Object.freeze(["id", "email"]),
                columns: Object.freeze([{ name: "id", index: 0 }, { name: "email", index: 1 }]),
                get closed() {
                    return closed;
                },
                close() {
                    closed = true;
                },
                [Symbol.asyncIterator]() {
                    return {
                        next() {
                            if (closed || index >= rows.length) {
                                closed = true;
                                return Promise.resolve({ done: true });
                            }
                            return Promise.resolve({ done: false, value: rows[index++] });
                        },
                        return() {
                            closed = true;
                            return Promise.resolve({ done: true });
                        },
                    };
                },
            });
        },
        __debug() {
            return Object.freeze({ provider: "sqlite", placeholderStyle: "question" });
        },
    };
    return Object.freeze(db);
}

function createAffectedRowsDb(affectedRows) {
    return Object.freeze({
        exec() {
            return { affectedRows };
        },
        __debug() {
            return Object.freeze({ provider: "sqlite", placeholderStyle: "question" });
        },
    });
}

const Teams = table("teams", {
    id: column.uuid().primaryKey(),
    slug: column.text().notNull().unique(),
    name: column.text().notNull(),
    createdAt: column.instant().notNull().defaultNow(),
});

const Users = table("users", {
    id: column.uuid().primaryKey(),
    teamId: column.uuid().notNull().references(() => Teams.id),
    email: column.text().notNull().unique(),
    displayName: column.text().nullable(),
    passwordHash: column.text().notNull().private(),
    version: column.int().notNull().concurrencyToken(),
    deletedAt: column.instant().nullable().softDelete(),
    createdAt: column.instant().notNull().defaultNow(),
});

relation(Users, ({ one }) => ({
    team: one(Teams, {
        local: Users.teamId,
        foreign: Teams.id,
    }),
}));

relation(Teams, ({ many }) => ({
    users: many(Users, {
        local: Teams.id,
        foreign: Users.teamId,
    }),
}));

{
    assert.equal(Object.isFrozen(Users.metadata), true);
    assert.deepEqual(Users.metadata.primaryKey, ["id"]);
    assert.deepEqual(Users.metadata.privateColumns, ["passwordHash"]);
    assert.equal(Users.metadata.softDeleteColumn, "deletedAt");
    assert.equal(Users.metadata.concurrencyTokenColumn, "version");
    assert.deepEqual(Users.metadata.foreignKeys, [{
        column: "teamId",
        foreignTable: "teams",
        foreignColumn: "id",
    }]);

    assertThrowsOrmHint(() => table("bad-name", { id: column.int() }), "SLOPPY_ORM_INVALID_IDENTIFIER", /letters, digits, and underscores/u);
    assertThrowsMessage(() => table("empty", {}), /non-empty plain object/);
    assertThrowsMessage(() => column.instant().notNull().softDelete(), /soft-delete column must be nullable/);
    assertThrowsMessage(() => column.text().default(1), /text default must be a string/);
}

{
    const KitchenSink = table("kitchen_sink", {
        id: column.int().primaryKey(),
        label: column.string().notNull(),
        total: column.decimal().default("0.00"),
        active: column.bool().default(true),
        count: column.integer().default(1),
        weight: column.float().default(1.5),
        externalId: column.bigint().nullable(),
        day: column.date().nullable(),
        payload: column.json().notNull(),
        bytes: column.bytes().nullable(),
        status: column.enum(["active", "archived"]).default("active"),
    });
    assert.equal(KitchenSink.metadata.columns.payload.type, "json");
    assert.equal(KitchenSink.metadata.columns.status.type, "enum");
    const validated = KitchenSink.insertSchema.validate({
        id: 1,
        label: "row",
        payload: { nested: ["value"] },
    });
    assert.equal(validated.ok, true);
    assert.deepEqual(validated.value.payload, { nested: ["value"] });
    assert.equal(KitchenSink.insertSchema.validate({ id: 1, label: "row" }).ok, false);
    assert.equal(KitchenSink.insertSchema.validate({ id: 1, label: "row", payload: undefined }).ok, false);
    assert.equal(KitchenSink.insertSchema.validate({ id: 1, label: "row", payload: { bad: 1n } }).ok, false);
}

{
    const validInsert = Users.insertSchema.validate({
        id: "00000000-0000-4000-8000-000000000001",
        teamId: "00000000-0000-4000-8000-000000000002",
        email: "ada@example.com",
        passwordHash: "hash",
        version: 1,
    });
    assert.equal(validInsert.ok, true);
    assert.equal(Users.insertSchema.validate({ email: "ada@example.com" }).ok, false);

    assert.equal(Users.patchSchema.validate({ displayName: null }).ok, true);
    await assertRejectsMessage(() => Users.updateById(createFakeDb(), "id", { displayName: undefined }), /undefined/);
    await assert.rejects(
        () => Users.updateById(createFakeDb(), "id", { passwordHash: "x" }),
        (error) => {
            assert.equal(error.code, "SLOPPY_ORM_PRIVATE_PATCH");
            assert.match(error.hint, /private columns/u);
            return true;
        },
    );

    assert.deepEqual(Users.public({
        id: "1",
        email: "ada@example.com",
        passwordHash: "secret",
        createdAt: "now",
    }, ["id", "email"]), {
        id: "1",
        email: "ada@example.com",
    });
    assert.deepEqual(Users.public({
        id: "1",
        email: "ada@example.com",
        passwordHash: "secret",
        createdAt: "now",
    }), {
        id: "1",
        email: "ada@example.com",
        createdAt: "now",
    });
    assertThrowsMessage(() => Users.publicSchema(["passwordHash"]), /private/);
}

{
    const db = createFakeDb();
    await Users.insert(db, {
        id: "00000000-0000-4000-8000-000000000001",
        teamId: "00000000-0000-4000-8000-000000000002",
        email: "ada@example.com",
        passwordHash: "hash",
        version: 1,
    }).execute();
    assert.equal(db.calls[0][0], "exec");
    assert.match(db.calls[0][1], /^insert into "users"/u);
    assert.deepEqual(db.calls[0][2], [
        "00000000-0000-4000-8000-000000000001",
        "00000000-0000-4000-8000-000000000002",
        "ada@example.com",
        "hash",
        1,
    ]);

    await Users.updateById(db, "00000000-0000-4000-8000-000000000001", {
        displayName: "Ada",
    }, {
        expected: { version: 1 },
    });
    assert.equal(db.calls[1][0], "exec");
    assert.match(db.calls[1][1], /"version" = "version" \+ 1/u);
    assert.deepEqual(db.calls[1][2], ["Ada", "00000000-0000-4000-8000-000000000001", 1]);

    await Users.softDeleteById(db, "00000000-0000-4000-8000-000000000001");
    assert.match(db.calls[2][1], /"deletedAt" = CURRENT_TIMESTAMP/u);
    await assert.rejects(
        () => Users.updateById(createAffectedRowsDb(0), "00000000-0000-4000-8000-000000000001", {
            displayName: "Ada",
        }, {
            expected: { version: 9 },
        }),
        (error) => error.code === "SLOPPY_ORM_CONCURRENCY_CONFLICT",
    );
    await assert.rejects(
        () => Users.deleteById(createAffectedRowsDb(0), "00000000-0000-4000-8000-000000000001", {
            expected: { version: 9 },
        }),
        (error) => error.code === "SLOPPY_ORM_CONCURRENCY_CONFLICT",
    );

    const edit = Users.edit({
        id: "00000000-0000-4000-8000-000000000001",
        version: 2,
    });
    edit.set("displayName", null);
    assert.deepEqual(edit.patch(), { displayName: null });
    await edit.save(db, { expected: { version: 2 } });
    const NoSoftDelete = table("no_soft_delete", {
        id: column.uuid().primaryKey(),
        name: column.text().notNull(),
    });
    assert.throws(
        () => NoSoftDelete.softDeleteById(db, "00000000-0000-4000-8000-000000000001"),
        (error) => error.code === "SLOPPY_ORM_SOFT_DELETE_UNAVAILABLE",
    );
}

{
    const rows = new Map();
    rows.set(
        'select "t0"."id" as "id", "t0"."email" as "email" from "users" "t0" where ("t0"."email" like ?) order by "t0"."createdAt" desc limit 2 offset 1',
        [{ id: "1", email: "ada@example.com" }],
    );
    const db = createFakeDb(rows);
    const result = await orm
        .from(Users)
        .where((u) => u.email.contains("example"))
        .orderBy((u) => u.createdAt.desc())
        .select((u) => ({ id: u.id, email: u.email }))
        .skip(1)
        .take(2)
        .toList(db);
    assert.deepEqual(result, [{ id: "1", email: "ada@example.com" }]);
    assert.equal(Object.isFrozen(result[0]), true);
    assert.deepEqual(db.calls[0][2], ["%example%"]);
}

{
    const joinSql = 'select "t0"."id" as "id", "t0"."teamId" as "teamId", "t0"."email" as "email", "t0"."displayName" as "displayName", "t0"."passwordHash" as "passwordHash", "t0"."version" as "version", "t0"."deletedAt" as "deletedAt", "t0"."createdAt" as "createdAt", "t1"."id" as "team__id", "t1"."slug" as "team__slug", "t1"."name" as "team__name", "t1"."createdAt" as "team__createdAt" from "users" "t0" left join "teams" "t1" on "t0"."teamId" = "t1"."id" where ("t0"."id" = ?) limit 2';
    const rows = new Map([[
        joinSql,
        [{
            id: "user-1",
            teamId: "team-1",
            email: "ada@example.com",
            displayName: "Ada",
            passwordHash: "secret",
            version: 1,
            deletedAt: null,
            createdAt: "now",
            team__id: "team-1",
            team__slug: "core",
            team__name: "Core",
            team__createdAt: "now",
        }],
    ]]);
    const db = createFakeDb(rows);
    const user = await orm
        .from(Users)
        .where((u) => u.id.eq("user-1"))
        .include((u) => u.team)
        .singleOrDefault(db);

    assert.equal(user.team.slug, "core");
    assert.equal(Object.isFrozen(user.team), true);
    assert.equal(Object.hasOwn(user, "team__id"), false);
    assert.equal(db.calls.length, 1);
    await assert.rejects(
        () => orm.from(Users).include((u) => u.team).cursor(db),
        (error) => error.code === "SLOPPY_ORM_CURSOR_INCLUDE_UNSUPPORTED",
    );
}

{
    const parentSql = 'select "t0"."id" as "id", "t0"."slug" as "slug", "t0"."name" as "name", "t0"."createdAt" as "createdAt" from "teams" "t0" where ("t0"."id" = ?) limit 2';
    const childSql = 'select "t0"."id" as "id", "t0"."teamId" as "teamId", "t0"."email" as "email", "t0"."displayName" as "displayName", "t0"."passwordHash" as "passwordHash", "t0"."version" as "version", "t0"."deletedAt" as "deletedAt", "t0"."createdAt" as "createdAt" from "users" "t0" where (("t0"."teamId" in (?)) and ("t0"."deletedAt" is null)) limit 100';
    const rows = new Map([
        [parentSql, [{ id: "team-1", slug: "core", name: "Core", createdAt: "now" }]],
        [childSql, [{ id: "user-1", teamId: "team-1", email: "ada@example.com", displayName: null, passwordHash: "secret", version: 1, deletedAt: null, createdAt: "now" }]],
    ]);
    const db = createFakeDb(rows);
    const team = await orm
        .from(Teams)
        .where((t) => t.id.eq("team-1"))
        .include((t) => t.users.where((u) => u.deletedAt.isNull()).take(100))
        .singleOrDefault(db);

    assert.equal(team.users.length, 1);
    assert.equal(team.users[0].email, "ada@example.com");
    assert.equal(Object.isFrozen(team.users), true);
}

{
    const sqliteSql = orm.migrations.script([Teams, Users], { provider: "sqlite" });
    assert.match(sqliteSql, /create table "teams"/u);
    assert.match(sqliteSql, /references "teams" \("id"\)/u);
    assert.match(sqliteSql, /create index "ix_users_deletedAt" on "users" \("deletedAt"\);/u);
    const postgresSql = orm.migrations.script(Users, { provider: "postgres" });
    assert.match(postgresSql, /"id" uuid primary key/u);
    assert.doesNotMatch(postgresSql, /"teamId" uuid not null references/u);
    assert.match(postgresSql, /alter table "users" add constraint "fk_users_teamId_teams_id" foreign key \("teamId"\) references "teams" \("id"\);/u);
    const sqlServerSql = orm.migrations.script(Users, { provider: "sqlserver" });
    assert.match(sqlServerSql, /create table \[users\]/u);
    assert.doesNotMatch(sqlServerSql, /\[teamId\] uniqueidentifier not null references/u);
    assert.match(sqlServerSql, /alter table \[users\] add constraint \[fk_users_teamId_teams_id\] foreign key \(\[teamId\]\) references \[teams\] \(\[id\]\);/u);
    assert.match(sqlServerSql, /create index \[ix_users_deletedAt\] on \[users\] \(\[deletedAt\]\);/u);
    const childFirstPostgresSql = orm.migrations.script([Users, Teams], { provider: "postgres" });
    assert.ok(childFirstPostgresSql.indexOf('create table "teams"') < childFirstPostgresSql.indexOf('create table "users"'));
    assert.ok(childFirstPostgresSql.indexOf('alter table "users" add constraint') > childFirstPostgresSql.indexOf('create table "users"'));
    assert.equal(orm.migrations.script(Users, { provider: "sqlite" }), goldenText("../golden/orm/sql/users-create.sqlite.sql"));
    assert.equal(postgresSql, goldenText("../golden/orm/sql/users-create.postgres.sql"));
    assert.equal(sqlServerSql, goldenText("../golden/orm/sql/users-create.sqlserver.sql"));

    const teamsSnapshot = orm.migrations.snapshot(Teams);
    const usersSnapshot = orm.migrations.snapshot([Users, Teams]);
    assert.equal(teamsSnapshot.format, "sloppy.orm.snapshot.v1");
    assert.equal(teamsSnapshot.tables[0].name, "teams");
    assert.match(teamsSnapshot.checksum, /^[0-9a-f]{8}$/u);
    assert.notEqual(teamsSnapshot.checksum, usersSnapshot.checksum);

    const createDiff = orm.migrations.diff(teamsSnapshot, [Teams, Users], { provider: "postgres" });
    assert.equal(createDiff.provider, "postgres");
    assert.equal(createDiff.destructive, false);
    assert.match(createDiff.sql, /create table "users"/u);
    assert.equal(createDiff.snapshot.checksum, usersSnapshot.checksum);

    const UsersWithLogin = table("users", {
        id: column.uuid().primaryKey(),
        teamId: column.uuid().notNull().references(() => Teams.id),
        email: column.text().notNull().unique(),
        displayName: column.text().nullable(),
        passwordHash: column.text().notNull().private(),
        version: column.int().notNull().concurrencyToken(),
        deletedAt: column.instant().nullable().softDelete(),
        createdAt: column.instant().notNull().defaultNow(),
        lastLoginAt: column.instant().nullable().index(),
    });
    const addColumnDiff = orm.migrations.diff(usersSnapshot, [Teams, UsersWithLogin], { provider: "sqlite" });
    assert.match(addColumnDiff.sql, /alter table "users" add "lastLoginAt" text;/u);
    assert.match(addColumnDiff.sql, /create index "ix_users_lastLoginAt"/u);

    assertThrowsMessage(
        () => orm.migrations.diff(usersSnapshot, [Teams], { provider: "sqlite" }),
        /destructive changes/u,
    );
}

{
    const throwingDb = (error) => Object.freeze({
        exec() {
            throw error;
        },
        query() {
            throw error;
        },
        queryOne() {
            throw error;
        },
        __debug() {
            return Object.freeze({ provider: "sqlite", placeholderStyle: "question" });
        },
    });
    await assert.rejects(
        () => Users.insert(throwingDb(Object.assign(new Error("UNIQUE constraint failed: users.email"), { code: "SQLITE_CONSTRAINT_UNIQUE" })), {
            id: "00000000-0000-4000-8000-000000000001",
            teamId: "00000000-0000-4000-8000-000000000002",
            email: "ada@example.com",
            passwordHash: "hash",
            version: 1,
        }).execute(),
        (error) => {
            assert.equal(error instanceof SloppyOrmError, true);
            assert.equal(error.code, "SLOPPY_ORM_UNIQUE_VIOLATION");
            assert.equal(error.details.table, "users");
            assert.match(error.hint, /unique value/u);
            return true;
        },
    );
    await assert.rejects(
        () => Users.updateById(throwingDb(Object.assign(new Error("insert or update on table violates foreign key constraint"), { code: "23503" })), "id", { displayName: "Ada" }),
        (error) => {
            assert.equal(error.code, "SLOPPY_ORM_FOREIGN_KEY_VIOLATION");
            assert.match(error.hint, /referenced parent row/u);
            return true;
        },
    );
    await assert.rejects(
        () => Users.deleteById(throwingDb(Object.assign(new Error("Cannot insert the value NULL into column"), { number: 515 })), "id"),
        (error) => {
            assert.equal(error.code, "SLOPPY_ORM_NOT_NULL_VIOLATION");
            assert.match(error.hint, /required columns/u);
            return true;
        },
    );
}

{
    const rows = new Map([
        ['select "t0"."id" as "id", "t0"."email" as "email" from "users" "t0" order by "t0"."id" asc', [
            { id: "1", email: "a@example.com" },
            { id: "2", email: "b@example.com" },
        ]],
    ]);
    const db = createFakeDb(rows);
    const cursor = await orm
        .from(Users)
        .select((u) => ({ id: u.id, email: u.email }))
        .orderBy((u) => u.id.asc())
        .cursor(db, { batchSize: 512 });
    const seen = [];
    for await (const row of cursor) {
        seen.push(row.email);
        break;
    }
    assert.deepEqual(seen, ["a@example.com"]);
    assert.equal(cursor.closed, true);

    await assertRejectsMessage(
        () => orm.from(Users).cursor(db, { batchSize: 0 }),
        /batchSize/,
    );
}

{
    const rows = new Map([
        ['select "t0"."id" as "id", "t0"."email" as "email" from "users" "t0" order by "t0"."id" asc', [
            { id: "1", email: "a@example.com" },
            { id: "2", email: "b@example.com" },
            { id: "3", email: "c@example.com" },
        ]],
    ]);
    const db = createFakeDb(rows);
    const cursor = await orm
        .from(Users)
        .select((u) => ({ id: u.id, email: u.email }))
        .orderBy((u) => u.id.asc())
        .cursor(db, { maxRows: 2 });
    const seen = [];
    for await (const row of cursor) {
        seen.push(row.email);
    }
    assert.deepEqual(seen, ["a@example.com", "b@example.com"]);
    assert.equal(cursor.closed, true);
}

{
    const rows = new Map([
        ['select "t0"."id" as "id", "t0"."email" as "email" from "users" "t0" order by "t0"."id" asc', [
            { id: "1", email: "a@example.com" },
            { id: "2", email: "b@example.com" },
        ]],
    ]);
    const db = createFakeDb(rows);
    const cursor = await orm
        .from(Users)
        .select((u) => ({ id: u.id, email: u.email }))
        .orderBy((u) => u.id.asc())
        .cursor(db);
    const stream = orm.stream.ndjson(cursor, (row) => ({ email: row.email }));
    assert.equal(stream.contentType, "application/x-ndjson; charset=utf-8");
    assert.deepEqual(stream.selected, ["id", "email"]);
    const chunks = [];
    for await (const chunk of stream) {
        chunks.push(chunk);
    }
    assert.deepEqual(chunks, [
        "{\"email\":\"a@example.com\"}\n",
        "{\"email\":\"b@example.com\"}\n",
    ]);
    assert.equal(cursor.closed, true);
}

{
    const rows = new Map();
    rows.set(
        "select [t0].[id] as [id], [t0].[email] as [email] from [users] [t0] where ([t0].[email] = ?) order by [t0].[createdAt] desc offset 1 rows fetch next 2 rows only",
        [{ id: "1", email: "ada@example.com" }],
    );
    const db = createFakeDb(rows);
    const result = await orm
        .from(Users)
        .where((u) => u.email.eq("ada@example.com"))
        .orderBy((u) => u.createdAt.desc())
        .select((u) => ({ id: u.id, email: u.email }))
        .skip(1)
        .take(2)
        .toList(db, { provider: "sqlserver" });
    assert.deepEqual(result, [{ id: "1", email: "ada@example.com" }]);
    assert.deepEqual(db.calls[0][2], ["ada@example.com"]);
}

{
    const rows = new Map();
    rows.set(
        'select "t0"."id" as "id" from "users" "t0" where (lower("t0"."email") like lower(?))',
        [{ id: "1" }],
    );
    const db = createFakeDb(rows);
    const result = await orm
        .from(Users)
        .where((u) => u.email.ilike("ADA@EXAMPLE.COM"))
        .select((u) => ({ id: u.id }))
        .toList(db);
    assert.deepEqual(result, [{ id: "1" }]);
    assert.deepEqual(db.calls[0][2], ["ADA@EXAMPLE.COM"]);
}

{
    const rows = new Map();
    rows.set(
        "select [t0].[id] as [id] from [users] [t0] where (lower([t0].[email]) like lower(?))",
        [{ id: "1" }],
    );
    const db = createFakeDb(rows);
    const result = await orm
        .from(Users)
        .where((u) => u.email.ilike("ADA@EXAMPLE.COM"))
        .select((u) => ({ id: u.id }))
        .toList(db, { provider: "sqlserver" });
    assert.deepEqual(result, [{ id: "1" }]);
}

{
    const returned = [{
        id: "00000000-0000-4000-8000-000000000001",
        teamId: "00000000-0000-4000-8000-000000000002",
        email: "ada@example.com",
        displayName: null,
        version: 1,
        deletedAt: null,
        createdAt: "now",
    }];
    const rows = new Map([[
        "insert into [users] ([id], [teamId], [email], [passwordHash], [version]) output inserted.[id], inserted.[teamId], inserted.[email], inserted.[displayName], inserted.[version], inserted.[deletedAt], inserted.[createdAt] values (?, ?, ?, ?, ?)",
        returned,
    ]]);
    const db = createFakeDb(rows);
    const created = await Users.insert(db, {
        id: "00000000-0000-4000-8000-000000000001",
        teamId: "00000000-0000-4000-8000-000000000002",
        email: "ada@example.com",
        passwordHash: "hash",
        version: 1,
    }).returning({ provider: "sqlserver" });
    assert.deepEqual(created, returned[0]);
}

{
    const rows = new Map();
    rows.set(
        'select "t0"."id" as "id" from "users" "t0" where (("t0"."email" = $1) and "t0"."version" between $2 and $3)',
        [{ id: "1" }],
    );
    const db = createFakeDb(rows);
    const result = await orm
        .from(Users)
        .where((u, { and, sql }) => and(
            u.email.eq("ada@example.com"),
            sql.postgres`"t0"."version" between ${1} and ${3}`,
        ))
        .select((u) => ({ id: u.id }))
        .toList(db, { provider: "postgres" });
    assert.deepEqual(result, [{ id: "1" }]);
    assert.deepEqual(db.calls[0][2], ["ada@example.com", 1, 3]);
}

{
    const db = createFakeDb(new Map([["select id from users where email = ?", [{ id: "1" }]]]));
    const rows = await orm.query(db, orm.sql`select id from users where email = ${"ada@example.com"}`);
    assert.deepEqual(rows, [{ id: "1" }]);
    assert.deepEqual(db.calls[0][2], ["ada@example.com"]);
    await assertRejectsMessage(() => orm.query(db, orm.sql.postgres`select 1`), /cannot run/);
    await assert.rejects(
        () => orm.from(Users).where((u, { sql }) => u.email.eq(sql.postgres`select email from users`)).toList(db),
        (error) => error.code === "SLOPPY_ORM_PROVIDER_SQL_MISMATCH",
    );
}
