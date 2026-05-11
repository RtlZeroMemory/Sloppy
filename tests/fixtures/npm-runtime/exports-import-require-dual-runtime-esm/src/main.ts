import pkg from "ir-dual";

export async function main() {
    const value = pkg as { chose: string; value: string };
    console.log(`dual: chose=${value.chose} value=${value.value}`);
    return 0;
}
