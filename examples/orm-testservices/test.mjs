import { Results, Sloppy, TestHost, TestServices, column, orm, table } from "../../stdlib/sloppy/index.js";

const Users = table("users", {
  id: column.uuid().primaryKey(),
  email: column.text().notNull().unique(),
});

const app = Sloppy.create();
app.mapGet("/users", async (ctx) => {
  const rows = await orm.from(Users).select((u) => ({ id: u.id, email: u.email })).toList(ctx.db);
  return Results.json(rows);
});

const pg = await TestServices.postgres({
  migrations: "migrations/postgres/*.sql",
});

if (!pg.available) {
  console.log(`${pg.status}: ${pg.reason}`);
} else {
  await TestHost.create(app, {
    providers: {
      main: pg.provider(),
    },
  });
}
