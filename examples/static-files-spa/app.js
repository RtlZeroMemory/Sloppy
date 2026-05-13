import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/api/health", () => Results.json({ ok: true }));

app.spa("/", {
    root: "dist",
    fallback: "index.html",
    cacheControl: {
        html: "no-cache",
        assets: "public, max-age=31536000, immutable",
    },
});

export default app;
