import { HOST, PORT, fetchHandler } from "../../shared/fetch-http.mjs";

Deno.serve({ hostname: HOST, port: PORT }, fetchHandler({ routing: "table", featureRich: true }));
