# Generated HTTP Client

This example shows the generated-client shape produced from a Sloppy OpenAPI
artifact. The generator returns JavaScript source that uses first-party
`Http.typedClient(...)` and `Config.required(...)`.

```js
import { File } from "sloppy/fs";
import { Http } from "sloppy/http";

const document = await File.readJson("openapi.json");
const generated = Http.generateClientFromOpenApi(document, {
    name: "Billing",
    baseUrlConfigKey: "Billing:BaseUrl",
});

await File.writeText("billing.client.js", generated.source);
```

`tests/bootstrap/test_http_client_factory.mjs` covers deterministic generated
output, importability, unsupported-schema warnings, and a generated client call
against `TestHttp.mock()`.
