import { Config, Http, Results, schema, Sloppy } from "sloppy";

const BillingInvoice = schema.object({
    id: schema.string(),
    status: schema.string(),
    amount: schema.number(),
});

const Billing = Http.typedClient("billing", {
    baseUrl: Config.required("Billing:BaseUrl"),
    timeoutMs: 2000,
    endpoints: {
        getInvoice: Http.get("/invoices/{id}")
            .params(schema.object({ id: schema.string() }))
            .returns(200, BillingInvoice),
    },
});

const app = Sloppy.create();

app.services.addHttpClient(Billing);

app.get("/invoices/{id}", async (ctx) => {
    const billing = ctx.services.get(Billing);
    const invoice = await billing.getInvoice(
        { id: ctx.route.id },
        { signal: ctx.signal, correlationId: ctx.requestId },
    );

    return Results.json(invoice);
});

export { app, Billing };
