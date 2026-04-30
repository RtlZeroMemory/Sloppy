import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/async", async () => Results.json({ ok: true }));

export default app;
