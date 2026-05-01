import { Sloppy, Results } from "sloppy";
import { healthModule, usersModule } from "./modules/routes.js";

const app = Sloppy.create();

app.useModule(healthModule);
app.useModule(usersModule);

export default app;
