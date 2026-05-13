import assert from "node:assert/strict";

const previousRuntime = globalThis.__sloppy_runtime;
await import("../../stdlib/sloppy/internal/runtime-classic.js");

try {
    const { column, orm, relation, table, SloppyOrmConcurrencyError, SloppyOrmError } = globalThis.__sloppy_runtime;

    assert.equal(typeof orm.from, "function");
    assert.equal(typeof table, "function");
    assert.equal(typeof column.uuid, "function");
    assert.equal(typeof relation, "function");
    assert.equal(SloppyOrmError.name, "SloppyOrmError");
    assert.equal(SloppyOrmConcurrencyError.name, "SloppyOrmConcurrencyError");

    const Teams = table("teams", {
        id: column.uuid().primaryKey(),
        name: column.text().notNull(),
    });
    const Users = table("users", {
        id: column.uuid().primaryKey(),
        teamId: column.uuid().notNull().references(() => Teams.id),
        email: column.text().notNull().unique(),
        profile: column.json().nullable(),
        version: column.int().notNull().concurrencyToken(),
        deletedAt: column.instant().nullable().softDelete(),
    });

    relation(Teams, ({ many }) => ({
        users: many(Users, {
            local: Teams.id,
            foreign: Users.teamId,
        }),
    }));
    relation(Users, ({ one }) => ({
        team: one(Teams, {
            local: Users.teamId,
            foreign: Teams.id,
        }),
    }));

    assert.deepEqual(Users.metadata.foreignKeys, [{
        column: "teamId",
        foreignTable: "teams",
        foreignColumn: "id",
    }]);
    assert.equal(Users.insertSchema.validate({
        id: "00000000-0000-4000-8000-000000000001",
        teamId: "00000000-0000-4000-8000-000000000002",
        email: "ada@example.com",
        profile: { nested: true },
        version: 1,
    }).ok, true);

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

    const sql = 'select "t0"."id" as "id", "t0"."email" as "email" from "users" "t0" where ("t0"."email" = ?)';
    const calls = [];
    const db = Object.freeze({
        query(sqlOrQuery, paramsOrOptions, options) {
            const call = normalizeProviderCall(sqlOrQuery, paramsOrOptions, options);
            calls.push(["query", call.text, call.parameters, call.options]);
            return call.text === sql ? [{ id: "1", email: "ada@example.com" }] : [];
        },
        __debug() {
            return Object.freeze({ provider: "sqlite", placeholderStyle: "question" });
        },
    });

    const rows = await orm
        .from(Users)
        .where((u) => u.email.eq("ada@example.com"))
        .select((u) => ({ id: u.id, email: u.email }))
        .toList(db);

    assert.deepEqual(rows, [{ id: "1", email: "ada@example.com" }]);
    assert.equal(Object.isFrozen(rows[0]), true);
    assert.deepEqual(calls[0][2], ["ada@example.com"]);

    const joinSql = 'select "t0"."id" as "id", "t0"."teamId" as "teamId", "t0"."email" as "email", "t0"."profile" as "profile", "t0"."version" as "version", "t0"."deletedAt" as "deletedAt", "t1"."id" as "team__id", "t1"."name" as "team__name" from "users" "t0" left join "teams" "t1" on "t0"."teamId" = "t1"."id" where ("t0"."id" = ?) limit 1';
    const joinRows = await orm
        .from(Users)
        .where((u) => u.id.eq("1"))
        .include((u) => u.team)
        .take(1)
        .toList(Object.freeze({
            query(sqlOrQuery, paramsOrOptions, options) {
                const query = normalizeProviderCall(sqlOrQuery, paramsOrOptions, options);
                assert.equal(query.text, joinSql);
                assert.deepEqual(query.parameters, ["1"]);
                return [{
                    id: "1",
                    teamId: "team-1",
                    email: "ada@example.com",
                    profile: null,
                    version: 1,
                    deletedAt: null,
                    team__id: "team-1",
                    team__name: "Core",
                }];
            },
            __debug() {
                return Object.freeze({ provider: "sqlite", placeholderStyle: "question" });
            },
        }));
    assert.equal(joinRows[0].team.name, "Core");
    assert.equal(Object.isFrozen(joinRows[0].team), true);
} finally {
    if (previousRuntime === undefined) {
        delete globalThis.__sloppy_runtime;
    } else {
        globalThis.__sloppy_runtime = previousRuntime;
    }
}
