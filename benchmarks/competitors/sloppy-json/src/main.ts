import { Sloppy, Results, Body, Route, Schema } from "sloppy";

const Login = Schema.object({
  username: Schema.string(),
  password: Schema.string(),
});

const Medium = Schema.object({
  name: Schema.string(),
  email: Schema.string(),
  roles: Schema.array(Schema.string()),
  profile: Schema.object({
    active: Schema.boolean(),
    age: Schema.integer(),
    tags: Schema.array(Schema.string()),
  }),
});

const app = Sloppy.create();

function largeList() {
  return Array.from({ length: 256 }, (_, id) => ({
    id,
    name: `user-${id}`,
    active: id % 2 === 0,
  }));
}

app.post("/small", (body: Body<typeof Login>) => {
  return Results.json({ ok: true, echo: body });
}).accepts(Login);

app.post("/medium", (body: Body<typeof Medium>) => {
  return Results.json({ ok: true, echo: body });
}).accepts(Medium);

app.get("/large", () => Results.json({ items: largeList() }));

app.get("/route/{id:int}", (id: Route<number>) => {
  return Results.json({ ok: true, route: `/route/${id}` });
});

export default app;
