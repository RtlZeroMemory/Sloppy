import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

const jwtSecret = app.config.getSecret("Auth:JwtSecret");

app.get("/redaction", () => Results.text("secret config value is redacted in metadata"));

export default app;
