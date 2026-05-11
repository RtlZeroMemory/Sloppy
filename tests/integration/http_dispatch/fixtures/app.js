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

globalThis.__sloppy_register_handler(7, async function () {
  try {
    throw new Error("SECRET_TOKEN_SHOULD_NOT_LEAK");
  } catch {
    return Results.problem({
      status: 500,
      title: "Internal Server Error",
      code: "SLOPPY_E_HANDLER_ERROR",
    }, { status: 500 });
  }
});

globalThis.__sloppy_register_handler(8, async function () {
  try {
    await Promise.reject(new Error("ASYNC_SECRET_SHOULD_NOT_LEAK"));
  } catch {
    return Results.problem({
      status: 500,
      title: "Internal Server Error",
      code: "SLOPPY_E_HANDLER_ERROR",
    }, { status: 500 });
  }
});

globalThis.__sloppy_register_handler(9, function (ctx) {
  return Results.json({
    session: ctx.cookies.get("session"),
    theme: ctx.cookies.get("theme"),
    missing: ctx.cookies.get("missing"),
  });
});

globalThis.__sloppy_register_handler(10, function () {
  return Results.ok({ ok: true })
    .cookie("session", "abc", { httpOnly: true, secure: true, sameSite: "Strict", path: "/" })
    .cookie("theme", "dark");
});

globalThis.__sloppy_register_handler(11, function (ctx) {
  const form = ctx.request.form();
  return Results.json({
    name: form.get("name"),
    repeated: form.get("repeated"),
    entries: Array.from(form.entries()),
  });
});

globalThis.__sloppy_register_handler(12, function (ctx) {
  const form = ctx.request.multipart();
  const file = form.file("avatar");
  return Results.json({
    title: form.get("title"),
    file: {
      fieldName: file.fieldName,
      name: file.name,
      contentType: file.contentType,
      size: file.size,
      text: file.text(),
      bytes: Array.from(file.bytes()),
    },
  });
});

globalThis.__sloppy_register_handler(13, async function () {
  return Results.stream(async (writer) => {
    writer.writeText("hello ");
    writer.writeBytes(new Uint8Array([119, 111, 114, 108, 100]));
  }, { contentType: "text/plain; charset=utf-8" });
});
