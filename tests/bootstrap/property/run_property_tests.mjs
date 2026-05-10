import { runTargets } from "../../fuzz/js_fuzz_targets.mjs";

function parseArgs(argv) {
    const options = {
        seed: 12345,
        iterations: 1000,
    };
    for (let index = 0; index < argv.length; index += 1) {
        const arg = argv[index];
        switch (arg) {
            case "--seed":
                options.seed = Number.parseInt(argv[++index] ?? "", 10);
                break;
            case "--iterations":
                options.iterations = Number.parseInt(argv[++index] ?? "", 10);
                break;
            case "-h":
            case "--help":
                options.help = true;
                break;
            default:
                throw new Error(`unknown option '${arg}'`);
        }
    }
    if (!Number.isSafeInteger(options.seed)) {
        throw new Error("--seed must be an integer");
    }
    if (!Number.isSafeInteger(options.iterations) || options.iterations < 1) {
        throw new Error("--iterations must be a positive integer");
    }
    return options;
}

function usage() {
    return "Usage: node tests/bootstrap/property/run_property_tests.mjs [--seed N] [--iterations N]";
}

async function main() {
    const options = parseArgs(process.argv.slice(2));
    if (options.help) {
        console.log(usage());
        return;
    }
    await runTargets({
        all: true,
        seed: options.seed,
        iterations: options.iterations,
        failureRoot: "artifacts/property/failures",
        reproCommand: `node tests/bootstrap/property/run_property_tests.mjs --seed ${options.seed} --iterations ${options.iterations}`,
    });
}

main().catch((error) => {
    console.error(error?.stack ?? String(error));
    process.exitCode = 1;
});
