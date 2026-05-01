import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
const serviceName = app.config.getString("App:Name", "configured-api");
const greeting = app.config.getString("App:Greeting", "hello");

app.get("/config", () => Results.json({ serviceName: "configured-api", greeting: "Plan-visible" }))
    .withName("Config.Get");

export default app;
