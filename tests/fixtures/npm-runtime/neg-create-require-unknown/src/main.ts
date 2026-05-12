import { createRequire } from "node:module";

export async function main() {
    const localRequire = createRequire(import.meta.url);
    const value = localRequire("no-such-pkg");
    console.log(`should-not-print: ${JSON.stringify(value)}`);
    return 0;
}
