import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/health", () => Results.text("ok"));
app.get("/version", () => Results.ok({ version: "seed" }));

export default app;
