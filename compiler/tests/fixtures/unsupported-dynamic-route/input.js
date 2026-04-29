import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
const route = "/";

app.mapGet(route, () => Results.text("Hello from Sloppy"));

export default app;
