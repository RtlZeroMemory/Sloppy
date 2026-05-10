import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
const staticRoute = "/health";

function routeFor(name: string) {
  return `/${name}`;
}

app.mapGet(staticRoute, () => Results.text("ok")).withName("Health.Get");
app.mapGet(routeFor("users"), () => Results.text("dynamic"));

export default app;
