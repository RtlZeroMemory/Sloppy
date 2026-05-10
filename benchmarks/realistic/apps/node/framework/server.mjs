import { createNodeServer } from "../../shared/node-http.mjs";

createNodeServer({ routing: "table", requestId: true, responseRequestId: true });
