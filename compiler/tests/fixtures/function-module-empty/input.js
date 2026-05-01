import { Sloppy, Results } from "sloppy";
import { emptyModule } from "./modules/empty.js";

const app = Sloppy.create();
app.useModule(emptyModule);
app.get("/health", () => Results.text("ok"));

export default app;
