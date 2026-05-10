const HOST = globalThis.Deno ? Deno.env.get("HOST") ?? "127.0.0.1" : process.env.HOST ?? "127.0.0.1";
const PORT = Number(globalThis.Deno ? Deno.env.get("PORT") ?? "5173" : process.env.PORT ?? "5173");
const ROUTE_COUNT = Number(globalThis.Deno ? Deno.env.get("ROUTE_COUNT") ?? "0" : process.env.ROUTE_COUNT ?? "0");
const PAYLOAD_64KB = "x".repeat(64 * 1024);

function json(value, headers = {}, status = 200) {
  return new Response(JSON.stringify(value), {
    status,
    headers: { "content-type": "application/json; charset=utf-8", ...headers },
  });
}

function text(value, headers = {}) {
  return new Response(value, {
    status: 200,
    headers: { "content-type": "text/plain; charset=utf-8", ...headers },
  });
}

function notFound() {
  return json({ status: 404, title: "Not Found" }, {}, 404);
}

function requestIdFrom(request) {
  return request.headers.get("x-request-id") || `req-${Date.now().toString(36)}`;
}

function contextFor(request, options) {
  const requestId = options.requestId ? requestIdFrom(request) : undefined;
  const headers = {};
  if (requestId && options.responseRequestId) {
    headers["x-request-id"] = requestId;
  }
  if (options.featureRich && request.headers.get("origin") === "https://app.example.com") {
    headers["access-control-allow-origin"] = "https://app.example.com";
    headers["access-control-expose-headers"] = "x-request-id";
  }
  return { request, requestId, headers };
}

function queryPayload(url) {
  return {
    q: url.searchParams.get("q") ?? "",
    page: Number(url.searchParams.get("page") ?? "0"),
    limit: Number(url.searchParams.get("limit") ?? "0"),
    results: [],
  };
}

async function handleKnownRoute(ctx, method, path, url) {
  if (method === "GET" && path === "/health") {
    return text("ok", ctx.headers);
  }
  if (method === "GET" && path === "/json") {
    return json({ message: "hello", ok: true, count: 42 }, ctx.headers);
  }
  if (method === "GET" && path.startsWith("/users/")) {
    const id = Number(path.slice("/users/".length));
    if (Number.isInteger(id)) {
      return json({ id, name: "Ada Lovelace" }, ctx.headers);
    }
  }
  if (method === "GET" && path === "/search") {
    return json(queryPayload(url), ctx.headers);
  }
  if (method === "POST" && path === "/echo") {
    const body = await ctx.request.json();
    return json({ name: body.name, count: body.count }, ctx.headers);
  }
  if (method === "GET" && path === "/middleware") {
    return json({ requestId: ctx.requestId ?? requestIdFrom(ctx.request) }, ctx.headers);
  }
  if (method === "GET" && path === "/payload/64kb") {
    return text(PAYLOAD_64KB, ctx.headers);
  }
  return null;
}

function buildRouteTable(options) {
  const routes = [
    { method: "GET", path: "/health", handler: (ctx) => text("ok", ctx.headers) },
    {
      method: "GET",
      path: "/json",
      handler: (ctx) => json({ message: "hello", ok: true, count: 42 }, ctx.headers),
    },
    { method: "GET", path: "/search", handler: (ctx, url) => json(queryPayload(url), ctx.headers) },
    {
      method: "GET",
      path: "/middleware",
      handler: (ctx) => json({ requestId: ctx.requestId ?? requestIdFrom(ctx.request) }, ctx.headers),
    },
    { method: "GET", path: "/payload/64kb", handler: (ctx) => text(PAYLOAD_64KB, ctx.headers) },
    {
      method: "POST",
      path: "/echo",
      handler: async (ctx) => {
        const body = await ctx.request.json();
        return json({ name: body.name, count: body.count }, ctx.headers);
      },
    },
    {
      method: "GET",
      path: "/users/:id",
      dynamic: true,
      handler: (ctx, _url, match) => json({ id: Number(match.id), name: "Ada Lovelace" }, ctx.headers),
    },
  ];
  for (let i = 0; i < ROUTE_COUNT; i += 1) {
    routes.push({
      method: "GET",
      path: `/routes/${i}`,
      handler: (ctx) => json({ route: i }, ctx.headers),
    });
  }
  if (options.featureRich) {
    routes.push({
      method: "OPTIONS",
      path: "/middleware",
      handler: (ctx) => new Response("", { status: 204, headers: ctx.headers }),
    });
  }
  return routes;
}

function routeTableLookup(routes, method, path) {
  for (const route of routes) {
    if (route.method !== method) {
      continue;
    }
    if (!route.dynamic && route.path === path) {
      return { route, match: undefined };
    }
    if (route.dynamic && route.path === "/users/:id" && path.startsWith("/users/")) {
      const id = path.slice("/users/".length);
      if (/^\d+$/.test(id)) {
        return { route, match: { id } };
      }
    }
  }
  return null;
}

export function fetchHandler(options = {}) {
  const mode = options.routing ?? "direct";
  const featureRich = options.featureRich === true;
  const requestId = options.requestId === true || featureRich;
  const responseRequestId = options.responseRequestId === true || featureRich;
  const routes = mode === "table" ? buildRouteTable({ featureRich }) : [];
  return async function handle(request) {
    try {
      const url = new URL(request.url);
      const ctx = contextFor(request, { featureRich, requestId, responseRequestId });
      if (mode === "direct") {
        const direct = await handleKnownRoute(ctx, request.method, url.pathname, url);
        return direct ?? notFound();
      }
      const found = routeTableLookup(routes, request.method, url.pathname);
      if (!found) {
        return notFound();
      }
      return await found.route.handler(ctx, url, found.match);
    } catch {
      return new Response(JSON.stringify({ status: 500, title: "Internal Server Error" }), {
        status: 500,
        headers: { "content-type": "application/json; charset=utf-8" },
      });
    }
  };
}

export { HOST, PORT };
