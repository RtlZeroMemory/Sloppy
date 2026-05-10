export async function main(args, ctx) {
    const nameFlag = args.indexOf("--name");
    const name = nameFlag >= 0 && args[nameFlag + 1] ? args[nameFlag + 1] : "Sloppy";

    console.log(`Hello, ${name}.`);
    console.log(`cwd=${ctx.cwd}`);
    console.log(`environment=${ctx.environment}`);
    return 0;
}
