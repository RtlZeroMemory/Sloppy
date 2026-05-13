import { Cache, Results, Schema, Sloppy } from "sloppy";

const app = Sloppy.create();
const cache = Cache.memory("main", { maxEntries: 1000, ttlMs: 30000 });
const UserDto = Schema.object({
    id: Schema.integer(),
    name: Schema.string(),
});

app.services.addCache(cache);
app.services.addSingleton("users", () => ({
    async findById(id) {
        return id === "42" ? { id: 42, name: "Ada Lovelace" } : null;
    },
}));

app.get("/users/{id}", async (ctx) => {
    const user = await cache.getOrCreate(`users:${ctx.route.id}`, {
        ttlMs: 30000,
        tags: [`user:${ctx.route.id}`, "users"],
        schema: UserDto,
    }, async () => ctx.services.get("users").findById(ctx.route.id));

    return user === null ? Results.notFound() : Results.json(user);
});

app.post("/users/{id}/invalidate", async (ctx) => {
    await cache.invalidateTags([`user:${ctx.route.id}`, "users"]);
    return Results.noContent();
});

export default app;
