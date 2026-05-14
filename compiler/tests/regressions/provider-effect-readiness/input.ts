import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";

const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");

app.get("/ready", async () => {
  const row = await db.queryOne("select 1 as ok", []);
  return Results.ok({ ok: row?.ok === 1 });
});

export default app;
