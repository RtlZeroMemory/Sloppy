import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

function queryOne() {
  return { ok: true };
}

app.get("/shadow", () => Results.ok(queryOne()));

export default app;
