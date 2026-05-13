import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/api/status", () => Results.json({ ok: true }));

app.staticFiles("/assets", {
    root: "public",
    dotfiles: "deny",
    precompressed: true,
});

export default app;
