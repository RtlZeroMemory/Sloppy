#!/usr/bin/env node
import { parseRunnerArgs, printHelp, repoRootFromRunner, writeJsonReport } from "./command-utils.mjs";
import { formatMarkdown, hasBlockingFindings, mergeReports } from "./contract-report.mjs";
import { runDataContract } from "../data/validate-data-contract.mjs";
import { runPackageContract } from "../package/validate-package-contract.mjs";

const AREA_RUNNERS = new Map([
    ["data", runDataContract],
    ["package", runPackageContract],
]);

async function main() {
    let options;
    try {
        options = parseRunnerArgs(process.argv.slice(2));
    } catch (error) {
        process.stderr.write(`contract-runner: ${error.message}\n`);
        process.exit(2);
    }
    if (options.help) {
        printHelp();
        return;
    }

    const repoRoot = repoRootFromRunner();
    const areas = options.area === "all" ? [...AREA_RUNNERS.keys()] : [options.area];
    const startedAt = new Date().toISOString();
    const reports = [];
    for (const area of areas) {
        const runner = AREA_RUNNERS.get(area);
        if (runner === undefined) {
            throw new Error(`no contract runner registered for area: ${area}`);
        }
        reports.push(await runner({ repoRoot, tier: options.tier }));
    }
    const finishedAt = new Date().toISOString();
    const report =
        reports.length === 1
            ? reports[0]
            : mergeReports({
                  subsystem: "all",
                  tier: options.tier,
                  startedAt,
                  finishedAt,
                  reports,
              });

    await writeJsonReport(options.out, report, repoRoot);
    process.stdout.write(options.format === "markdown" ? formatMarkdown(report) : `${JSON.stringify(report, null, 2)}\n`);

    if (hasBlockingFindings(report)) {
        process.exit(1);
    }
}

main().catch((error) => {
    process.stderr.write(`contract-runner: ${error.stack ?? error.message}\n`);
    process.exit(1);
});
