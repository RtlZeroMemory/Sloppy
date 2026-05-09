import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
app.use((ctx, next) => next());
app.get("/", () => Results.ok({ ok: true }));

export default app;
