import { Sloppy, Results, schema } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { healthModule } from "./modules/health";
import { usersModule } from "./modules/users";
import { toUserName } from "./helpers/names";

const UserBody = schema.object({
  name: schema.string().min(2),
});

const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
app.staticFiles("/assets", {
  root: "public",
  cacheControl: "public, max-age=60",
  index: false,
});

const appName = app.config.getString("App:Name", "compiler-mega-app");

app.get("/", () => Results.text(appName)).withName("Root.Index");
app.get("/meta/package/{name}", (ctx) => Results.ok(toUserName(ctx.route.name)))
  .withName("Meta.Package")
  .openapi({ responses: { "200": { description: "Package helper metadata" } } });
app.post("/validate", (ctx) => Results.ok({ schema: "UserBody", body: ctx.body.json(UserBody) }))
  .withName("Meta.Validate");

app.useModule(healthModule);
app.useModule(usersModule);

export default app;
