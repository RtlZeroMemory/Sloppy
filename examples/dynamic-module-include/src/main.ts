export async function main(args) {
    const name = args[0] ?? "alpha";
    const plugin = await import("./plugins/" + name + ".js");
    console.log(plugin.describe());
    return 0;
}
