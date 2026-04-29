import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

const hello = () => Results.text("Hello");

app.mapGet("/", hello);

export default app;
