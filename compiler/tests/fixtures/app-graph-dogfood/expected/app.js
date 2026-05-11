const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Results, ProblemDetails, data, System, Environment, Process, ProcessHandle, Signals, OsError, __createFrameworkServiceProvider } = __sloppyRuntime;
const __sloppy_framework_services = __createFrameworkServiceProvider();
__sloppy_framework_services.addSingleton("ClockService", () => ({ now: "2026-01-01T00:00:00Z" }));
const __sloppy_framework_provider_configs = new Map([["data.main", {"access":"read","connectionStringEnv":null,"connectionStringKey":null,"providerKind":"sqlite"}]]);
const __sloppy_framework_config_defaults = new Map([["App:Greeting", "hello"]]);
function __sloppy_open_data_provider(kind, token) {
  if (kind === "sqlite") { return data.sqlite(token); }
  throw new Error(`sloppy: ${kind} provider bridge unavailable`);
}
function __sloppy_framework_arg(ctx, scope, binding) {
  if (binding.kind === "body.json") { return ctx.request.json(); }
  if (binding.kind === "body.form") { return ctx.request.form(); }
  if (binding.kind === "body.multipart") { return ctx.request.multipart(); }
  if (binding.kind === "context") { return ctx; }
  if (binding.kind === "injection") { return __sloppy_framework_injection(scope, binding); }
  if (binding.kind === "config") { const value = Environment.get(binding.name); if (value !== undefined) { return value; } if (__sloppy_framework_config_defaults.has(binding.name)) { return __sloppy_framework_config_defaults.get(binding.name); } throw new Error(`sloppy: Config injection for '${binding.name}' requires an environment value.`); }
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
  if (binding.injectionKind === "provider" && binding.providerKind === "sqlite" && typeof data.sqlite === "function") { return scope.track(data.sqlite(dependencyName)); }
  if (binding.injectionKind === "provider" && binding.providerKind === "postgres" && data.postgres !== undefined && typeof data.postgres.open === "function") { return scope.track(data.postgres.open(__sloppy_framework_provider_open_options(binding, dependencyName))); }
  if (binding.injectionKind === "provider" && binding.providerKind === "sqlserver" && data.sqlserver !== undefined && typeof data.sqlserver.open === "function") { return scope.track(data.sqlserver.open(__sloppy_framework_provider_open_options(binding, dependencyName))); }
  throw new Error(`sloppy: ${binding.injectionKind} injection for '${binding.name}' is unavailable in this runtime lane.`);
}
function __sloppy_framework_provider_open_options(binding, token) {
  const config = __sloppy_framework_provider_configs.get(token);
  if (config === undefined) { throw new Error(`sloppy: provider '${token}' is not configured for Framework injection.`); }
  if (config.providerKind !== binding.providerKind) { throw new Error(`sloppy: provider '${token}' is configured as ${config.providerKind}, not ${binding.providerKind}.`); }
  const key = config.connectionStringKey;
  const env = config.connectionStringEnv;
  if (typeof key !== "string" || key.length === 0 || typeof env !== "string" || env.length === 0) { throw new Error(`sloppy: provider '${token}' does not declare a connection string config key for Framework injection.`); }
  const connectionString = Environment.get(env);
  if (typeof connectionString !== "string" || connectionString.length === 0) { throw new Error(`sloppy: provider '${token}' requires config '${key}' from environment '${env}'.`); }
  return { connectionString, capability: token, access: config.access === "read" ? "read" : "readwrite" };
}

