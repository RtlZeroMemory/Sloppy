import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapPost("/", () => Results.text("Hello"));

export default app;
