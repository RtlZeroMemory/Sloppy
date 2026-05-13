# TestHost Package HTTP Mock

`TestHost.fromArtifacts(...)`, `TestHost.fromPackage(...)`, and loopback modes
accept `httpClients` overrides.

```js
import { TestHost, TestHttp } from "sloppy";

const billing = TestHttp.mock()
    .get("/invoices/inv_1")
    .replyJson(200, { id: "inv_1", status: "paid", amount: 42 });

const host = await TestHost.fromArtifacts(".sloppy", {
    httpClients: { billing },
});

await host.get("/route-that-calls-billing").expectStatus(200);
billing.expectCalled("GET", "/invoices/inv_1").expectNoUnexpectedCalls();
await host.close();
```

For artifact and package hosts, TestHost starts a local loopback mock server and
injects the generated base URL through config environment variables such as
`Billing__BaseUrl`. Handler execution in this example requires a V8-enabled
`sloppy` CLI; the process-host harness is covered by
`tests/bootstrap/test_testhost_process_modes.mjs`.
