import { Cache, Results, Sloppy } from "sloppy";

const app = Sloppy.create();
const cache = Cache.memory("default", { maxEntries: 1000, ttlMs: 30000 });

app.services.addCache(cache);

let productCalls = 0;
app.get("/products", (ctx) => {
    productCalls += 1;
    return Results.json({
        category: ctx.query.category ?? "all",
        page: Number(ctx.query.page ?? 1),
        calls: productCalls,
    });
}).outputCache({
    ttlMs: 30000,
    varyByQuery: ["category", "page"],
    tags: ["products"],
});

app.get("/assets/config.json", () => Results.json({ version: 1 }))
    .cacheHeaders({
        cacheControl: "public, max-age=60",
        vary: ["Accept-Encoding"],
        etag: true,
    });

export default app;
