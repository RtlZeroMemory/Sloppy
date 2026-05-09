import { Sloppy } from "sloppy";
import { appsModule } from "./routes/apps.ts";
import { buildsModule } from "./routes/builds.ts";
import { deploymentsModule } from "./routes/deployments.ts";
import { diagnosticsModule } from "./routes/diagnostics.ts";
import { healthModule } from "./routes/health.ts";
import { projectsModule } from "./routes/projects.ts";

const app = Sloppy.create();

app.useModule(healthModule);
app.useModule(projectsModule);
app.useModule(appsModule);
app.useModule(buildsModule);
app.useModule(deploymentsModule);
app.useModule(diagnosticsModule);

export default app;
