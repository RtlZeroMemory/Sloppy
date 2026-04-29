import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/", (ctx, extra) => Results.text("ok"));

export default app;
