import { Sloppy, Results } from "sloppy";
import { isUserName, normalizeName } from "validator-lite";

const app = Sloppy.create();

app.get("/users/{name}", (ctx) => {
  const name = normalizeName(ctx.route.name);
  return Results.ok({ name, valid: isUserName(name) });
});

export default app;
