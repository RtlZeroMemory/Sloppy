import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/", ({ route }) => Results.text("ok"));

export default app;
