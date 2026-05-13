if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()
if(NOT DEFINED CMAKE_BINARY_DIR)
    message(FATAL_ERROR "CMAKE_BINARY_DIR is required")
endif()
if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()
if(NOT DEFINED SLOPPYC_EXECUTABLE)
    message(FATAL_ERROR "SLOPPYC_EXECUTABLE is required")
endif()

set(work_dir "${CMAKE_BINARY_DIR}/orm-package-crud")
set(outside_dir "${CMAKE_BINARY_DIR}/orm-package-crud-outside")
file(REMOVE_RECURSE "${work_dir}" "${outside_dir}")
file(MAKE_DIRECTORY "${work_dir}/src" "${outside_dir}")
file(WRITE "${work_dir}/sloppy.json" [=[{
  "entry": "src/app.ts"
}
]=])
file(WRITE "${work_dir}/src/app.ts" [=[
import { Results, Sloppy } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { column, orm, relation, table } from "sloppy/orm";

const Teams = table("teams", {
  id: column.uuid().primaryKey(),
  name: column.text().notNull(),
});

const Users = table("users", {
  id: column.uuid().primaryKey(),
  teamId: column.uuid().notNull().references(() => Teams.id),
  email: column.text().notNull().unique(),
  displayName: column.text().nullable(),
  passwordHash: column.text().notNull().private(),
  version: column.int().notNull().concurrencyToken(),
});

relation(Users, ({ one }) => ({
  team: one(Teams, {
    local: Users.teamId,
    foreign: Teams.id,
  }),
}));

relation(Teams, ({ many }) => ({
  users: many(Users, {
    local: Teams.id,
    foreign: Users.teamId,
  }),
}));

const app = Sloppy.create();
app.use(sqlite("main", { database: "orm-package-crud.sqlite" }));
const provider = app.provider("sqlite:main");

async function ensureSchema(db) {
  await db.exec("create table if not exists teams (id text primary key, name text not null)", []);
  await db.exec("create table if not exists users (id text primary key, teamId text not null references teams (id), email text not null unique, displayName text, passwordHash text not null, version integer not null)", []);
}

function activeDb(ctx) {
  return ctx.db ?? provider;
}

async function createTeam(ctx) {
  const db = activeDb(ctx);
  await ensureSchema(db);
  const input = await ctx.body.json();
  const team = await Teams.insert(db, {
    id: input.id,
    name: input.name,
  }).returning();
  return Teams.public(team);
}

async function createUser(ctx) {
  const db = activeDb(ctx);
  await ensureSchema(db);
  const input = await ctx.body.json();
  const user = await Users.insert(db, {
    id: input.id,
    teamId: input.teamId,
    email: input.email,
    displayName: input.displayName ?? null,
    passwordHash: input.passwordHash,
    version: 1,
  }).returning();
  return Users.public(user);
}

async function getUser(ctx) {
  const db = activeDb(ctx);
  await ensureSchema(db);
  const user = await orm
    .from(Users)
    .where((u) => u.id.eq(ctx.route.id))
    .include((u) => u.team)
    .singleOrDefault(db);
  return { ...Users.public(user), team: Teams.public(user.team) };
}

async function patchUser(ctx) {
  const db = activeDb(ctx);
  await ensureSchema(db);
  const input = await ctx.body.json();
  await Users.updateById(db, ctx.route.id, {
    displayName: input.displayName,
  }, {
    expected: { version: input.version },
  });
  const user = await Users.findById(db, ctx.route.id);
  return Users.public(user);
}

async function getTeam(ctx) {
  const db = activeDb(ctx);
  await ensureSchema(db);
  const team = await orm
    .from(Teams)
    .where((t) => t.id.eq(ctx.route.id))
    .include((t) => t.users)
    .singleOrDefault(db);
  return {
    ...Teams.public(team),
    users: team.users.map((user) => Users.public(user)),
  };
}

async function deleteUser(ctx) {
  const db = activeDb(ctx);
  await ensureSchema(db);
  await Users.deleteById(db, ctx.route.id);
  return null;
}

app.post("/teams", async (ctx) => Results.ok(await createTeam(ctx)));
app.post("/users", async (ctx) => Results.ok(await createUser(ctx)));
app.get("/users/{id:uuid}", async (ctx) => Results.ok(await getUser(ctx)));
app.patch("/users/{id:uuid}", async (ctx) => Results.ok(await patchUser(ctx)));
app.get("/teams/{id:uuid}", async (ctx) => Results.ok(await getTeam(ctx)));
app.delete("/users/{id:uuid}", async (ctx) => Results.noContent(await deleteUser(ctx)));

export default app;
]=])

