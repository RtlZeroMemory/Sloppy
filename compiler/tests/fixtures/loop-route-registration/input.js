import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
const routes = ["/"];

for (const route of routes) {
  app.mapGet(route, () => Results.text("Hello"));
}

export default app;
