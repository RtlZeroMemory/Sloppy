import { Results } from "sloppy";

export function healthModule(app) {
    app.get("/health", () => Results.text("ok")).withName("Health.Get");
}
