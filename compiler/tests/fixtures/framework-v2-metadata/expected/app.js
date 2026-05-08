const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Results, data, sql, System, Environment, Process, Signals, BackgroundService, WorkQueue, WorkerPool, Worker, WorkerCancellationController, WorkerCancellationSignal, SloppyWorkerError, __createFrameworkServiceProvider } = __sloppyRuntime;
const __sloppy_framework_services = __createFrameworkServiceProvider();
__sloppy_framework_services.addSingleton("queue.emails", () => WorkQueue.create("emails"));
const __sloppy_framework_provider_configs = new Map([["data.main", {"access":"readwrite","connectionStringEnv":"SLOPPY_POSTGRES_TEST_URL","providerKind":"postgres"}], ["data.audit", {"access":"readwrite","connectionStringEnv":null,"providerKind":"sqlite"}], ["data.search", {"access":"readwrite","connectionStringEnv":"SLOPPY_SQLSERVER_TEST_CONNECTION_STRING","providerKind":"sqlserver"}]]);
function __sloppy_framework_arg(ctx, scope, binding) {
  if (binding.kind === "body.json") { return ctx.request.json(); }
  if (binding.kind === "context") { return ctx; }
  if (binding.kind === "injection") { return __sloppy_framework_injection(scope, binding); }
  if (binding.kind === "config") { const value = Environment.get(binding.name); if (value === undefined) { throw new Error(`sloppy: Config injection for '${binding.name}' requires an environment value.`); } return value; }
  let value;
  if (binding.kind === "route") { value = ctx.route[binding.name]; }
  else if (binding.kind === "query") { value = ctx.query[binding.name]; }
  else if (binding.kind === "header") { value = ctx.request.headers.get(binding.name); }
  else { throw new TypeError(`Sloppy Framework binding kind '${binding.kind}' is not supported.`); }
  return __sloppy_framework_coerce(value, binding);
}
function __sloppy_framework_coerce(value, binding) {
  if (value === null || value === undefined) { return value; }
  const type = String(binding.type || binding.schema || "");
  if (type.includes("boolean")) {
    const normalized = String(value).toLowerCase();
    if (normalized === "true") { return true; }
    if (normalized === "false") { return false; }
    throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected a boolean value.`);
  }
  if (type.includes("number") || type.includes("PositiveInt") || type === "int") {
    const parsed = Number(value);
    if (!Number.isFinite(parsed)) { throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected a numeric value.`); }
    if ((type.includes("PositiveInt") || type === "int") && !Number.isInteger(parsed)) { throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected an integer value.`); }
    if (type.includes("PositiveInt") && parsed <= 0) { throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected a positive integer value.`); }
    return parsed;
  }
  return value;
}
function __sloppy_framework_injection(scope, binding) {
  const dependencyName = binding.capability || (binding.name && binding.name.includes(".") ? binding.name : `data.${binding.name}`);
  if (binding.injectionKind === "service") { return scope.get(binding.name); }
  if (binding.injectionKind === "queue") { return scope.get(dependencyName); }
  if (binding.injectionKind === "provider" && binding.providerKind === "sqlite") { return scope.track(data.sqlite(dependencyName)); }
  if (binding.injectionKind === "provider" && binding.providerKind === "postgres") { return scope.track(data.postgres.open(__sloppy_framework_provider_open_options(binding, dependencyName))); }
  if (binding.injectionKind === "provider" && binding.providerKind === "sqlserver") { return scope.track(data.sqlserver.open(__sloppy_framework_provider_open_options(binding, dependencyName))); }
  throw new Error(`sloppy: ${binding.injectionKind} injection for '${binding.name}' is unavailable in this runtime lane.`);
}
function __sloppy_framework_provider_open_options(binding, token) {
  const config = __sloppy_framework_provider_configs.get(token);
  if (config === undefined) { throw new Error(`sloppy: provider '${token}' is not configured for Framework injection.`); }
  if (config.providerKind !== binding.providerKind) { throw new Error(`sloppy: provider '${token}' is configured as ${config.providerKind}, not ${binding.providerKind}.`); }
  const key = config.connectionStringEnv;
  if (typeof key !== "string" || key.length === 0) { throw new Error(`sloppy: provider '${token}' does not declare a connection string config key for Framework injection.`); }
  const connectionString = Environment.get(key);
  if (typeof connectionString !== "string" || connectionString.length === 0) { throw new Error(`sloppy: provider '${token}' requires environment config '${key}'.`); }
  return { connectionString, capability: token, access: config.access === "read" ? "read" : "readwrite" };
}

