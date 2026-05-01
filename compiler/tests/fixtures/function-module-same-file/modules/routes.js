import { Results } from "sloppy";

export function healthModule(app) {
    const health = app.group("/health");

    health.get("/", () => Results.text("ok"));
}

export function usersModule(app) {
    const users = app.group("/users");

    users.get("/", () => Results.text("users"));
}
