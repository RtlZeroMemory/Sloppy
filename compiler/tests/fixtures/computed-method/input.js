import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
const method = "mapGet";

app[method]("/", () => Results.text("Hello"));

export default app;
