import http from "node:http";

const HOST = process.env.HOST ?? "127.0.0.1";
const PORT = Number(process.env.PORT ?? "5173");
const ROUTE_COUNT = Number(process.env.ROUTE_COUNT ?? "0");
const PAYLOAD_64KB = "x".repeat(64 * 1024);

function parseQuery(searchParams) {
  return {
    q: searchParams.get("q") ?? "",
    page: Number(searchParams.get("page") ?? "0"),
    limit: Number(searchParams.get("limit") ?? "0"),
    results: [],
  };
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on("data", (chunk) => chunks.push(chunk));
    req.on("end", () => resolve(Buffer.concat(chunks).toString("utf8")));
    req.on("error", reject);
  });
}

function requestIdFrom(req) {
  const incoming = req.headers["x-request-id"];
  if (typeof incoming === "string" && incoming.length > 0) {
    return incoming;
  }
  return `req-${Date.now().toString(36)}`;
}

function send(res, status, body, contentType, extraHeaders = {}) {
  const bytes = Buffer.byteLength(body);
  res.writeHead(status, {
    "content-type": contentType,
    "content-length": String(bytes),
    ...extraHeaders,
  });
  res.end(body);
}

function sendJson(res, value, extraHeaders = {}) {
  send(res, 200, JSON.stringify(value), "application/json; charset=utf-8", extraHeaders);
}

function sendJsonStatus(res, status, value, extraHeaders = {}) {
  send(res, status, JSON.stringify(value), "application/json; charset=utf-8", extraHeaders);
}

function sendText(res, value, extraHeaders = {}) {
  send(res, 200, value, "text/plain; charset=utf-8", extraHeaders);
}

function sendNotFound(res) {
  sendJsonStatus(res, 404, { status: 404, title: "Not Found" });
}

function makeContext(req, url, options) {
  const requestId = options.requestId ? requestIdFrom(req) : undefined;
  return {
    req,
    url,
    requestId,
    responseHeaders: requestId && options.responseRequestId ? { "x-request-id": requestId } : {},
  };
}

async function handleKnownRoute(ctx, res, path) {
  const { req, url } = ctx;
  if (req.method === "GET" && path === "/health") {
    sendText(res, "ok", ctx.responseHeaders);
    return true;
  }
  if (req.method === "GET" && path === "/json") {
    sendJson(res, { message: "hello", ok: true, count: 42 }, ctx.responseHeaders);
    return true;
  }
  if (req.method === "GET" && path.startsWith("/users/")) {
    const id = Number(path.slice("/users/".length));
    if (Number.isInteger(id)) {
      sendJson(res, { id, name: "Ada Lovelace" }, ctx.responseHeaders);
      return true;
    }
  }
  if (req.method === "GET" && path === "/search") {
    sendJson(res, parseQuery(url.searchParams), ctx.responseHeaders);
    return true;
  }
  if (req.method === "POST" && path === "/echo") {
    const body = JSON.parse(await readBody(req));
    sendJson(res, { name: body.name, count: body.count }, ctx.responseHeaders);
    return true;
  }
  if (req.method === "GET" && path === "/middleware") {
    sendJson(res, { requestId: ctx.requestId ?? requestIdFrom(req) }, ctx.responseHeaders);
    return true;
  }
  if (req.method === "GET" && path === "/payload/64kb") {
    sendText(res, PAYLOAD_64KB, ctx.responseHeaders);
    return true;
  }
  return false;
}

function buildRouteTable(options) {
  const routes = [
    { method: "GET", path: "/health", handler: (ctx, res) => sendText(res, "ok", ctx.responseHeaders) },
    {
      method: "GET",
      path: "/json",
      handler: (ctx, res) =>
        sendJson(res, { message: "hello", ok: true, count: 42 }, ctx.responseHeaders),
    },
    {
      method: "GET",
      path: "/search",
      handler: (ctx, res) => sendJson(res, parseQuery(ctx.url.searchParams), ctx.responseHeaders),
    },
    {
      method: "GET",
      path: "/middleware",
      handler: (ctx, res) =>
        sendJson(res, { requestId: ctx.requestId ?? requestIdFrom(ctx.req) }, ctx.responseHeaders),
    },
    {
      method: "GET",
      path: "/payload/64kb",
      handler: (ctx, res) => sendText(res, PAYLOAD_64KB, ctx.responseHeaders),
    },
  ];
  routes.push({
    method: "POST",
    path: "/echo",
    handler: async (ctx, res) => {
      const body = JSON.parse(await readBody(ctx.req));
      sendJson(res, { name: body.name, count: body.count }, ctx.responseHeaders);
    },
  });
  routes.push({
    method: "GET",
    path: "/users/:id",
    dynamic: true,
    handler: (ctx, res, match) =>
      sendJson(res, { id: Number(match.id), name: "Ada Lovelace" }, ctx.responseHeaders),
  });
  for (let i = 0; i < ROUTE_COUNT; i += 1) {
    routes.push({
      method: "GET",
      path: `/routes/${i}`,
      handler: (ctx, res) => sendJson(res, { route: i }, ctx.responseHeaders),
    });
  }
  if (options.featureRich) {
    routes.push({
      method: "OPTIONS",
      path: "/middleware",
      handler: (ctx, res) => send(res, 204, "", "text/plain; charset=utf-8", ctx.responseHeaders),
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

export function createNodeServer(options = {}) {
  const mode = options.routing ?? "direct";
  const featureRich = options.featureRich === true;
  const requestId = options.requestId === true || featureRich;
  const responseRequestId = options.responseRequestId === true || featureRich;
  const routes = mode === "table" ? buildRouteTable({ featureRich }) : [];
  const server = http.createServer(async (req, res) => {
    try {
      const url = new URL(req.url ?? "/", `http://${req.headers.host ?? `${HOST}:${PORT}`}`);
      const ctx = makeContext(req, url, { requestId, responseRequestId });
      if (featureRich && req.headers.origin === "https://app.example.com") {
        ctx.responseHeaders["access-control-allow-origin"] = "https://app.example.com";
        ctx.responseHeaders["access-control-expose-headers"] = "x-request-id";
      }

      if (mode === "direct") {
        if (await handleKnownRoute(ctx, res, url.pathname)) {
          return;
        }
      } else {
        const found = routeTableLookup(routes, req.method ?? "GET", url.pathname);
        if (found) {
          await found.route.handler(ctx, res, found.match);
          return;
        }
      }
      sendNotFound(res);
    } catch (error) {
      send(res, 500, JSON.stringify({ status: 500, title: "Internal Server Error" }),
        "application/json; charset=utf-8");
    }
  });
  server.listen(PORT, HOST);
}
