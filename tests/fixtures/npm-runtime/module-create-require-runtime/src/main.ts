import { createRequire } from "node:module";

export async function main() {
    const localRequire = createRequire(import.meta.url);
    const json = localRequire("subpath-pkg/data.json") as { kind: string };
    const subpath = localRequire("subpath-pkg/feature") as { value: string };
    let resolveOk = false;
    try {
        const resolved = localRequire.resolve("subpath-pkg/feature");
        resolveOk = typeof resolved === "string" && resolved.length > 0;
    } catch {
        resolveOk = false;
    }
    console.log(`createRequire: json.kind=${json.kind} subpath=${subpath.value} resolveOk=${resolveOk}`);
    return 0;
}