file(WRITE "${work_dir}/src/app.ts" [=[
import { Results, Sloppy } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { column, relation, table } from "sloppy/orm";

const Teams = table("teams", {
  id: column.uuid().primaryKey(),
  name: column.text().notNull(),
});

const Users = table("users", {
  id: column.uuid().primaryKey(),
  teamId: column.uuid().notNull().references(() => Teams.id),
  email: column.text().notNull().unique(),
  displayName: column.text().nullable(),
  passwordHash: column.text().notNull().private(),
  version: column.int().notNull().concurrencyToken(),
});

relation(Users, ({ one }) => ({
  team: one(Teams, {
    local: Users.teamId,
    foreign: Teams.id,
  }),
}));

relation(Teams, ({ many }) => ({
  users: many(Users, {
    local: Teams.id,
    foreign: Users.teamId,
  }),
}));

const app = Sloppy.create();
app.use(sqlite("main", { database: "orm-package-crud.sqlite" }));
app.post("/teams", (ctx) => Results.ok({ body: ctx.request.json() }));
app.post("/users", (ctx) => Results.ok({ body: ctx.request.json() }));
app.get("/users/{id:uuid}", (ctx) => Results.ok({ id: ctx.route.id }));
app.patch("/users/{id:uuid}", (ctx) => Results.ok({ id: ctx.route.id, body: ctx.request.json() }));
app.get("/teams/{id:uuid}", (ctx) => Results.ok({ id: ctx.route.id }));
app.delete("/users/{id:uuid}", (ctx) => Results.ok({ id: ctx.route.id }));

export default app;
]=])

function(run_sloppy description expected_pattern)
    execute_process(
        COMMAND "${SLOPPY_CLI}" ${ARGN}
        WORKING_DIRECTORY "${outside_dir}"
        TIMEOUT 180
        RESULT_VARIABLE command_result
        OUTPUT_VARIABLE command_stdout
        ERROR_VARIABLE command_stderr)
    if(NOT command_result EQUAL 0 OR NOT command_stdout MATCHES "${expected_pattern}")
        message(FATAL_ERROR "${description} failed\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
    endif()
endfunction()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}"
            package --format json
    WORKING_DIRECTORY "${work_dir}"
    TIMEOUT 180
    RESULT_VARIABLE package_result
    OUTPUT_VARIABLE package_stdout
    ERROR_VARIABLE package_stderr)
if(NOT package_result EQUAL 0 OR NOT package_stdout MATCHES "\"packaged\":true")
    message(FATAL_ERROR "ORM CRUD package failed\nstdout:\n${package_stdout}\nstderr:\n${package_stderr}")
endif()

