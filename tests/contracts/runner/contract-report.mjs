import { redactText, redactValue } from "./redaction-utils.mjs";

const STATUSES = new Set(["pass", "fail", "skip", "unavailable"]);
const SEVERITIES = new Set(["info", "warning", "error"]);

export function createFinding({
    id,
    status,
    severity = status === "fail" ? "error" : "info",
    subsystem,
    invariant,
    fixture,
    path,
    message,
    details,
}) {
    if (!STATUSES.has(status)) {
        throw new Error(`invalid contract finding status: ${status}`);
    }
    if (!SEVERITIES.has(severity)) {
        throw new Error(`invalid contract finding severity: ${severity}`);
    }
    const finding = {
        id,
        status,
        severity,
        subsystem,
        invariant,
        message: redactText(message),
    };
    if (fixture !== undefined) {
        finding.fixture = fixture;
    }
    if (path !== undefined) {
        finding.path = redactText(path);
    }
    if (details !== undefined) {
        finding.details = redactValue(details);
    }
    return finding;
}

export function summarizeFindings(findings) {
    const summary = {
        pass: 0,
        fail: 0,
        warning: 0,
        skip: 0,
        unavailable: 0,
    };
    for (const finding of findings) {
        if (finding.status === "pass") {
            summary.pass += 1;
        } else if (finding.status === "fail") {
            summary.fail += 1;
        } else if (finding.status === "skip") {
            summary.skip += 1;
        } else if (finding.status === "unavailable") {
            summary.unavailable += 1;
        }
        if (finding.severity === "warning") {
            summary.warning += 1;
        }
    }
    return summary;
}

export function createReport({ subsystem, tier, startedAt, finishedAt, findings }) {
    const orderedFindings = [...findings].sort((left, right) => {
        const fixture = (left.fixture ?? "").localeCompare(right.fixture ?? "");
        if (fixture !== 0) {
            return fixture;
        }
        return left.id.localeCompare(right.id);
    });
    return {
        schemaVersion: 1,
        subsystem,
        tier,
        startedAt,
        finishedAt,
        findings: orderedFindings,
        summary: summarizeFindings(orderedFindings),
    };
}

export function mergeReports({ subsystem, tier, startedAt, finishedAt, reports }) {
    return createReport({
        subsystem,
        tier,
        startedAt,
        finishedAt,
        findings: reports.flatMap((report) => report.findings),
    });
}

export function hasBlockingFindings(report) {
    return report.findings.some((finding) => finding.status === "fail" || finding.severity === "error");
}

export function formatMarkdown(report) {
    const lines = [
        `# Contract Report: ${report.subsystem}`,
        "",
        `Tier: \`${report.tier}\``,
        "",
        "| Status | Count |",
        "| --- | ---: |",
        `| pass | ${report.summary.pass} |`,
        `| fail | ${report.summary.fail} |`,
        `| warning | ${report.summary.warning} |`,
        `| skip | ${report.summary.skip} |`,
        `| unavailable | ${report.summary.unavailable} |`,
        "",
        "| Status | Severity | Fixture | Invariant | Message |",
        "| --- | --- | --- | --- | --- |",
    ];
    for (const finding of report.findings) {
        lines.push(
            `| ${finding.status} | ${finding.severity} | ${finding.fixture ?? ""} | ${finding.invariant} | ${finding.message.replaceAll("|", "\\|")} |`,
        );
    }
    return `${lines.join("\n")}\n`;
}
