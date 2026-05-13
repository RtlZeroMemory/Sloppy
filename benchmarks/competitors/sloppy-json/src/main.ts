import { Sloppy, Results, Body, Header, Query, RequestContext, Route, Service } from "sloppy";

const app = Sloppy.create();

type LoginPayload = {
  username: string;
  password: string;
};

type MediumPayload = {
  name: string;
  email: string;
  roles: string[];
  profile: {
    active: boolean;
    age: number;
    tags: string[];
  };
};

type BenchService = {
  label: string;
};

app.services.addSingleton("BenchService", () => ({
  label: "bench-service",
}));

function largeList() {
  return Array.from({ length: 256 }, (_, id) => ({
    id,
    name: `user-${id}`,
    active: id % 2 === 0,
  }));
}

app.get("/large", () => Results.json({ items: largeList() }));

app.get("/route/{id:int}", (id: Route<number>) => {
  return Results.json({ ok: true, route: `/route/${id}` });
});

app.get("/static-json", () => Results.json({ ok: true, mode: "static" }));

app.get("/static-text", () => Results.text("ok\n"));

app.get("/static-status", () => Results.status(204));

app.get("/static-problem", () =>
  Results.problem(
    { status: 400, title: "Static problem", code: "SLOPPY_E_STATIC_PROBLEM" },
    { status: 400 },
  ));

app.get("/dynamic-json", (ctx: RequestContext) => {
  return Results.json({ ok: true, mode: ctx.request.method === "GET" ? "dynamic-0" : "bad" });
});

app.get("/dynamic-text", (ctx: RequestContext) => {
  return Results.text(ctx.request.method === "GET" ? "dynamic-text\n" : "bad\n");
});

app.get("/dynamic-async", async (ctx: RequestContext) => {
  const mode = await Promise.resolve(ctx.request.method === "GET" ? "async-dynamic" : "bad");
  return Results.json({ ok: true, mode });
});

app.get("/ctx-query", (q: Query<string>) => Results.json({ ok: true, query: q }));

app.get("/ctx-headers", (trace: Header<"x-trace">) => Results.json({ ok: true, trace }));

app.post("/small", (input: Body<LoginPayload>) => Results.json({ ok: true, echo: input }));

app.post("/medium", (input: Body<MediumPayload>) => Results.json({ ok: true, echo: input }));

app.get("/ctx-services", (service: Service<BenchService>) =>
  Results.json({ ok: true, service: service.label }));

app.get("/plain-object", (ctx: RequestContext) => ({
  ok: true,
  mode: ctx.request.method === "GET" ? "plain-object" : "bad",
}));

app.get("/exception", () => {
  throw new Error("bench exception");
});

export default app;
