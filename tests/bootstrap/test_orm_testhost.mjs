import assert from "node:assert/strict";

import { Sloppy } from "../../stdlib/sloppy/app.js";
import { orm, table, column } from "../../stdlib/sloppy/orm.js";
import { Results } from "../../stdlib/sloppy/results.js";
import { TestHost } from "../../stdlib/sloppy/testing.js";

const Users = table("users", {
    id: column.uuid().primaryKey(),
    email: column.text().notNull().unique(),
    displayName: column.text().nullable(),
    passwordHash: column.text().notNull().private(),
});

const expectedSql = 'select "t0"."id" as "id", "t0"."email" as "email", "t0"."displayName" as "displayName" from "users" "t0" where ("t0"."id" = ?) limit 2';
function normalizeSqlFixture(sql) {
    return sql.replace(/\s+/gu, " ").trim().toLowerCase();
}

const calls = [];
const db = Object.freeze({
    query(sql, params, options) {
        calls.push(["query", sql, [...params], options]);
        return normalizeSqlFixture(sql) === normalizeSqlFixture(expectedSql)
            ? [{ id: "00000000-0000-4000-8000-000000000001", email: "ada@example.com", displayName: "Ada" }]
            : [];
    },
    __debug() {
        return Object.freeze({ provider: "sqlite", placeholderStyle: "question" });
    },
});

const app = Sloppy.create();
app.get("/users/{id:uuid}", async (ctx) => {
    const user = await orm
        .from(Users)
        .where((u) => u.id.eq(ctx.route.id))
        .select((u) => ({ id: u.id, email: u.email, displayName: u.displayName }))
        .singleOrDefault(ctx.db);

    if (user === null) {
        return Results.notFound();
    }

    return Results.json(user);
})
    .returns(200, Users.publicSchema(["id", "email", "displayName"]));

const host = await TestHost.create(app, {
    providers: {
        main: db,
    },
});

try {
    const response = await host.get("/users/00000000-0000-4000-8000-000000000001");
    response.expectStatus(200).expectJson({
        id: "00000000-0000-4000-8000-000000000001",
        email: "ada@example.com",
        displayName: "Ada",
    });
    assert.deepEqual(calls[0][2], ["00000000-0000-4000-8000-000000000001"]);
    const openapi = await host.openapi();
    assert.equal(openapi.paths["/users/{id}"].get.responses["200"].description, "OK");
} finally {
    await host.close();
}
