import { Results, Sloppy, column, relation, table } from "sloppy";

const Teams = table("teams", {
  id: column.uuid().primaryKey(),
  slug: column.text().notNull().unique(),
  name: column.text().notNull(),
});

const Users = table("users", {
  id: column.uuid().primaryKey(),
  teamId: column.uuid().notNull().references(() => Teams.id),
  email: column.text().notNull().unique(),
  deletedAt: column.instant().nullable().softDelete(),
});

relation(Users, ({ one }) => ({
  team: one(Teams, {
    local: Users.teamId,
    foreign: Teams.id,
  }),
}));

const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.main", {
  provider: "sqlite",
  database: "data/app.db",
  access: "readwrite",
});

const app = builder.build();

app.mapGet("/users", () => Results.json([]));

export default app;
