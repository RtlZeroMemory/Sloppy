import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

if (true) {
  app.mapGet("/", () => Results.text("Hello"));
}

export default app;
