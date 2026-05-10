import { Sloppy, Results, Body, Query, RequestContext, RequestId, Route } from "sloppy";

const app = Sloppy.create();
const payload64kb = "x".repeat(64 * 1024);
type EchoBody = { name: string; count: number };

app.use(RequestId.defaults({ header: "x-request-id", responseHeader: true, trustIncoming: true }));

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
