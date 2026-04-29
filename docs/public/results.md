# Results

Status: Tiny bootstrap helpers implemented.

Bootstrap status: `stdlib/sloppy/results.js` exports a frozen `Results` object with
`Results.text(...)` and `Results.json(...)`.

Purpose: document current result descriptor helpers and the future path where handler
return values become native response descriptors.

Implemented API example:

```ts
app.mapGet("/hello", () => Results.text("hello"));
app.mapGet("/health", () => Results.json({ status: "ok" }, { status: 200 }));
```

`examples/hello/app.js` uses `Results.text("Hello from Sloppy")` to demonstrate the current
text descriptor shape. No HTTP response writer consumes that descriptor yet.

`Results.text(body, options?)` returns a frozen plain descriptor:

```js
{
  __sloppyResult: true,
  kind: "text",
  status: 200,
  body: "hello",
  contentType: "text/plain; charset=utf-8"
}
```

`Results.json(value, options?)` returns a frozen plain descriptor without stringifying the
value:

```js
{
  __sloppyResult: true,
  kind: "json",
  status: 200,
  body: value,
  contentType: "application/json; charset=utf-8"
}
```

Both helpers accept `options.status`; status must be an integer from 100 to 999 and
defaults to `200`. `Results.text` stores `String(body)`. `Results.json` preserves the
provided JavaScript value as `body`; the descriptor is frozen, but object values are not
deep-frozen.

Not implemented yet: JSON serialization, response writing, headers, streaming, files,
HTML, problem details, content negotiation, native result conversion, and the rest of the
planned `Results.*` helper set.

Related internal docs: `docs/developer-ergonomics.md`, `docs/execution-model.md`.
