import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
const db = app.provider("sqlite:main");

app.get("/users", () => Results.json(db.query("select id from users", [])));

export default app;
