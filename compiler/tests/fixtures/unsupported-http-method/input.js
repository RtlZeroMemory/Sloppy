import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapHead("/", () => Results.text("Hello"));

export default app;
