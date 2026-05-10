import { ProblemDetails, Sloppy } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { healthModule } from "./routes/health.ts";
import { usersModule } from "./routes/users.ts";

const app = Sloppy.create();

app.use(sqlite("main"));
app.use(ProblemDetails.defaults({ detail: "never" }));
app.useModule(usersModule);
app.useModule(healthModule);

export default app;
