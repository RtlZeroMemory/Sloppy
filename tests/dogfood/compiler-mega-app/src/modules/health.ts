import { Results } from "sloppy";

export function healthModule(app) {
  const db = app.provider("sqlite:main");
  const health = app.group("/health").withTags("health");
  health.get("/live", () => Results.text("live")).withName("Health.Live");
  health.get("/ready", async () => Results.ok(await db.queryOne("select 1 as ok", [])))
    .withName("Health.Ready");
}
