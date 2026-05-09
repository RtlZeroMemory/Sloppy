import { Sloppy, Results, Testing } from "sloppy";

const app = Sloppy.create();
app.get("/", () => Results.ok({ ok: true }));

export default app;
