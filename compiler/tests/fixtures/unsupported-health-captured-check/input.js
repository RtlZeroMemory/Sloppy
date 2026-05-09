import { Sloppy, Results } from "sloppy";

const ready = true;
const app = Sloppy.create();
app.mapHealthChecks({
  checks: [{ name: "captured", check: () => ready }]
});
app.get("/", () => Results.ok({ ok: true }));

export default app;
