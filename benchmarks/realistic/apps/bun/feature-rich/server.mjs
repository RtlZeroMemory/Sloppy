import { HOST, PORT, fetchHandler } from "../../shared/fetch-http.mjs";

Bun.serve({ hostname: HOST, port: PORT, fetch: fetchHandler({ routing: "table", featureRich: true }) });
