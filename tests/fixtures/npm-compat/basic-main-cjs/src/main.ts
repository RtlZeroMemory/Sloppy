import greet from "greet-cjs";

export async function main() {
    const message = greet("npm-compat");
    console.log(message);
    return 0;
}
