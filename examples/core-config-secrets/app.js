import { Results, Sloppy } from "sloppy";

const app = Sloppy.create();

app.get("/config-status", () => {
    const endpoint = app.config.getString("Provider:Endpoint", "runtime:/provider.sock");
    const providerCredential = app.config.getSecret("Provider:Token");

    return Results.json({
        endpoint,
        credential: String(providerCredential),
    });
});

export default app;
