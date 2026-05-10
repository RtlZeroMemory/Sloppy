import { Sloppy, Results, Body, Query, RequestContext, RequestId, Route } from "sloppy";

function auditMiddleware(ctx, next) {
  return next();
}

function quietRequestLogging(ctx, next) {
  return next();
}

const builder = Sloppy.createBuilder();
builder.services.addSingleton("BenchClock", () => ({ now: "2026-05-10T00:00:00Z" }));

const app = builder.build();
const payload64kb = "x".repeat(64 * 1024);
type EchoBody = { name: string; count: number };

app.use(RequestId.defaults({ header: "x-request-id", responseHeader: true, trustIncoming: true }));
app.use(quietRequestLogging);
app.use(auditMiddleware);
app.useCors({
  origins: ["https://app.example.com"],
  headers: ["content-type", "x-request-id"],
  exposedHeaders: ["x-request-id"],
  credentials: true,
  maxAgeSeconds: 600,
});

app.get("/health", () => Results.text("ok"));
app.get("/json", () => Results.json({ message: "hello", ok: true, count: 42 }));
app.get("/users/{id:int}", (id: Route<number>) => Results.json({ id, name: "Ada Lovelace" }));
app.get("/search", (q: Query<string>, page: Query<number>, limit: Query<number>) => Results.json({
  q,
  page,
  limit,
  results: [],
}));
app.post("/echo", (body: Body<EchoBody>) => Results.json({ name: body.name, count: body.count }));
app.get("/middleware", (ctx: RequestContext) => Results.json({ requestId: ctx.requestId }));
app.get("/payload/64kb", () => Results.text(payload64kb));

export default app;
