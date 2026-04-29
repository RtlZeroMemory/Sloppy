import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/function", function () {
  return Results.text("Hello from a function handler");
});

export default app;
