import { Results, Sloppy } from "sloppy";
import { healthModule } from "./modules/health.module.ts";
import { usersModule } from "./modules/users.module.ts";

const app = Sloppy.create();

app.useModule(healthModule);
app.useModule(usersModule);

export default app;
