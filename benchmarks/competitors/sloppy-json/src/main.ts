import { Sloppy, Results, Route } from "sloppy";

const app = Sloppy.create();

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

app.get("/dynamic-json", () => {
  return Results.json({ ok: true, mode: `dynamic-${Math.trunc(Math.random() * 0)}` });
});

export default app;
