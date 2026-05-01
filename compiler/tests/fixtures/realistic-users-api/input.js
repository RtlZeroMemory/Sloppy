import { Sloppy, Results, schema } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";

const UserCreate = schema.object({
  name: schema.string().min(1),
  email: schema.string().email().optional(),
  tags: schema.array(schema.string()).optional()
});

const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
const api = app.group("/api");
const users = api.group("/users");
const host = app.config.getString("Sloppy:Server:Host", "127.0.0.1");

function listUsers() {
  return db.query("select id, name, email from users", []);
}

function createUser(body) {
  db.exec("insert into users (name, email) values (?, ?)", [body.name, body.email]);
  return db.queryOne("select id, name, email from users where id = last_insert_rowid()", []);
}

users.get("/", (ctx) => Results.json({
  q: ctx.query.q,
  users: listUsers()
})).withName("Users.List");

users.post("/", (ctx) => Results.created("/api/users/1", {
  user: createUser(ctx.body.json(UserCreate))
})).withName("Users.Create");

users.get("/{id:int}", (ctx) => Results.json({
  id: ctx.route.id,
  user: db.queryOne("select id, name, email from users where id = ?", [ctx.route.id])
})).withName("Users.Get");

app.get("/health", () => Results.text("ok"));

export default app;
