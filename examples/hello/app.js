import { Sloppy, Results } from "sloppy";

const builder = Sloppy.createBuilder();

builder.config.addObject({
    "app.name": "hello",
});

builder.logging.addMemorySink();
builder.services.addSingleton("message", () => "Hello from Sloppy");

const app = builder.build();

app.mapGet("/", ({ services }) => Results.text(services.get("message")))
    .withName("Hello.Index");

export default app;
