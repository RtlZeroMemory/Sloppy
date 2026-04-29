import { Sloppy, Results } from "sloppy";

const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("users.db", {
  provider: "sqlite",
  access: "readwrite",
  connectionString: "file:users.db"
});

const app = builder.build();
app.mapGet("/users", () => Results.json({ ok: true }));

export default app;
