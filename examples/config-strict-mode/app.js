import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

const issuer = app.config.getString("Auth:Issuer");
const mode = app.config.getString("Sloppy:Config:Mode", "strict");

app.get("/strict-config", () => Results.text("strict config metadata is Plan-visible"));

export default app;
