import { Sloppy } from "sloppy";

const PolicyModule = Sloppy.module("core-policy").capabilities((capabilities) => {
    capabilities.addDatabase("data.main", {
        provider: "sqlite",
        access: "readwrite",
        metadata: { strict: true },
    });
});

export const policyMetadata = Object.freeze({
    filesystem: { mode: "strict", roots: ["data:/"] },
    network: { mode: "strict", connect: ["127.0.0.1:9000"] },
    process: { mode: "strict", run: ["status-tool"] },
});

const app = Sloppy.createBuilder()
    .addModule(PolicyModule)
    .build();

export default app;
