const Results = Object.freeze({
  text(body, options) {
    return Object.freeze({
      __sloppyResult: true,
      kind: "text",
      status: options?.status ?? 200,
      body: String(body),
      contentType: options?.contentType ?? "text/plain; charset=utf-8",
    });
  },
  json(value, options) {
    return Object.freeze({
      __sloppyResult: true,
      kind: "json",
      status: options?.status ?? 200,
      body: value,
      contentType: options?.contentType ?? "application/json; charset=utf-8",
    });
  },
  ok(value, options) {
    return this.json(value, options);
  },
  noContent() {
    return Object.freeze({ __sloppyResult: true, kind: "empty", status: 204 });
  },
});

globalThis.__sloppy_handler_1 = () => Results.text("Hello from Sloppy");
