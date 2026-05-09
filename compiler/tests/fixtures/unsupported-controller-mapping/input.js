import { Sloppy, Results } from "sloppy";

function UsersController() {}

const app = Sloppy.create();
app.mapController("/users", UsersController);
app.get("/", () => Results.ok({ ok: true }));

export default app;
