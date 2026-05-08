import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

const auth = app.config.bind("Auth", {
  jwtSecret: { type: "secret", required: false },
  tokenTtlMinutes: { type: "number", default: 60, min: 1, max: 1440 },
  issuer: { type: "string", required: true },
});

app.get("/config", () => Results.text("configuration metadata is Plan-visible"));

export default app;
