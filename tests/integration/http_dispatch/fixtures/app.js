const { Results } = globalThis.__sloppy_runtime;

globalThis.__sloppy_register_handler(1, function () {
  return "sloppy-ok";
});

globalThis.__sloppy_register_handler(3, function () {
  throw new Error("dispatch boom");
});

globalThis.__sloppy_register_handler(4, function (ctx) {
  return Results.json({
    method: ctx.request.method,
  });
});

globalThis.__sloppy_register_handler(5, function (ctx) {
  return Results.json({
    contentType: ctx.request.headers.get("content-type"),
    trace: ctx.request.headers.get("X-Trace"),
    text: ctx.request.text(),
    body: ctx.request.json(),
  });
});
