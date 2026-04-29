import { Sloppy as S, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/", () => Results.text("ok"));

export default app;
