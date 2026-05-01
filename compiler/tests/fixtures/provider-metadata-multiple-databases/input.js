import { Sloppy, Results } from "sloppy";

const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.analytics", {
  provider: "postgres",
  access: "read",
  database: "analytics"
});
builder.capabilities.addDatabase("data.reporting", {
  provider: "sqlserver",
  access: "readwrite",
  database: "reporting"
});

const app = builder.build();
const analytics = app.provider("postgres:analytics");
const reporting = app.provider("sqlserver:reporting");

app.get("/metadata", () => Results.json({
  analytics: "postgres",
  reporting: "sqlserver"
}));

export default app;
