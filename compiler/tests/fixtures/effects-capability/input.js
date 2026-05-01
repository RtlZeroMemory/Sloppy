import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";

const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");

function listUsers() {
  return db.query("select id, name from users", []);
}

app.get("/users", () => Results.json(listUsers()));

export default app;
