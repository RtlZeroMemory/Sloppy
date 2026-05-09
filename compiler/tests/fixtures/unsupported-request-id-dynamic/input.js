import { Sloppy, Results, RequestId } from "sloppy";

const app = Sloppy.create();
app.use(RequestId.defaults({ generator: () => "req-1" }));
app.get("/", () => Results.ok({ ok: true }));

export default app;
