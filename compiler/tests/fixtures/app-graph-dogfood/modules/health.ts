import { Results } from "sloppy";

export function healthModule(app) {
  const status = app.group("/status").withTags("status");
  status.get("/ping", () => Results.text("pong")).withName("Status.Ping");
}
