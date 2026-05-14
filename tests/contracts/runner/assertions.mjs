import { createFinding } from "./contract-report.mjs";

export class ContractAssertionCollector {
    constructor({ subsystem, fixture }) {
        this.subsystem = subsystem;
        this.fixture = fixture;
        this.findings = [];
    }

    pass(invariant, message, details = undefined) {
        this.findings.push(
            createFinding({
                id: `${this.subsystem}.${this.fixture}.${invariant}`,
                status: "pass",
                severity: "info",
                subsystem: this.subsystem,
                invariant,
                fixture: this.fixture,
                message,
                details,
            }),
        );
    }

    warn(invariant, message, details = undefined) {
        this.findings.push(
            createFinding({
                id: `${this.subsystem}.${this.fixture}.${invariant}`,
                status: "pass",
                severity: "warning",
                subsystem: this.subsystem,
                invariant,
                fixture: this.fixture,
                message,
                details,
            }),
        );
    }

    fail(invariant, message, details = undefined) {
        this.findings.push(
            createFinding({
                id: `${this.subsystem}.${this.fixture}.${invariant}`,
                status: "fail",
                severity: "error",
                subsystem: this.subsystem,
                invariant,
                fixture: this.fixture,
                message,
                details,
            }),
        );
    }

    unavailable(invariant, message, details = undefined) {
        this.findings.push(
            createFinding({
                id: `${this.subsystem}.${this.fixture}.${invariant}`,
                status: "unavailable",
                severity: "info",
                subsystem: this.subsystem,
                invariant,
                fixture: this.fixture,
                message,
                details,
            }),
        );
    }
}

export function errorInvariants(findings) {
    return findings
        .filter((finding) => finding.status === "fail" && finding.severity === "error")
        .map((finding) => finding.invariant)
        .sort();
}
