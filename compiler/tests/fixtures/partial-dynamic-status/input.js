import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/status/{code:int}", (ctx) => Results.status(ctx.route.code, {
  ok: true
}));

export default app;
