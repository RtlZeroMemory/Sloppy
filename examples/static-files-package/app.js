import { Sloppy } from "sloppy";

const app = Sloppy.create();

app.staticFiles("/public", {
    root: "public",
    cacheControl: "public, max-age=3600",
});

export default app;
