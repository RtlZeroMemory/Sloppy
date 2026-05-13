const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Results, schema, Schema, data } = __sloppyRuntime;

const UserCreate = schema.object({
  name: schema.string().min(1),
  email: schema.string().email().optional(),
  tags: schema.array(schema.string()).optional()
});

globalThis.__sloppy_handler_1 = (ctx) => Results.json({
  id: ctx.route.id,
  search: ctx.query.q,
  agent: ctx.header.userAgent,
  body: ctx.body.json(undefined)
});
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
globalThis.__sloppy_handler_2 = () => Results.text("ok");
globalThis.__sloppy_register_handler(2, globalThis.__sloppy_handler_2);
