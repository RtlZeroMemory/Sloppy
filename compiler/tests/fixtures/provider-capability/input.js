import { Sloppy, Results, data } from "sloppy";

const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("users.db", {
  provider: "sqlite",
  database: ":memory:",
  access: "readwrite"
});

const app = builder.build();

app.mapGet("/users", () => Results.json({ ok: true }));

export default app;
