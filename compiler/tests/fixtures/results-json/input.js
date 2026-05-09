import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/json", () => Results.json({ ok: true, tags: ["compiler", "artifact"] }));

export default app;