globalThis.__sloppy_handler_1 = async (ctx) => { const __sloppy_scope = __sloppy_framework_services.createScope(ctx); ctx.services = __sloppy_scope; try { const __sloppy_typed_handler = async (
  input,
  db,
  audit,
  search,
  emails,
  ctx,
) => {
  const user = await db.queryOne(
    sql`select id, email, name from users where id = ${1}`,
    { deadline: ctx.deadline }
  );

  return Results.created(`/users/${user.id}`, user);
}; const __sloppy_args = await Promise.all([__sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"body.json","name":null,"parameter":"input","providerKind":null,"schema":"UserCreate","type":"UserCreate","wrapper":null}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":"data.main","injectionKind":"provider","kind":"injection","name":"main","parameter":"db","providerKind":"postgres","schema":null,"type":"Postgres<\"main\">","wrapper":null}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":"data.audit","injectionKind":"provider","kind":"injection","name":"audit","parameter":"audit","providerKind":"sqlite","schema":null,"type":"Sqlite<\"audit\">","wrapper":null}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":"data.search","injectionKind":"provider","kind":"injection","name":"search","parameter":"search","providerKind":"sqlserver","schema":null,"type":"SqlServer<\"search\">","wrapper":null}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":"queue.emails","injectionKind":"queue","kind":"injection","name":"emails","parameter":"emails","providerKind":"workqueue","schema":null,"type":"WorkQueue<\"emails\">","wrapper":null}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"context","name":"RequestContext","parameter":"ctx","providerKind":null,"schema":null,"type":"RequestContext","wrapper":null})]); return await __sloppy_typed_handler(...__sloppy_args); } finally { await __sloppy_scope.dispose(); } };
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
globalThis.__sloppy_handler_2 = async (ctx) => { const __sloppy_scope = __sloppy_framework_services.createScope(ctx); ctx.services = __sloppy_scope; try { const __sloppy_typed_handler = async (
  id,
  trace,
  includeDeleted,
  input,
  db,
  ctx,
) => {
  const user = await db.queryOne(
    sql`select id, email, name from users where id = ${id}`,
    { deadline: ctx.deadline, trace, includeDeleted, input }
  );

  return user ? Results.ok(user) : Results.notFound();
}; const __sloppy_args = await Promise.all([__sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"route","name":"id","parameter":"id","providerKind":null,"schema":"number","type":"Route<number>","wrapper":"Route"}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"header","name":"x-trace-id","parameter":"trace","providerKind":null,"schema":null,"type":"Header<\"x-trace-id\">","wrapper":"Header"}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"query","name":"includeDeleted","parameter":"includeDeleted","providerKind":null,"schema":"bool","type":"Query<boolean>","wrapper":"Query"}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"body.json","name":"input","parameter":"input","providerKind":null,"schema":"UserCreate","type":"Body<UserCreate>","wrapper":"Body"}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":"data.main","injectionKind":"provider","kind":"injection","name":"main","parameter":"db","providerKind":"postgres","schema":null,"type":"Postgres<\"main\">","wrapper":null}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"context","name":"RequestContext","parameter":"ctx","providerKind":null,"schema":null,"type":"RequestContext","wrapper":null})]); return await __sloppy_typed_handler(...__sloppy_args); } finally { await __sloppy_scope.dispose(); } };
globalThis.__sloppy_register_handler(2, globalThis.__sloppy_handler_2);
