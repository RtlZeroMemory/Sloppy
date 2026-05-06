import { HttpClient } from "sloppy/net";

const billing = HttpClient.create({
    baseUrl: "https://billing.example.test",
    timeoutMs: 5000,
    maxResponseBytes: "4mb",
    headers: {
        "user-agent": "sloppy-http-client-example"
    },
    redirects: {
        enabled: true,
        max: 5,
        crossOriginSensitiveHeaders: "strip"
    },
    pool: {
        maxConnectionsPerOrigin: 8,
        idleTimeoutMs: 30000
    },
    network: {
        strict: true,
        allow: ["https://billing.example.test"]
    }
});

async function loadHealth() {
    const response = await billing.get("/health", { timeoutMs: 2000 });
    return await response.json();
}

async function createInvoice(customerId) {
    const response = await billing.postJson("/invoices", { customerId });
    if (response.status !== 201) {
        throw new Error(`invoice create failed with status ${response.status}`);
    }
    return await response.json();
}

async function loadTextStatus() {
    return await HttpClient.text("http://127.0.0.1:8080/status", {
        timeoutMs: 1000,
        redirects: { enabled: false },
        maxResponseBytes: 4096
    });
}

export { billing, createInvoice, loadHealth, loadTextStatus };
