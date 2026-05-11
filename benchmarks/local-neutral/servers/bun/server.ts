const host = process.env.BENCH_HOST ?? "127.0.0.1";
const port = Number(process.env.BENCH_PORT ?? "41000");
const staticText = "hello static";

function json(value: unknown, status = 200) {
  return new Response(JSON.stringify(value), { status, headers: { "content-type": "application/json" } });
}

function validUser(input: any) {
  return input && typeof input.name === "string" && input.name.length > 0 &&
    input.name.length <= 100 && typeof input.email === "string" && input.email.includes("@");
}

Bun.serve({
  hostname: host,
  port,
  async fetch(req) {
    const url = new URL(req.url);
    if (req.method === "GET" && url.pathname === "/health") return new Response("ok", { headers: { "content-type": "text/plain" } });
    if (req.method === "GET" && url.pathname === "/json-small") return json({ ok: true, message: "hello", count: 3 });
    if (req.method === "GET" && url.pathname === "/users/123") return json({ id: "123", name: "Ada" });
    if (req.method === "POST" && url.pathname === "/users") {
      try {
        const body = await req.json();
        if (!validUser(body)) return json({ error: "invalid user" }, 400);
        return json({ id: 1, name: body.name, email: body.email });
      } catch {
        return json({ error: "invalid json" }, 400);
      }
    }
    if (req.method === "GET" && url.pathname === "/private") {
      if (req.headers.get("x-api-key") !== "benchmark-secret") return json({ error: "unauthorized" }, 401);
      return json({ ok: true, sub: "api-key" });
    }
    if (req.method === "GET" && url.pathname === "/public/hello.txt") {
      return new Response(staticText, { headers: { "content-type": "text/plain" } });
    }
    return json({ error: "not found" }, 404);
  },
});
