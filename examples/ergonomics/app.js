import { Sloppy, Results, schema } from "sloppy";

const builder = Sloppy.createBuilder();

builder.config.addObject({
    "app.name": "Sloppy Ergonomics Demo",
});

builder.logging.addMemorySink();
builder.services.addSingleton("users.message", () => "hello from the bootstrap app host");

const app = builder.build();

const SearchQuery = schema.object({
    q: schema.string().min(1),
});

const users = app.mapGroup("/users")
    .withTags("Users")
    .withName("Users");

users.mapGet("{id:int}", ({ route, services }) => {
    return Results.ok({
        id: route.id ?? "demo",
        message: services.get("users.message"),
    });
}).withName("Users.Get");

users.mapGet("/search", { query: SearchQuery }, () => {
    return Results.accepted({
        status: "shape-only",
    });
}).withName("Users.Search");

app.mapGet("/health", () => Results.noContent())
    .withName("Health");

export default app;
