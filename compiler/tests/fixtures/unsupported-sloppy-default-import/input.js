import Sloppy from "sloppy";
import { Results } from "sloppy";

const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));

export default app;
