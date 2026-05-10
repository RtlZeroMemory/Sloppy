import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

function routeFor(name) {
    return `/${name}`;
}

app.mapGet(routeFor("health"), () => Results.text("Hello from Sloppy"));

export default app;