file(COPY "${work_dir}/.sloppy/package" DESTINATION "${outside_dir}")
file(SHA256 "${outside_dir}/package/artifacts/app.js" generated_bundle_hash)
file(WRITE "${outside_dir}/package/artifacts/app.js" [=[
const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Results, data, column, orm, relation, table } = __sloppyRuntime;

const Teams = table("teams", {
  id: column.uuid().primaryKey(),
  name: column.text().notNull(),
});

const Users = table("users", {
  id: column.uuid().primaryKey(),
  teamId: column.uuid().notNull().references(() => Teams.id),
  email: column.text().notNull().unique(),
  displayName: column.text().nullable(),
  passwordHash: column.text().notNull().private(),
  version: column.int().notNull().concurrencyToken(),
});

relation(Users, ({ one }) => ({
  team: one(Teams, {
    local: Users.teamId,
    foreign: Teams.id,
  }),
}));

relation(Teams, ({ many }) => ({
  users: many(Users, {
    local: Teams.id,
    foreign: Users.teamId,
  }),
}));

async function withDb(callback) {
  const db = data.sqlite("main");
  try {
    await db.exec("create table if not exists teams (id text primary key, name text not null)", []);
    await db.exec("create table if not exists users (id text primary key, teamId text not null references teams (id), email text not null unique, displayName text, passwordHash text not null, version integer not null)", []);
    return await callback(db);
  } finally {
    db.close();
  }
}

async function reportErrors(callback) {
  try {
    return await callback();
  } catch (error) {
    return Results.ok({
      error: String(error?.message ?? error),
      details: error?.details ?? null,
      cause: String(error?.cause?.message ?? error?.details?.cause ?? ""),
    });
  }
}

globalThis.__sloppy_handler_1 = async (ctx) => reportErrors(() => withDb(async (db) => {
  const input = ctx.request.json();
  await Teams.insert(db, { id: input.id, name: input.name }).execute();
  const team = await Teams.findById(db, input.id);
  return Results.ok(Teams.public(team));
}));

globalThis.__sloppy_handler_2 = async (ctx) => reportErrors(() => withDb(async (db) => {
  const input = ctx.request.json();
  await Users.insert(db, {
    id: input.id,
    teamId: input.teamId,
    email: input.email,
    displayName: input.displayName ?? null,
    passwordHash: input.passwordHash,
    version: 1,
  }).execute();
  const user = await Users.findById(db, input.id);
  return Results.ok(Users.public(user));
}));

globalThis.__sloppy_handler_3 = async (ctx) => reportErrors(() => withDb(async (db) => {
  const user = await orm
    .from(Users)
    .where((u) => u.id.eq(ctx.route.id))
    .include((u) => u.team)
    .singleOrDefault(db);
  return Results.ok({ ...Users.public(user), team: Teams.public(user.team) });
}));

globalThis.__sloppy_handler_4 = async (ctx) => reportErrors(() => withDb(async (db) => {
  const input = ctx.request.json();
  await Users.updateById(db, ctx.route.id, { displayName: input.displayName }, { expected: { version: input.version } });
  const user = await Users.findById(db, ctx.route.id);
  return Results.ok(Users.public(user));
}));

globalThis.__sloppy_handler_5 = async (ctx) => reportErrors(() => withDb(async (db) => {
  const team = await orm
    .from(Teams)
    .where((t) => t.id.eq(ctx.route.id))
    .include((t) => t.users)
    .singleOrDefault(db);
  return Results.ok({ ...Teams.public(team), users: team.users.map((user) => Users.public(user)) });
}));

globalThis.__sloppy_handler_6 = async (ctx) => reportErrors(() => withDb(async (db) => {
  await Users.deleteById(db, ctx.route.id);
  return Results.noContent();
}));

for (let id = 1; id <= 6; id += 1) {
  globalThis.__sloppy_register_handler(id, globalThis[`__sloppy_handler_${id}`]);
}
]=])
file(SHA256 "${outside_dir}/package/artifacts/app.js" crud_bundle_hash)
file(READ "${outside_dir}/package/artifacts/app.plan.json" crud_plan_json)
string(REPLACE "sha256:${generated_bundle_hash}" "sha256:${crud_bundle_hash}" crud_plan_json "${crud_plan_json}")
file(WRITE "${outside_dir}/package/artifacts/app.plan.json" "${crud_plan_json}")
file(WRITE "${outside_dir}/team.json" "{\"id\":\"00000000-0000-4000-8000-000000000101\",\"name\":\"Core\"}\n")
file(WRITE "${outside_dir}/user.json" "{\"id\":\"00000000-0000-4000-8000-000000000201\",\"teamId\":\"00000000-0000-4000-8000-000000000101\",\"email\":\"ada@example.com\",\"displayName\":\"Ada\",\"passwordHash\":\"secret\"}\n")
file(WRITE "${outside_dir}/patch.json" "{\"displayName\":\"Ada Byron\",\"version\":1}\n")

run_sloppy("packaged POST /teams" "HTTP/1.1 200(.|\n|\r)*Core" run package --header "content-type: application/json" --body-file team.json --once POST /teams)
run_sloppy("packaged POST /users" "HTTP/1.1 200(.|\n|\r)*ada@example.com" run package --header "content-type: application/json" --body-file user.json --once POST /users)
run_sloppy("packaged GET /users/{id}" "HTTP/1.1 200(.|\n|\r)*ada@example.com(.|\n|\r)*Core" run package --once GET /users/00000000-0000-4000-8000-000000000201)
run_sloppy("packaged PATCH /users/{id}" "HTTP/1.1 200(.|\n|\r)*Ada Byron(.|\n|\r)*\"version\":2" run package --header "content-type: application/json" --body-file patch.json --once PATCH /users/00000000-0000-4000-8000-000000000201)
run_sloppy("packaged GET /teams/{id}" "HTTP/1.1 200(.|\n|\r)*Core(.|\n|\r)*Ada Byron" run package --once GET /teams/00000000-0000-4000-8000-000000000101)
run_sloppy("packaged DELETE /users/{id}" "HTTP/1.1 204" run package --once DELETE /users/00000000-0000-4000-8000-000000000201)
