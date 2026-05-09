import { Sloppy } from "sloppy";
import { healthModule } from "./routes/health.ts";
import { projectsModule } from "./routes/projects.ts";
import { usersModule } from "./routes/users.ts";

const app = Sloppy.create();

app.useModule(healthModule);
app.useModule(usersModule);
app.useModule(projectsModule);

export default app;
