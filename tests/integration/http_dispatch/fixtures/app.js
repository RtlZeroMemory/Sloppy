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

globalThis.__sloppy_register_handler(6, function (ctx) {
  const bytes = ctx.request.body.bytes();
  let secondReadRejected = false;
  let secondReadError = null;
  try {
    ctx.request.body.text();
  } catch (error) {
    secondReadRejected = true;
    secondReadError = {
      name: String(error && error.name ? error.name : "Error"),
      message: String(error && error.message ? error.message : error),
    };
  }

  return Results.json({
    id: ctx.request.id,
    scheme: ctx.request.scheme,
    protocol: ctx.request.protocol,
    queryString: ctx.request.queryString,
    contentType: ctx.request.contentType,
    contentLength: ctx.request.contentLength,
    connectionId: ctx.connection.id,
    connection: `${ctx.connection.scheme}:${ctx.connection.protocol}:${ctx.connection.secure}`,
    bodyKind: ctx.request.body.kind,
    consumed: ctx.request.body.consumed,
    bytes: Array.from(bytes),
    secondReadRejected,
    secondReadError,
  });
});
