import { Sloppy, Results } from "../../stdlib/sloppy/index.js";

const app = Sloppy.create();

app.mapGet("/", () => Results.text("Hello from Sloppy"))
    .withName("Hello.Index");

export default app;
