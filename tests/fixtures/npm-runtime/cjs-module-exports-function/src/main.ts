import greet from "module-exports-fn";

export async function main() {
    const message = greet("sloppy");
    console.log(`module-exports-fn: ${message}`);
    return 0;
}
