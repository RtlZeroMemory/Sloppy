const largeList = () =>
  Array.from({ length: 256 }, (_, id) => ({ id, name: `user-${id}`, active: id % 2 === 0 }));

function json(body, status = 200) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "content-type": "application/json" },
  });
}

function validateLogin(value) {
  return value && typeof value.username === "string" && typeof value.password === "string";
}

const server = Bun.serve({
  hostname: "127.0.0.1",
  port: 0,
  async fetch(request) {
    const url = new URL(request.url);
    if (request.method === "GET" && url.pathname === "/large") {
      return json({ items: largeList() });
    }
    if (request.method === "GET" && url.pathname === "/static-json") {
      return json({ ok: true, mode: "static" });
    }
    if (request.method === "GET" && url.pathname === "/static-text") {
      return new Response("ok\n", {
        headers: { "content-type": "text/plain; charset=utf-8", "content-length": "3" },
      });
    }
    if (request.method === "GET" && url.pathname === "/static-status") {
      return new Response("", { status: 204 });
    }
    if (request.method === "GET" && url.pathname === "/static-problem") {
      return json({ status: 400, title: "Static problem", code: "SLOPPY_E_STATIC_PROBLEM" }, 400);
    }
    if (request.method === "GET" && url.pathname === "/dynamic-json") {
      return json({ ok: true, mode: "dynamic-0" });
    }
    if (request.method === "GET" && url.pathname === "/dynamic-text") {
      return new Response("dynamic-text\n", {
        headers: { "content-type": "text/plain; charset=utf-8", "content-length": "13" },
      });
    }
    if (request.method === "GET" && url.pathname === "/dynamic-async") {
      return json({ ok: true, mode: await Promise.resolve("async-dynamic") });
    }
    if (request.method === "GET" && url.pathname === "/ctx-query") {
      return json({ ok: true, query: url.searchParams.get("q") });
    }
    if (request.method === "GET" && url.pathname === "/ctx-headers") {
      return json({ ok: true, trace: request.headers.get("x-trace") });
    }
    if (request.method === "GET" && url.pathname === "/ctx-services") {
      return json({ ok: true, service: "bench-service" });
    }
    if (request.method === "GET" && url.pathname === "/plain-object") {
      return json({ ok: true, mode: "plain-object" });
    }
    if (request.method === "GET" && url.pathname === "/exception") {
      return json({ status: 500, title: "Internal Server Error" }, 500);
    }
    if (request.method === "GET" && url.pathname.startsWith("/route/")) {
      return json({ ok: true, route: url.pathname });
    }
    if (request.method !== "POST") {
      return new Response("", { status: 404 });
    }
    let parsed;
    try {
      parsed = await request.json();
    } catch {
      return json({ error: "malformed_json" }, 400);
    }
    if (url.pathname === "/small" && !validateLogin(parsed)) {
      return json({ error: "invalid_body" }, 400);
    }
    return json({ ok: true, echo: parsed });
  },
});

console.log(JSON.stringify({ port: server.port }));

process.on("SIGTERM", () => {
  server.stop(true);
  process.exit(0);
});
