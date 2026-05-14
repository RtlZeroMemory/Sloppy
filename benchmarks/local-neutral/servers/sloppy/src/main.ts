import { Sloppy, Results, Route, Auth, RequestContext } from "sloppy";

const app = Sloppy.create();

function validUser(body) {
  return body &&
    typeof body.name === "string" &&
    body.name.length > 0 &&
    body.name.length <= 100 &&
    typeof body.email === "string" &&
    body.email.includes("@");
}

app.useStaticFiles({
  requestPath: "/public",
  root: "public",
});

app.use(Auth.apiKey({
  header: "x-api-key",
  configKey: "Auth:ApiKey",
}));

app.get("/health", () => Results.text("ok"));
app.get("/json-small", () => Results.json({ ok: true, message: "hello", count: 3 }));
app.get("/users/{id}", (id: Route<string>) => Results.json({ id, name: "Ada" }));
app.post("/users", (ctx: RequestContext) => {
  const body = ctx.request.json();
  if (!validUser(body)) {
    return Results.status(400, { error: String(body?.name ?? "invalid user") });
  }
  return Results.json({ id: 1, name: body.name, email: body.email });
});
app.get("/private", () => Results.json({ ok: true, sub: "api-key" })).requireAuth();

export default app;
