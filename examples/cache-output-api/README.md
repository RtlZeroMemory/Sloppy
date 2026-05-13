# Cache Output API Example

This example separates server-side OutputCache from HTTP cache headers.

- `GET /products?category=books&page=1` is cached by Sloppy on the server.
- `GET /assets/config.json` emits `Cache-Control`, `Vary`, and `ETag` headers
  for clients and proxies, but does not use server-side output cache.

OutputCache keys use the route pattern and configured vary fields, not raw URLs
or secret headers.
