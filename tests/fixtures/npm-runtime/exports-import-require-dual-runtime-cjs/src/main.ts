import { createRequire } from "node:module";

export async function main() {
    const localRequire = createRequire(import.meta.url);
    const value = localRequire("ir-dual") as { chose: string; value: string };
    console.log(`dual: chose=${value.chose} value=${value.value}`);
    return 0;
}
