import { ProblemDetails, Sloppy } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { appsModule } from "./routes/apps.js";
import { buildsModule } from "./routes/builds.js";
import { deploymentsModule } from "./routes/deployments.js";
import { diagnosticsModule } from "./routes/diagnostics.js";
import { healthModule } from "./routes/health.js";
import { projectsModule } from "./routes/projects.js";

const app = Sloppy.create();

app.use(ProblemDetails.defaults());
app.use(sqlite("main", { database: "control-plane.db" }));
app.useModule(healthModule);
app.useModule(projectsModule);
app.useModule(appsModule);
app.useModule(buildsModule);
app.useModule(deploymentsModule);
app.useModule(diagnosticsModule);

export default app;
