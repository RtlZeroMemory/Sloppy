import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { usersModule } from "./modules/users.js";

const app = Sloppy.create();

app.use(sqlite("main", { database: ":memory:" }));
app.useModule(usersModule);
app.get("/health", () => Results.text("ok"));

export default app;
