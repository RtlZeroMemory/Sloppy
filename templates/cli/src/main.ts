import { Environment, System } from "sloppy/os";
import { echo } from "./commands/echo.ts";
import { inspect } from "./commands/inspect.ts";

function printHelp() {
    console.log("Usage: sloppy run .sloppy -- <command> [args]");
    console.log("");
    console.log("Commands:");
    console.log("  --help             Show help");
    console.log("  echo <text>        Print text");
    console.log("  inspect <path>     Print basic file information");
    console.log("  env                Print platform information");
}

export async function main(args, ctx) {
    const command = args[0] || "--help";
    if (command === "--help" || command === "help") {
        printHelp();
        return 0;
    }
    if (command === "echo") {
        return echo(args.slice(1));
    }
    if (command === "inspect") {
        return inspect(args.slice(1));
    }
    if (command === "env") {
        console.log(JSON.stringify({
            cwd: ctx.cwd,
            environment: ctx.environment,
            platform: System.platform,
            homeSet: Boolean(Environment.get("HOME") || Environment.get("USERPROFILE")),
        }, null, 2));
        return 0;
    }
    console.error(`unknown command: ${command}`);
    printHelp();
    return 1;
}
