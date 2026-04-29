import { Sloppy, Results } from "sloppy";

const builder = Sloppy.createBuilder();
const app = builder.build();

app.mapGet("/", () => Results.text("Hello from Sloppy"));

export default app;
