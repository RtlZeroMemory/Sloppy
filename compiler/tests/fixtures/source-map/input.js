import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/source", function () {
  return Results.text("mapped");
});

export default app;
