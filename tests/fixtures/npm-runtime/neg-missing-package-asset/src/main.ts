import probe from "missing-asset-pkg";

export async function main() {
    const text = (probe as () => string)();
    console.log(`should-not-print: ${text}`);
    return 0;
}
