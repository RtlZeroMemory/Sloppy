import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/health", () => Results.text("ok"));

app.staticFiles("/assets", {
    root: "public",
    cacheControl: "public, max-age=3600",
});

export default app;
