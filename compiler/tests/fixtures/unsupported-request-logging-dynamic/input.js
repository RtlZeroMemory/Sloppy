import { Sloppy, Results, RequestLogging } from "sloppy";

const includeRoute = true;
const app = Sloppy.create();
app.use(RequestLogging.defaults({ includeRoute }));
app.get("/", () => Results.ok({ ok: true }));

export default app;
