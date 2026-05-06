import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
const route = "/users/:id";

app.mapGet(route, () => Results.text("unsupported"));

export default app;
