import { Sloppy } from "sloppy";

const app = Sloppy.create();

app.staticFiles("/assets", {
    root: "public",
    cacheControl: "public, max-age=31536000, immutable",
    precompressed: ["br", "gzip"],
});

export default app;
