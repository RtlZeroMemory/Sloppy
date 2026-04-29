import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
const message = "ok";

app.mapGet("/", () => Results.text(message));

export default app;
