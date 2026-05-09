# HTTP Runtime Internals

## Where It Lives

- `include/sloppy/http.h`
- `include/sloppy/http_dispatch.h`
- `src/core/http.c`
- `src/core/http_dispatch.c`
- `src/platform/libuv/http_transport_libuv.c`

## Model

The current HTTP runtime validates methods, matches routes, performs request
validation, and dispatches handlers through Plan metadata. Transport work is
bounded and evidence-scoped.

## Limits

Production-edge HTTP, public streaming APIs, TLS, and broad middleware behavior
are not claimed by the current runtime.
