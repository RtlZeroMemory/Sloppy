import assert from "node:assert/strict";

import { app } from "../http-client-typed/app.js";
import { TestHost, TestHttp } from "sloppy";

const billing = TestHttp.mock()
    .get("/invoices/inv_1")
    .replyJson(200, { id: "inv_1", status: "paid", amount: 42 });

const host = await TestHost.create(app, {
    config: {
        "Billing:BaseUrl": "http://billing.test",
    },
    httpClients: {
        billing,
    },
});

try {
    const response = await host.get("/invoices/inv_1").expectStatus(200);
    await response.expectJson({ id: "inv_1", status: "paid", amount: 42 });
    billing.expectCalled("GET", "/invoices/inv_1").expectNoUnexpectedCalls();
} finally {
    await host.close();
}

assert.equal(true, true);
