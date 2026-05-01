import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.post("/echo", (ctx) => Results.json({
  body: ctx.body.json()
}));

export default app;
