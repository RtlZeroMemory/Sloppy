const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Results, data, sql } = __sloppyRuntime;
const __sloppy_framework_services = (() => {
  const registrations = new Map();
  const singletonDisposables = [];
  let disposed = false;
  function validateToken(token) {
    if (typeof token !== "string" || token.length === 0) { throw new TypeError("Sloppy Framework service token must be a non-empty string."); }
  }
  function add(lifetime, token, factory) {
    validateToken(token);
    if (typeof factory !== "function") { throw new TypeError(`Sloppy Framework ${lifetime} service factory must be a function.`); }
    if (registrations.has(token)) { throw new Error(`sloppy: service '${token}' is already registered.`); }
    registrations.set(token, { lifetime, factory, initialized: false, value: undefined });
  }
  function disposeValue(value) {
    if (value === null || value === undefined) { return undefined; }
    if (typeof value[Symbol.dispose] === "function") { return value[Symbol.dispose](); }
    if (typeof value.dispose === "function") { return value.dispose(); }
    if (typeof value.close === "function") { return value.close(); }
    return undefined;
  }
  function createScope(context) {
    const scoped = new Map();
    const transient = [];
    const resolving = [];
    const resolvingLifetimes = [];
    let scopeDisposed = false;
    const scope = {
      context,
      get(token) { return resolve(scope, token); },
      track(value) { transient.push(value); return value; },
      async dispose() {
        if (scopeDisposed) { return; }
        scopeDisposed = true;
        for (const value of [...transient.reverse(), ...Array.from(scoped.values()).reverse()]) { await disposeValue(value); }
      },
      __disposed() { return scopeDisposed; },
      __hasScoped(token) { return scoped.has(token); },
      __getScoped(token) { return scoped.get(token); },
      __setScoped(token, value) { scoped.set(token, value); },
      __resolving() { return resolving; },
      __resolvingLifetimes() { return resolvingLifetimes; },
      __push(token, lifetime) { resolving.push(token); resolvingLifetimes.push(lifetime); },
      __pop() { resolving.pop(); resolvingLifetimes.pop(); }
    };
    return Object.freeze(scope);
  }
  function resolve(scope, token) {
    validateToken(token);
    if (disposed) { throw new Error("sloppy: service provider is disposed."); }
    if (scope.__disposed()) { throw new Error("sloppy: service scope is disposed."); }
    if (!registrations.has(token)) { throw new Error(`sloppy: service '${token}' is not registered.`); }
    const registration = registrations.get(token);
    if (scope.__resolving().includes(token)) { throw new Error(`sloppy: service circular dependency detected: ${[...scope.__resolving(), token].join(" -> ")}.`); }
    if (registration.lifetime === "scoped" && scope.__resolvingLifetimes().includes("singleton")) { throw new Error(`sloppy: singleton service cannot depend on scoped service '${token}'.`); }
    if (registration.lifetime === "singleton") {
      if (!registration.initialized) {
        scope.__push(token, "singleton");
        try { registration.value = registration.factory(scope); singletonDisposables.push(registration.value); registration.initialized = true; } finally { scope.__pop(); }
      }
      return registration.value;
    }
    if (registration.lifetime === "scoped") {
      if (!scope.__hasScoped(token)) {
        scope.__push(token, "scoped");
        try { scope.__setScoped(token, registration.factory(scope)); } finally { scope.__pop(); }
      }
      return scope.__getScoped(token);
    }
    scope.__push(token, "transient");
    try { const value = registration.factory(scope); scope.track(value); return value; } finally { scope.__pop(); }
  }
  return Object.freeze({
    addSingleton(token, factory) { add("singleton", token, factory); },
    addScoped(token, factory) { add("scoped", token, factory); },
    addTransient(token, factory) { add("transient", token, factory); },
    createScope,
    async dispose() {
      if (disposed) { return; }
      disposed = true;
      for (const value of singletonDisposables.reverse()) { await disposeValue(value); }
    }
  });
})();
function __sloppy_framework_arg(ctx, scope, binding) {
  if (binding.kind === "body.json") { return ctx.request.json(); }
  if (binding.kind === "context") { return ctx; }
  if (binding.kind === "injection") { return __sloppy_framework_injection(scope, binding); }
  if (binding.kind === "config") { throw new Error(`sloppy: Config injection for '${binding.name}' is unavailable in this runtime lane.`); }
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
  if (binding.injectionKind === "provider" && binding.providerKind === "postgres") { return scope.track(data.postgres.open({ provider: dependencyName })); }
  if (binding.injectionKind === "provider" && binding.providerKind === "sqlserver") { return scope.track(data.sqlserver.open({ provider: dependencyName })); }
  throw new Error(`sloppy: ${binding.injectionKind} injection for '${binding.name}' is unavailable in this runtime lane.`);
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
}; return await __sloppy_typed_handler(__sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"body.json","name":null,"parameter":"input","providerKind":null,"schema":"UserCreate","type":"UserCreate","wrapper":null}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":"data.main","injectionKind":"provider","kind":"injection","name":"main","parameter":"db","providerKind":"postgres","schema":null,"type":"Postgres<\"main\">","wrapper":null}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":"data.audit","injectionKind":"provider","kind":"injection","name":"audit","parameter":"audit","providerKind":"sqlite","schema":null,"type":"Sqlite<\"audit\">","wrapper":null}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":"data.search","injectionKind":"provider","kind":"injection","name":"search","parameter":"search","providerKind":"sqlserver","schema":null,"type":"SqlServer<\"search\">","wrapper":null}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":"queue.emails","injectionKind":"queue","kind":"injection","name":"emails","parameter":"emails","providerKind":"workqueue","schema":null,"type":"WorkQueue<\"emails\">","wrapper":null}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"context","name":"RequestContext","parameter":"ctx","providerKind":null,"schema":null,"type":"RequestContext","wrapper":null})); } finally { await __sloppy_scope.dispose(); } };
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
}; return await __sloppy_typed_handler(__sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"route","name":"id","parameter":"id","providerKind":null,"schema":"number","type":"Route<number>","wrapper":"Route"}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"header","name":"x-trace-id","parameter":"trace","providerKind":null,"schema":null,"type":"Header<\"x-trace-id\">","wrapper":"Header"}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"query","name":"includeDeleted","parameter":"includeDeleted","providerKind":null,"schema":"bool","type":"Query<boolean>","wrapper":"Query"}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"body.json","name":"input","parameter":"input","providerKind":null,"schema":"UserCreate","type":"Body<UserCreate>","wrapper":"Body"}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":"data.main","injectionKind":"provider","kind":"injection","name":"main","parameter":"db","providerKind":"postgres","schema":null,"type":"Postgres<\"main\">","wrapper":null}), __sloppy_framework_arg(ctx, __sloppy_scope, {"capability":null,"injectionKind":null,"kind":"context","name":"RequestContext","parameter":"ctx","providerKind":null,"schema":null,"type":"RequestContext","wrapper":null})); } finally { await __sloppy_scope.dispose(); } };
globalThis.__sloppy_register_handler(2, globalThis.__sloppy_handler_2);
