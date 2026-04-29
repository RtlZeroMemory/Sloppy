import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/", (): string => Results.text("ok"));

export default app;
