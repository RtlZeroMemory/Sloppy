import { Sloppy, Results, data as db } from "sloppy";

const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));

export default app;
