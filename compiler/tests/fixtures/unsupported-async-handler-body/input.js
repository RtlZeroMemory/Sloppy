import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/async", async () => {
  await Promise.resolve();
  return Results.text("done");
});

export default app;
