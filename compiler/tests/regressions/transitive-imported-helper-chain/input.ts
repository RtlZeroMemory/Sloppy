import { Sloppy, Results } from "sloppy";
import { buildTicket } from "./helpers";

const app = Sloppy.create();

app.get("/tickets/{id:int}", (ctx) => {
  const normalizeTicket = () => ({ id: -1, label: "local-shadow" });
  return Results.ok(buildTicket({ id: ctx.route.id, label: " alpha " }));
});

export default app;
