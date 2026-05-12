# Request Context Reference

Sloppy currently has two request-context shapes, depending on entry path.

## Framework Runtime Context (stdlib app API)

For stdlib `Sloppy.create()` routes, handler context is a plain object.

Default context (when handler is called without explicit context) includes:

- `services` (scope from `app.services.createScope()`)
- `capabilities`
- `config`
- `log`
- `request` with app-host method/path metadata when the route wrapper knows it
- `route` (empty object by default)
- `routeName`
- `routePattern`
- `urlFor(name, params?, query?)`
- `requestId` when `RequestId.defaults()` middleware is installed before the handler

If a caller provides context, handler receives that context instead.
The app-host decorates missing `config`, `log`, `capabilities`, `request`, and
route-pattern metadata so middleware can observe the same request shape.

## Compiler-Typed Context Bindings (`sloppyc`)

Typed handler wrappers map parameters through generated helper code:

- `Body<T>` -> `ctx.request.json()`
- `Route<T>` -> `ctx.route[name]`
- `Query<T>` -> `ctx.query[name]`
- `Header<"name", T>` -> `ctx.request.headers.get("name")`
- `Config<"KEY">` -> environment value, falling back to a compiler-recorded
  literal default when one exists
- `RequestContext`, `SlopRequest`, `SlopResponse`, `CancellationSignal`, `Deadline` -> `ctx`

Generated wrapper behavior also:

- creates a per-request services scope
- assigns `ctx.services` to that scope
- disposes scope in `finally`

## Typed Coercion Rules

Current generated coercion checks include:

- boolean targets: only `"true"` / `"false"`
- numeric/int targets: finite number parse required
- `PositiveInt`: integer and `> 0`

Coercion failures throw `TypeError` from generated runtime wrapper.

For `Body<T>` parameters backed by compiler-visible `ctx.body.validate(...)` or
`.accepts(...)` schema metadata, compiled/native runs validate the JSON body
before the generated wrapper calls `ctx.request.json()`. The wrapper still
materializes the JavaScript body object when the handler declares a body
parameter; routes that do not have schema metadata use the generic body helper
path.

## Parameter-Shape Limits (typed handlers)

Rejected shapes include:

- rest parameters
- destructuring parameters
- default-initialized parameters
- missing TypeScript type annotation on typed-parameter handlers

These fail with `SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS` (or related typed-binding diagnostics).
