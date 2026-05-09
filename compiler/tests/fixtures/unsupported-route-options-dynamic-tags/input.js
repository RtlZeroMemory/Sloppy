import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
const tags = ["users"];
app.get("/users", { tags }, () => Results.ok({ ok: true }));

export default app;
