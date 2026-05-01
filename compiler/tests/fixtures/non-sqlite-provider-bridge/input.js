import { Sloppy, Results } from "sloppy";

const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.analytics", {
  provider: "postgres",
  access: "read",
  database: "analytics"
});

const app = builder.build();
const analytics = app.provider("postgres:analytics");

app.get("/analytics", () => Results.json(analytics.query("select id from events", [])));

export default app;
