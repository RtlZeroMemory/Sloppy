import { Results, Sloppy } from "sloppy";
import { column, orm, relation, table } from "sloppy/orm";

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

const app = Sloppy.create();

app.post("/teams/{teamId:uuid}/users", async (ctx) => {
    const input = await ctx.body.json(
        Users.insertSchema.pick("email", "displayName", "passwordHash"),
    );

    const created = await orm.transaction(ctx.db, async (tx) => Users.insert(tx, {
        id: crypto.randomUUID(),
        teamId: ctx.params.teamId,
        email: input.email,
        displayName: input.displayName ?? null,
        passwordHash: input.passwordHash,
        version: 1,
    }).returning());

    return Results.json(
        Users.public(created, ["id", "email", "displayName", "createdAt"]),
        { status: 201 },
    );
})
    .accepts(Users.insertSchema.pick("email", "displayName", "passwordHash"))
    .returns(201, Users.publicSchema(["id", "email", "displayName", "createdAt"]));

app.get("/teams/{teamId:uuid}", async (ctx) => {
    const team = await orm
        .from(Teams)
        .where((t) => t.id.eq(ctx.params.teamId))
        .include((t) => t.users.where((u) => u.deletedAt.isNull()).take(100))
        .singleOrDefault(ctx.db);

    if (team === null) {
        return Results.notFound();
    }

    return Results.json({
        id: team.id,
        slug: team.slug,
        name: team.name,
        createdAt: team.createdAt,
        users: team.users.map((user) =>
            Users.public(user, ["id", "email", "displayName", "createdAt"])),
    });
});

export { Teams, Users };
export default app;
