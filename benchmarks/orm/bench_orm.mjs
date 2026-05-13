import { performance } from "node:perf_hooks";

import { column, orm, relation, table } from "../../stdlib/sloppy/orm.js";

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
    team: one(Teams, { local: Users.teamId, foreign: Teams.id }),
}));

relation(Teams, ({ many }) => ({
    users: many(Users, { local: Teams.id, foreign: Users.teamId }),
}));

function createDb() {
    return Object.freeze({
        query() {
            return [];
        },
        queryOne() {
            return { count: 0 };
        },
        exec() {
            return { affectedRows: 1 };
        },
        queryCursor() {
            throw new Error("cursor is outside this benchmark");
        },
        __debug() {
            return Object.freeze({ provider: "sqlite", placeholderStyle: "question" });
        },
    });
}

async function measure(name, iterations, callback) {
    const start = performance.now();
    for (let index = 0; index < iterations; index += 1) {
        await callback(index);
    }
    const elapsedMs = performance.now() - start;
    return Object.freeze({
        name,
        iterations,
        elapsedMs,
        opsPerSecond: iterations / (elapsedMs / 1000),
    });
}

const db = createDb();
const previousSnapshot = orm.migrations.snapshot(Teams);
const nextTables = [Teams, Users];

const results = [
    await measure("query-builder-select", 10_000, () =>
        orm
            .from(Users)
            .where((u) => u.email.contains("@example.com"))
            .orderBy((u) => u.createdAt.desc())
            .select((u) => ({ id: u.id, email: u.email }))
            .take(20)
            .toList(db)),
    await measure("migration-diff", 2_000, () =>
        orm.migrations.diff(previousSnapshot, nextTables, { provider: "sqlite" })),
    await measure("one-include-join", 5_000, () =>
        orm
            .from(Users)
            .include((u) => u.team)
            .take(10)
            .toList(db)),
];

console.log(JSON.stringify({
    benchmark: "sloppy.orm.local",
    measuredAt: new Date().toISOString(),
    results,
}, null, 2));
