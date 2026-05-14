import { Results } from "sloppy";
import { toUserName } from "../helpers/names";
import { createUser, deleteUser, findUser, listUsers, updateUser } from "../repositories/users";

export function usersModule(app) {
  const db = app.provider("sqlite:main");
  const api = app.group("/api").withTags("api");
  const users = api.group("/users").withTags("users");

  users.get("/", async (ctx) => {
    const normalizeRow = () => ({ id: -1, name: "shadow" });
    return Results.ok({ q: ctx.query.q, users: await listUsers(db) });
  }).withName("Users.List");

  users.get("/{id:int}", async (ctx) => {
    const user = await findUser(db, ctx.route.id);
    return Results.ok(user);
  }).withName("Users.Get");

  users.post("/", async (ctx) => {
    const input = await ctx.body.json();
    const checked = toUserName(input.name);
    if (!checked.valid) {
      return Results.problem({ status: 400, title: "Invalid user name" }, { status: 400 });
    }
    const created = await createUser(db, checked.name);
    return Results.created(`/api/users/${created.id}`, created);
  }).withName("Users.Create");

  users.put("/{id:int}", async (ctx) => {
    const input = await ctx.body.json();
    const checked = toUserName(input.name);
    return Results.ok(await updateUser(db, ctx.route.id, checked.name));
  }).withName("Users.Update");

  users.patch("/{id:int}", async (ctx) => {
    const input = await ctx.body.json();
    return Results.ok(await updateUser(db, ctx.route.id, input.name ?? "patched"));
  }).withName("Users.Patch");

  users.delete("/{id:int}", async (ctx) => Results.ok(await deleteUser(db, ctx.route.id)))
    .withName("Users.Delete");
}
