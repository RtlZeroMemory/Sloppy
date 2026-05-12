import { createRequire } from "node:module";

export async function main() {
    const localRequire = createRequire(import.meta.url);
    const computed = ["dyn-target-pkg/", "missing"].join("");
    const value = localRequire(computed);
    console.log(`should-not-print: ${JSON.stringify(value)}`);
    return 0;
}