globalThis.__sloppy_handler_1 = async function(ctx) { try { return await (function(ctx) { const __sloppy_opened_providers = []; let db; try { db = __sloppy_open_data_provider("sqlite", "data.main"); __sloppy_opened_providers.push(db); function listProjects() {
  return db.query("select id, name from projects", []);
} return ((ctx) => Results.ok({
  q: ctx.query.q,
  rows: listProjects(),
}))(ctx); } finally { while (__sloppy_opened_providers.length > 0) { const __sloppy_provider = __sloppy_opened_providers.pop(); try { __sloppy_provider.close(); } catch (_) {} } } })(ctx); } catch (error) { const __sloppy_problem = { status: 500, title: "Internal Server Error", code: "SLOPPY_E_HANDLER_ERROR" }; if (false) { __sloppy_problem.detail = String((error && error.message) ?? error); } return Results.problem(__sloppy_problem, { status: 500 }); } };
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
globalThis.__sloppy_handler_2 = async function(ctx) { try { return await (async (ctx) => { const __sloppy_scope = __sloppy_framework_services.createScope(ctx); ctx.services = __sloppy_scope; try { const __sloppy_typed_handler = (
  body,
  database,
  clock,
  trace,
  message,
  ctx,
) => Results.created("/api/projects/1", {
  name: body.name,
  at: clock.now,
  trace,
  message,
  hasContext: ctx !== undefined,
  providerReady: database !== undefined,
}); const __sloppy_args = await Promise.all([__sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"body.json","name":"body","parameter":"body","providerKind":null,"schema":"ProjectCreateInput","type":"Body<ProjectCreateInput>","wrapper":"Body"}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":"data.main","injectionKind":"provider","kind":"injection","name":"main","parameter":"database","providerKind":"sqlite","schema":null,"type":"Sqlite<\"main\">","wrapper":null}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":"service","kind":"injection","name":"ClockService","parameter":"clock","providerKind":null,"schema":"ClockService","type":"Service<ClockService>","wrapper":"Service"}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"header","name":"x-trace-id","parameter":"trace","providerKind":null,"schema":null,"type":"Header<\"x-trace-id\">","wrapper":"Header"}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"config","name":"App:Greeting","parameter":"message","providerKind":null,"schema":null,"type":"Config<\"App:Greeting\">","wrapper":"Config"}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"context","name":"RequestContext","parameter":"ctx","providerKind":null,"schema":null,"type":"RequestContext","wrapper":null})]); return await __sloppy_typed_handler(...__sloppy_args); } finally { await __sloppy_scope.dispose(); } })(ctx); } catch (error) { const __sloppy_problem = { status: 500, title: "Internal Server Error", code: "SLOPPY_E_HANDLER_ERROR" }; if (false) { __sloppy_problem.detail = String((error && error.message) ?? error); } return Results.problem(__sloppy_problem, { status: 500 }); } };
globalThis.__sloppy_register_handler(2, globalThis.__sloppy_handler_2);
globalThis.__sloppy_handler_3 = async function(ctx) { try { return await (async (ctx) => { const __sloppy_scope = __sloppy_framework_services.createScope(ctx); ctx.services = __sloppy_scope; try { const __sloppy_typed_handler = (
  id,
  includeDeleted,
  database,
  ctx,
) => {
  const project = database.queryOne("select id, name from projects where id = ?", [id]);
  return project ? Results.ok(project) : Results.notFound();
}; const __sloppy_args = await Promise.all([__sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"route","name":"id","parameter":"id","providerKind":null,"schema":"number","type":"Route<number>","wrapper":"Route"}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"query","name":"includeDeleted","parameter":"includeDeleted","providerKind":null,"schema":"bool","type":"Query<boolean>","wrapper":"Query"}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":"data.main","injectionKind":"provider","kind":"injection","name":"main","parameter":"database","providerKind":"sqlite","schema":null,"type":"Sqlite<\"main\">","wrapper":null}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"context","name":"RequestContext","parameter":"ctx","providerKind":null,"schema":null,"type":"RequestContext","wrapper":null})]); return await __sloppy_typed_handler(...__sloppy_args); } finally { await __sloppy_scope.dispose(); } })(ctx); } catch (error) { const __sloppy_problem = { status: 500, title: "Internal Server Error", code: "SLOPPY_E_HANDLER_ERROR" }; if (false) { __sloppy_problem.detail = String((error && error.message) ?? error); } return Results.problem(__sloppy_problem, { status: 500 }); } };
globalThis.__sloppy_register_handler(3, globalThis.__sloppy_handler_3);
globalThis.__sloppy_handler_4 = async function(ctx) { try { return await (async function(ctx) { const __sloppy_health_checks = [{ name: "database", check: () => true }, { name: "scheduler", check: function () { return true; } }]; const __sloppy_health_results = []; let __sloppy_health_ok = true; for (const __sloppy_health_check of __sloppy_health_checks) { try { const __sloppy_health_value = await __sloppy_health_check.check(ctx); const __sloppy_check_ok = __sloppy_health_value === undefined ? true : (typeof __sloppy_health_value === "boolean" ? __sloppy_health_value : (__sloppy_health_value && typeof __sloppy_health_value === "object" && typeof __sloppy_health_value.ok === "boolean" ? __sloppy_health_value.ok : true)); __sloppy_health_ok = __sloppy_health_ok && __sloppy_check_ok; __sloppy_health_results.push({ name: __sloppy_health_check.name, status: __sloppy_check_ok ? "healthy" : "unhealthy" }); } catch { __sloppy_health_ok = false; __sloppy_health_results.push({ name: __sloppy_health_check.name, status: "unhealthy" }); } } const __sloppy_health_body = { status: __sloppy_health_ok ? "healthy" : "unhealthy", checks: __sloppy_health_results }; return __sloppy_health_ok ? Results.ok(__sloppy_health_body) : Results.status(503, __sloppy_health_body); })(ctx); } catch (error) { const __sloppy_problem = { status: 500, title: "Internal Server Error", code: "SLOPPY_E_HANDLER_ERROR" }; if (false) { __sloppy_problem.detail = String((error && error.message) ?? error); } return Results.problem(__sloppy_problem, { status: 500 }); } };
globalThis.__sloppy_register_handler(4, globalThis.__sloppy_handler_4);
globalThis.__sloppy_handler_5 = async function(ctx) { try { return await (async function(ctx) { const __sloppy_health_checks = [{ name: "scheduler", check: function () { return true; } }]; const __sloppy_health_results = []; let __sloppy_health_ok = true; for (const __sloppy_health_check of __sloppy_health_checks) { try { const __sloppy_health_value = await __sloppy_health_check.check(ctx); const __sloppy_check_ok = __sloppy_health_value === undefined ? true : (typeof __sloppy_health_value === "boolean" ? __sloppy_health_value : (__sloppy_health_value && typeof __sloppy_health_value === "object" && typeof __sloppy_health_value.ok === "boolean" ? __sloppy_health_value.ok : true)); __sloppy_health_ok = __sloppy_health_ok && __sloppy_check_ok; __sloppy_health_results.push({ name: __sloppy_health_check.name, status: __sloppy_check_ok ? "healthy" : "unhealthy" }); } catch { __sloppy_health_ok = false; __sloppy_health_results.push({ name: __sloppy_health_check.name, status: "unhealthy" }); } } const __sloppy_health_body = { status: __sloppy_health_ok ? "healthy" : "unhealthy", checks: __sloppy_health_results }; return __sloppy_health_ok ? Results.ok(__sloppy_health_body) : Results.status(503, __sloppy_health_body); })(ctx); } catch (error) { const __sloppy_problem = { status: 500, title: "Internal Server Error", code: "SLOPPY_E_HANDLER_ERROR" }; if (false) { __sloppy_problem.detail = String((error && error.message) ?? error); } return Results.problem(__sloppy_problem, { status: 500 }); } };
globalThis.__sloppy_register_handler(5, globalThis.__sloppy_handler_5);
globalThis.__sloppy_handler_6 = async function(ctx) { try { return await (async function(ctx) { const __sloppy_health_checks = [{ name: "database", check: () => true }]; const __sloppy_health_results = []; let __sloppy_health_ok = true; for (const __sloppy_health_check of __sloppy_health_checks) { try { const __sloppy_health_value = await __sloppy_health_check.check(ctx); const __sloppy_check_ok = __sloppy_health_value === undefined ? true : (typeof __sloppy_health_value === "boolean" ? __sloppy_health_value : (__sloppy_health_value && typeof __sloppy_health_value === "object" && typeof __sloppy_health_value.ok === "boolean" ? __sloppy_health_value.ok : true)); __sloppy_health_ok = __sloppy_health_ok && __sloppy_check_ok; __sloppy_health_results.push({ name: __sloppy_health_check.name, status: __sloppy_check_ok ? "healthy" : "unhealthy" }); } catch { __sloppy_health_ok = false; __sloppy_health_results.push({ name: __sloppy_health_check.name, status: "unhealthy" }); } } const __sloppy_health_body = { status: __sloppy_health_ok ? "healthy" : "unhealthy", checks: __sloppy_health_results }; return __sloppy_health_ok ? Results.ok(__sloppy_health_body) : Results.status(503, __sloppy_health_body); })(ctx); } catch (error) { const __sloppy_problem = { status: 500, title: "Internal Server Error", code: "SLOPPY_E_HANDLER_ERROR" }; if (false) { __sloppy_problem.detail = String((error && error.message) ?? error); } return Results.problem(__sloppy_problem, { status: 500 }); } };
globalThis.__sloppy_register_handler(6, globalThis.__sloppy_handler_6);
globalThis.__sloppy_handler_7 = async function(ctx) { try { return await (() => Results.text("pong"))(ctx); } catch (error) { const __sloppy_problem = { status: 500, title: "Internal Server Error", code: "SLOPPY_E_HANDLER_ERROR" }; if (false) { __sloppy_problem.detail = String((error && error.message) ?? error); } return Results.problem(__sloppy_problem, { status: 500 }); } };
globalThis.__sloppy_register_handler(7, globalThis.__sloppy_handler_7);
