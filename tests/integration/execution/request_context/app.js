const Results = Object.freeze({
  json(value, options) {
    return Object.freeze({
      __sloppyResult: true,
      kind: "json",
      status: options?.status ?? 200,
      body: value,
      contentType: options?.contentType ?? "application/json; charset=utf-8",
    });
  },
});

globalThis.__sloppy_handler_1 = ({ route, query, request }) =>
  Results.json({
    id: route.id,
    q: query.q,
    path: request.path,
    rawTarget: request.rawTarget,
    method: request.method,
  });
