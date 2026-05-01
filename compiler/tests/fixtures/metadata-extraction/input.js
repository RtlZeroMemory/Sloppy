import { Sloppy, Results, schema } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";

const UserCreate = schema.object({
  name: schema.string().min(1),
  email: schema.string().email().optional(),
  tags: schema.array(schema.string()).optional()
});

const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const host = app.config.getString("Sloppy:Server:Host", "127.0.0.1");

app.post("/users/{id:int}", (ctx) => Results.json({
  id: ctx.route.id,
  search: ctx.query.q,
  agent: ctx.header.userAgent,
  body: ctx.body.json(UserCreate)
})).withName("Users.Create");

app.get("/health", () => Results.text("ok"));

export default app;
