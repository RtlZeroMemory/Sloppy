import { Sloppy, Results, column, orm, relation, table } from "sloppy";

const Teams = table("teams", {
    id: column.uuid().primaryKey(),
    slug: column.text().notNull().unique(),
    name: column.text().notNull(),
});

const Users = table("users", {
    id: column.uuid().primaryKey(),
    teamId: column.uuid().notNull().references(() => Teams.id),
    email: column.text().notNull().unique(),
    displayName: column.text().nullable(),
    deletedAt: column.instant().nullable().softDelete(),
});

relation(Teams, ({ many }) => ({
    users: many(Users, { local: Teams.id, foreign: Users.teamId }),
}));

relation(Users, ({ one }) => ({
    team: one(Teams, { local: Users.teamId, foreign: Teams.id }),
}));

const app = Sloppy.create();

app.get("/teams/{teamId:uuid}", async (ctx) => {
    const team = await orm
        .from(Teams)
        .where((t) => t.id.eq(ctx.params.teamId))
        .include((t) => t.users.where((u) => u.deletedAt.isNull()).take(100))
        .singleOrDefault(ctx.db);

    return team === null ? Results.notFound() : Results.json(team);
});

app.get("/users/{userId:uuid}", async (ctx) => {
    const user = await orm
        .from(Users)
        .where((u) => u.id.eq(ctx.params.userId))
        .include((u) => u.team)
        .singleOrDefault(ctx.db);

    return user === null ? Results.notFound() : Results.json(user);
});

export default app;
