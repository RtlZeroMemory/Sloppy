import { createRequire } from "node:module";

export async function main() {
    const localRequire = createRequire(import.meta.url);
    const resolved = localRequire.resolve("no-such-pkg");
    console.log(`should-not-print: ${resolved}`);
    return 0;
}
