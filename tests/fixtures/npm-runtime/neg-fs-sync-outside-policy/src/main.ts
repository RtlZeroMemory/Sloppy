import { createRequire } from "node:module";

export async function main() {
    const localRequire = createRequire(import.meta.url);
    const fs = localRequire("node:fs") as { readFileSync(path: string, encoding: string): string };
    // Path outside any sealed package asset; must throw.
    const text = fs.readFileSync("/tmp/sloppy-runtime-not-sealed.txt", "utf8");
    console.log(`should-not-print: ${text}`);
    return 0;
}
