import { Sloppy, Results } from "sloppy";
import fs from "node:fs";

const app = Sloppy.create();
app.mapGet("/", () => Results.text("Hello"));
export default app;
