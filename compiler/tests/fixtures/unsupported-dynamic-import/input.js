import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

const users = import("./users.js");
app.get("/", () => Results.text("ok"));

export default app;
