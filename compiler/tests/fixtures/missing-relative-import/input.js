import { Sloppy, Results } from "sloppy";
import { usersModule } from "./missing.js";

const app = Sloppy.create();

app.useModule(usersModule);
app.get("/", () => Results.text("ok"));

export default app;
