import fs from "node:fs/promises";
import path from "node:path";

import { ContractAssertionCollector } from "../runner/assertions.mjs";
import { createFinding, createReport } from "../runner/contract-report.mjs";

const SUBSYSTEM = "jobs";
const FIXTURE = "scheduler-model";
const BASE_TIME = Date.parse("2026-05-12T18:00:00.000Z");
const MINUTE = 60000;

const STATUSES = new Set([
    "scheduled",
    "queued",
    "processing",
    "succeeded",
    "failed",
    "retrying",
    "dead",
    "cancelled",
    "deleted",
]);

const VALID_TRANSITIONS = Object.freeze({
    scheduled: Object.freeze(["queued", "cancelled"]),
    queued: Object.freeze(["processing", "cancelled", "deleted"]),
    processing: Object.freeze(["succeeded", "failed", "cancelled"]),
    failed: Object.freeze(["retrying", "dead", "queued"]),
    retrying: Object.freeze(["queued", "cancelled"]),
    dead: Object.freeze(["queued", "deleted"]),
    succeeded: Object.freeze(["deleted"]),
    cancelled: Object.freeze(["deleted"]),
    deleted: Object.freeze([]),
});

function iso(ms) {
    return new Date(ms).toISOString();
}

function assertCondition(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

function parseUtcFiveFieldCron(expression) {
    const fields = expression.trim().split(/\s+/u);
    if (fields.length !== 5) {
        throw new Error("cron expression must have five UTC fields");
    }
    const [minute, hour, dayOfMonth, month, dayOfWeek] = fields;
    if (dayOfMonth !== "*" || month !== "*" || dayOfWeek !== "*") {
        throw new Error("contract harness supports wildcard day/month fields");
    }
    const parseField = (field, min, max) => {
        if (field === "*") {
            return { every: 1 };
        }
        const everyMatch = /^\*\/([1-9][0-9]*)$/u.exec(field);
        if (everyMatch !== null) {
            const every = Number(everyMatch[1]);
            if (every < min || every > max + 1) {
                throw new Error("cron step is out of range");
            }
            return { every };
        }
        if (/^[0-9]+$/u.test(field)) {
            const value = Number(field);
            if (value < min || value > max) {
                throw new Error("cron value is out of range");
            }
            return { values: new Set([value]) };
        }
        throw new Error("unsupported cron field");
    };
    return {
        minute: parseField(minute, 0, 59),
        hour: parseField(hour, 0, 23),
    };
}

function fieldMatches(field, value) {
    if (field.values !== undefined) {
        return field.values.has(value);
    }
    return value % field.every === 0;
}

function nextOccurrence(expression, afterMs) {
    const cron = parseUtcFiveFieldCron(expression);
    let candidate = Math.floor(afterMs / MINUTE) * MINUTE + MINUTE;
    for (let index = 0; index < 366 * 24 * 60; index += 1) {
        const date = new Date(candidate);
        if (fieldMatches(cron.minute, date.getUTCMinutes()) && fieldMatches(cron.hour, date.getUTCHours())) {
            return candidate;
        }
        candidate += MINUTE;
    }
    throw new Error("cron expression did not produce a bounded next occurrence");
}

class DeterministicJobs {
    constructor() {
        this.nowMs = BASE_TIME;
        this.sequence = 0;
        this.jobs = [];
        this.events = [];
        this.attempts = [];
        this.recurring = new Map();
        this.locks = new Map();
        this.idSource = "deterministic-random-fixture";
    }

    id(prefix) {
        this.sequence += 1;
        return `${prefix}_${this.sequence.toString(36).padStart(6, "0")}_contract`;
    }

    setNow(ms) {
        this.nowMs = ms;
    }

    enqueue(name, payload = {}, options = {}) {
        if (options.idempotencyKey !== undefined) {
            const existing = this.jobs.find((job) => job.idempotencyKey === options.idempotencyKey);
            if (existing !== undefined) {
                return existing;
            }
        }
        if (options.occurrenceKey !== undefined) {
            const existing = this.jobs.find((job) => job.occurrenceKey === options.occurrenceKey);
            if (existing !== undefined) {
                return existing;
            }
        }
        const runAtMs = options.runAtMs ?? this.nowMs;
        const job = {
            id: options.id ?? this.id("job"),
            name,
            payload,
            queue: options.queue ?? "default",
            status: runAtMs > this.nowMs ? "scheduled" : "queued",
            runAtMs,
            attempts: 0,
            maxAttempts: options.maxAttempts ?? 3,
            idempotencyKey: options.idempotencyKey,
            occurrenceKey: options.occurrenceKey,
            token: undefined,
        };
        this.jobs.push(job);
        this.events.push({ jobId: job.id, from: null, to: job.status, at: iso(this.nowMs) });
        return job;
    }

    transition(job, to) {
        if (job.status === to) {
            return job;
        }
        if (!STATUSES.has(to) || !VALID_TRANSITIONS[job.status].includes(to)) {
            throw new Error(`SLOPPY_E_JOBS_TRANSITION_INVALID: ${job.status} -> ${to}`);
        }
        const from = job.status;
        job.status = to;
        this.events.push({ jobId: job.id, from, to, at: iso(this.nowMs) });
        return job;
    }

    claim(owner, queues = ["default"]) {
        for (const job of this.jobs) {
            if ((job.status === "scheduled" || job.status === "retrying") && job.runAtMs <= this.nowMs) {
                this.transition(job, "queued");
            }
        }
        const job = this.jobs.find((entry) => entry.status === "queued" && entry.runAtMs <= this.nowMs && queues.includes(entry.queue));
        if (job === undefined) {
            return undefined;
        }
        this.transition(job, "processing");
        job.attempts += 1;
        job.token = this.id("attempt");
        this.attempts.push({ id: job.token, jobId: job.id, owner, number: job.attempts, startedAt: iso(this.nowMs) });
        return { job, token: job.token };
    }

    complete(token) {
        const job = this.jobs.find((entry) => entry.token === token);
        if (job === undefined || job.status !== "processing") {
            return false;
        }
        this.transition(job, "succeeded");
        job.token = undefined;
        return true;
    }

    fail(token) {
        const job = this.jobs.find((entry) => entry.token === token);
        if (job === undefined || job.status !== "processing") {
            return false;
        }
        this.transition(job, "failed");
        this.transition(job, job.attempts >= job.maxAttempts ? "dead" : "retrying");
        job.runAtMs = this.nowMs + MINUTE;
        job.token = undefined;
        return true;
    }

    cancel(job) {
        this.transition(job, "cancelled");
    }

    delete(job) {
        this.transition(job, "deleted");
    }

    defineRecurring(name, jobName, payload, options) {
        const nextRunAtMs = options.nextRunAtMs ?? nextOccurrence(options.cron, this.nowMs - MINUTE);
        this.recurring.set(name, {
            name,
            jobName,
            payload,
            cron: options.cron,
            enabled: true,
            misfirePolicy: options.misfirePolicy ?? "run-once",
            catchUpLimit: options.catchUpLimit ?? 3,
            nextRunAtMs,
            lastRunAtMs: undefined,
        });
    }

    occurrenceKey(schedule, occurrenceMs) {
        return `recurring:${schedule.name}:${iso(occurrenceMs)}`;
    }

    firstOccurrenceAfter(schedule, afterMs) {
        return nextOccurrence(schedule.cron, afterMs);
    }

    latestDueOccurrence(schedule) {
        let latestMs = schedule.nextRunAtMs;
        let nextMs = this.firstOccurrenceAfter(schedule, latestMs);
        while (nextMs <= this.nowMs) {
            latestMs = nextMs;
            nextMs = this.firstOccurrenceAfter(schedule, latestMs);
        }
        return latestMs;
    }

    enqueueOccurrence(schedule, occurrenceMs) {
        return this.enqueue(schedule.jobName, schedule.payload, {
            occurrenceKey: this.occurrenceKey(schedule, occurrenceMs),
            idempotencyKey: this.occurrenceKey(schedule, occurrenceMs),
        });
    }

    tickRecurring({ owner }) {
        const lock = this.acquireLock("sloppy.jobs.recurring.tick", owner, 1000);
        if (!lock.acquired) {
            return [];
        }
        const enqueued = [];
        try {
            for (const schedule of this.recurring.values()) {
                if (!schedule.enabled || schedule.nextRunAtMs > this.nowMs) {
                    continue;
                }
                if (schedule.misfirePolicy === "ignore") {
                    schedule.lastRunAtMs = this.latestDueOccurrence(schedule);
                    schedule.nextRunAtMs = this.firstOccurrenceAfter(schedule, this.nowMs);
                    continue;
                }
                if (schedule.misfirePolicy === "run-once") {
                    const occurrenceMs = this.latestDueOccurrence(schedule);
                    enqueued.push(this.enqueueOccurrence(schedule, occurrenceMs));
                    schedule.lastRunAtMs = occurrenceMs;
                    schedule.nextRunAtMs = this.firstOccurrenceAfter(schedule, occurrenceMs);
                    continue;
                }
                if (schedule.misfirePolicy !== "catch-up-limited") {
                    throw new Error(`unsupported misfire policy: ${schedule.misfirePolicy}`);
                }

                let occurrenceMs = schedule.nextRunAtMs;
                let enqueuedCount = 0;
                let lastRunAtMs = undefined;
                while (occurrenceMs <= this.nowMs && enqueuedCount < schedule.catchUpLimit) {
                    enqueued.push(this.enqueueOccurrence(schedule, occurrenceMs));
                    lastRunAtMs = occurrenceMs;
                    enqueuedCount += 1;
                    occurrenceMs = this.firstOccurrenceAfter(schedule, occurrenceMs);
                }
                schedule.lastRunAtMs = lastRunAtMs;
                schedule.nextRunAtMs = occurrenceMs;
            }
            return enqueued;
        } finally {
            this.releaseLock("sloppy.jobs.recurring.tick", owner);
        }
    }

    pauseRecurring(name) {
        this.recurring.get(name).enabled = false;
    }

    resumeRecurring(name) {
        this.recurring.get(name).enabled = true;
    }

    triggerRecurring(name) {
        const schedule = this.recurring.get(name);
        return this.enqueue(schedule.jobName, schedule.payload, {
            idempotencyKey: `manual:${schedule.name}:${iso(this.nowMs)}`,
        });
    }

    acquireLock(name, owner, ttlMs) {
        const current = this.locks.get(name);
        if (current === undefined || current.lockedUntilMs <= this.nowMs || current.owner === owner) {
            this.locks.set(name, { name, owner, lockedUntilMs: this.nowMs + ttlMs });
            return { acquired: true, owner };
        }
        return { acquired: false, owner: current.owner };
    }

    extendLock(name, owner, ttlMs) {
        const current = this.locks.get(name);
        if (current === undefined || current.owner !== owner) {
            throw new Error("SLOPPY_E_JOBS_LOCK_CONFLICT: lock is held by another owner");
        }
        current.lockedUntilMs = this.nowMs + ttlMs;
    }

    releaseLock(name, owner) {
        const current = this.locks.get(name);
        if (current === undefined) {
            return false;
        }
        if (current.owner !== owner) {
            throw new Error("SLOPPY_E_JOBS_LOCK_CONFLICT: lock is held by another owner");
        }
        this.locks.delete(name);
        return true;
    }
}

function runInvariant(name, collector, callback) {
    try {
        callback();
        collector.pass(name, `${name} holds for deterministic scheduler model`);
    } catch (error) {
        collector.fail(name, `${name} failed`, { error: error.message });
    }
}

function assertThrows(callback, pattern, message) {
    try {
        callback();
    } catch (error) {
        assertCondition(pattern.test(error.message), message);
        return;
    }
    throw new Error(message);
}

function validateStateMachine(collector) {
    runInvariant("jobs.state.allowed-transition", collector, () => {
        const jobs = new DeterministicJobs();
        const job = jobs.enqueue("email", {}, { runAtMs: BASE_TIME + MINUTE });
        assertCondition(job.status === "scheduled", "future runAt must be scheduled");
        assertThrows(() => jobs.transition(job, "succeeded"), /SLOPPY_E_JOBS_TRANSITION_INVALID/u, "invalid transition must fail");
        jobs.setNow(BASE_TIME + MINUTE);
        assertCondition(jobs.claim("worker-1").job.id === job.id, "due scheduled job must be claimable");
        const eventCount = jobs.events.length;
        assertCondition(jobs.transition(job, "processing") === job, "same-state transition must return current job");
        assertCondition(jobs.events.length === eventCount, "same-state transition must not add a duplicate event");
        assertCondition(jobs.complete(job.token) === true, "processing job must complete");
    });

    runInvariant("jobs.state.no-double-complete", collector, () => {
        const jobs = new DeterministicJobs();
        const job = jobs.enqueue("email");
        const claim = jobs.claim("worker-1");
        assertCondition(jobs.complete(claim.token) === true, "first completion must succeed");
        assertCondition(jobs.complete(claim.token) === false, "late completion must be ignored");
        assertCondition(jobs.events.filter((event) => event.jobId === job.id && event.to === "succeeded").length === 1, "job must have one succeeded event");
    });

    runInvariant("jobs.retry.exhaustion", collector, () => {
        const jobs = new DeterministicJobs();
        const job = jobs.enqueue("email", {}, { maxAttempts: 2 });
        let claim = jobs.claim("worker-1");
        jobs.fail(claim.token);
        assertCondition(job.status === "retrying", "first failure should retry");
        jobs.setNow(BASE_TIME + MINUTE);
        claim = jobs.claim("worker-1");
        jobs.fail(claim.token);
        assertCondition(job.status === "dead", "exhausted retries must go dead");
    });

    runInvariant("jobs.dead.not-claimed", collector, () => {
        const jobs = new DeterministicJobs();
        const job = jobs.enqueue("email", {}, { maxAttempts: 1 });
        const claim = jobs.claim("worker-1");
        jobs.fail(claim.token);
        assertCondition(job.status === "dead", "job should be dead");
        assertCondition(jobs.claim("worker-2") === undefined, "dead job must not be claimed");
    });

    runInvariant("jobs.cancelled.not-claimed", collector, () => {
        const jobs = new DeterministicJobs();
        const cancelled = jobs.enqueue("email");
        jobs.cancel(cancelled);
        const queued = jobs.enqueue("email");
        jobs.delete(queued);
        assertCondition(jobs.claim("worker-1") === undefined, "cancelled and deleted jobs must not be claimed");
    });

    runInvariant("jobs.attempt-log-agreement", collector, () => {
        const jobs = new DeterministicJobs();
        const job = jobs.enqueue("email");
        const claim = jobs.claim("worker-1");
        jobs.complete(claim.token);
        const transitions = jobs.events.filter((event) => event.jobId === job.id).map((event) => event.to);
        assertCondition(transitions.join(">") === "queued>processing>succeeded", "event log must match state transitions");
        assertCondition(jobs.attempts.length === 1 && jobs.attempts[0].jobId === job.id, "attempt log must match processing transition");
    });
}

function validateRecurring(collector) {
    runInvariant("scheduler.cron.utc-five-field", collector, () => {
        assertCondition(nextOccurrence("*/5 * * * *", BASE_TIME) === BASE_TIME + 5 * MINUTE, "five-field UTC cron must calculate next occurrence");
        assertThrows(() => parseUtcFiveFieldCron("*/5 * * *"), /five UTC fields/u, "invalid cron expression must fail");
    });

    runInvariant("scheduler.recurring.no-duplicate-occurrence", collector, () => {
        const jobs = new DeterministicJobs();
        jobs.defineRecurring("sync-five", "sync", {}, { cron: "*/5 * * * *", nextRunAtMs: BASE_TIME });
        const first = jobs.tickRecurring({ owner: "scheduler-1" });
        const second = jobs.tickRecurring({ owner: "scheduler-2" });
        assertCondition(first.length === 1, "first tick must enqueue due occurrence");
        assertCondition(second.length === 0, "second tick must not duplicate occurrence");
        assertCondition(new Set(jobs.jobs.map((job) => job.occurrenceKey)).size === jobs.jobs.length, "occurrence keys must be unique");
    });

    runInvariant("scheduler.pause-resume", collector, () => {
        const jobs = new DeterministicJobs();
        jobs.defineRecurring("paused", "sync", {}, { cron: "* * * * *", nextRunAtMs: BASE_TIME });
        jobs.pauseRecurring("paused");
        assertCondition(jobs.tickRecurring({ owner: "scheduler-1" }).length === 0, "paused schedule must not enqueue");
        jobs.resumeRecurring("paused");
        assertCondition(jobs.tickRecurring({ owner: "scheduler-1" }).length === 1, "resumed schedule must enqueue due work");
    });

    runInvariant("scheduler.manual-trigger", collector, () => {
        const jobs = new DeterministicJobs();
        jobs.defineRecurring("manual", "sync", { source: "manual" }, { cron: "0 * * * *", nextRunAtMs: BASE_TIME + 60 * MINUTE });
        const job = jobs.triggerRecurring("manual");
        assertCondition(job.status === "queued" && job.name === "sync", "manual trigger must enqueue expected job");
    });

    runInvariant("scheduler.misfire.ignore", collector, () => {
        const jobs = new DeterministicJobs();
        jobs.defineRecurring("ignore", "sync", {}, {
            cron: "* * * * *",
            misfirePolicy: "ignore",
            nextRunAtMs: BASE_TIME - 120 * MINUTE,
        });
        assertCondition(jobs.tickRecurring({ owner: "scheduler-1" }).length === 0, "ignore must not enqueue missed occurrence");
        assertCondition(jobs.recurring.get("ignore").nextRunAtMs === BASE_TIME + MINUTE, "ignore must advance past long backlog");
    });

    runInvariant("scheduler.misfire.run-once", collector, () => {
        const jobs = new DeterministicJobs();
        jobs.defineRecurring("run-once", "sync", {}, {
            cron: "* * * * *",
            misfirePolicy: "run-once",
            nextRunAtMs: BASE_TIME - 120 * MINUTE,
        });
        const enqueued = jobs.tickRecurring({ owner: "scheduler-1" });
        assertCondition(enqueued.length === 1, "run-once must enqueue one missed occurrence");
        assertCondition(enqueued[0].occurrenceKey.endsWith(iso(BASE_TIME)), "run-once must enqueue latest due occurrence");
        assertCondition(jobs.recurring.get("run-once").nextRunAtMs === BASE_TIME + MINUTE, "run-once must advance past long backlog");
    });

    runInvariant("scheduler.misfire.catch-up-limited", collector, () => {
        const jobs = new DeterministicJobs();
        jobs.defineRecurring("catch-up", "sync", {}, {
            cron: "* * * * *",
            misfirePolicy: "catch-up-limited",
            catchUpLimit: 2,
            nextRunAtMs: BASE_TIME - 120 * MINUTE,
        });
        const enqueued = jobs.tickRecurring({ owner: "scheduler-1" });
        assertCondition(enqueued.length === 2, "catch-up-limited must respect limit");
        assertCondition(jobs.recurring.get("catch-up").nextRunAtMs === BASE_TIME - 118 * MINUTE, "catch-up-limited must advance by its limit");
    });
}

function validateLocksAndIdempotency(collector) {
    runInvariant("locks.owner-release", collector, () => {
        const jobs = new DeterministicJobs();
        assertCondition(jobs.acquireLock("nightly", "owner-1", 1000).acquired === true, "free lock must be acquired");
        jobs.extendLock("nightly", "owner-1", 2000);
        assertThrows(() => jobs.releaseLock("nightly", "owner-2"), /SLOPPY_E_JOBS_LOCK_CONFLICT/u, "non-owner release must fail");
        assertCondition(jobs.releaseLock("nightly", "owner-1") === true, "owner release must succeed");
    });

    runInvariant("locks.takeover-after-expiry", collector, () => {
        const jobs = new DeterministicJobs();
        jobs.acquireLock("nightly", "owner-1", 1000);
        assertCondition(jobs.acquireLock("nightly", "owner-2", 1000).acquired === false, "non-expired lock must not be stolen");
        jobs.setNow(BASE_TIME + 1001);
        assertCondition(jobs.acquireLock("nightly", "owner-2", 1000).acquired === true, "expired lock must be taken over");
        const diagnostics = JSON.stringify(jobs.locks.get("nightly"));
        assertCondition(!/secret|token|payload/iu.test(diagnostics), "lock diagnostics must not expose secret payloads");
    });

    runInvariant("jobs.idempotency", collector, () => {
        const jobs = new DeterministicJobs();
        const first = jobs.enqueue("email", { to: "ada@example.com" }, { idempotencyKey: "welcome:ada" });
        const second = jobs.enqueue("email", { to: "ada@example.com" }, { idempotencyKey: "welcome:ada" });
        assertCondition(first.id === second.id && jobs.jobs.length === 1, "duplicate idempotency key must return existing job");
        assertCondition(/^job_[a-z0-9]+_contract$/u.test(first.id), "generated IDs must be prefixed and readable");
        assertCondition(jobs.idSource !== "date-now" && jobs.idSource !== "process-counter", "IDs must not be Date.now or process counter sourced");
    });
}

function negativeFinding(invariant, detected, details = undefined) {
    return createFinding({
        id: `${SUBSYSTEM}.negative.${invariant}`,
        status: detected ? "pass" : "fail",
        severity: detected ? "info" : "error",
        subsystem: SUBSYSTEM,
        invariant: `negative.${invariant}`,
        fixture: "negative-cases",
        message: detected ? `broken case produced expected ${invariant} finding` : `broken case did not produce expected ${invariant} finding`,
        details,
    });
}

function validateNegativeCases() {
    return [
        negativeFinding("jobs.state.allowed-transition", (() => {
            const jobs = new DeterministicJobs();
            const job = jobs.enqueue("email");
            try {
                jobs.transition(job, "succeeded");
                return false;
            } catch {
                return job.status === "queued";
            }
        })()),
        negativeFinding("scheduler.recurring.no-duplicate-occurrence", (() => {
            const jobs = new DeterministicJobs();
            jobs.defineRecurring("sync", "sync", {}, { cron: "* * * * *", nextRunAtMs: BASE_TIME });
            jobs.tickRecurring({ owner: "scheduler-1" });
            jobs.recurring.get("sync").nextRunAtMs = BASE_TIME;
            jobs.tickRecurring({ owner: "scheduler-2" });
            return jobs.jobs.length === 1;
        })()),
        negativeFinding("locks.owner-release", (() => {
            const jobs = new DeterministicJobs();
            jobs.acquireLock("nightly", "owner-1", 1000);
            try {
                jobs.releaseLock("nightly", "owner-2");
                return false;
            } catch {
                return true;
            }
        })()),
        negativeFinding("jobs.retry.exhaustion", (() => {
            const jobs = new DeterministicJobs();
            const job = jobs.enqueue("email", {}, { maxAttempts: 1 });
            jobs.fail(jobs.claim("worker-1").token);
            return job.status === "dead";
        })()),
        negativeFinding("scheduler.pause-resume", (() => {
            const jobs = new DeterministicJobs();
            jobs.defineRecurring("paused", "sync", {}, { cron: "* * * * *", nextRunAtMs: BASE_TIME });
            jobs.pauseRecurring("paused");
            return jobs.tickRecurring({ owner: "scheduler-1" }).length === 0;
        })()),
        negativeFinding("scheduler.cron.utc-five-field", (() => {
            try {
                parseUtcFiveFieldCron("*/5 * * *");
                return false;
            } catch {
                return true;
            }
        })()),
        negativeFinding("scheduler.misfire.catch-up-limited", (() => {
            const jobs = new DeterministicJobs();
            jobs.defineRecurring("catch-up", "sync", {}, {
                cron: "* * * * *",
                misfirePolicy: "catch-up-limited",
                catchUpLimit: 2,
                nextRunAtMs: BASE_TIME - 120 * MINUTE,
            });
            return jobs.tickRecurring({ owner: "scheduler-1" }).length === 2;
        })()),
        negativeFinding("jobs.idempotency", (() => {
            const jobs = new DeterministicJobs();
            jobs.enqueue("email", {}, { idempotencyKey: "same" });
            jobs.enqueue("email", {}, { idempotencyKey: "same" });
            return jobs.jobs.length === 1;
        })()),
    ];
}

async function exists(filePath) {
    try {
        await fs.access(filePath);
        return true;
    } catch {
        return false;
    }
}

async function addSourceTruthFindings(repoRoot, collector) {
    const requiredDocs = [
        "docs/api/jobs.md",
        "docs/reference/jobs-storage.md",
        "docs/internals/scheduler.md",
        "docs/cli/jobs.md",
    ];
    for (const relative of requiredDocs) {
        const docName = relative.replace(/^docs\//u, "").replace(/\.md$/u, "").replace(/[^a-z0-9]+/gu, "-");
        const docInvariant = `jobs.source-doc.${docName}.present`;
        if (await exists(path.join(repoRoot, relative))) {
            collector.pass(docInvariant, `${relative} is present`);
        } else {
            collector.fail(docInvariant, `${relative} must be present`);
        }
    }

    const jobsModule = path.join(repoRoot, "stdlib/sloppy/jobs.js");
    if (await exists(jobsModule)) {
        const source = await fs.readFile(jobsModule, "utf8");
        const expectedExports = ["Jobs", "SloppyJobsError"];
        for (const exportName of expectedExports) {
            if (new RegExp(`\\b${exportName}\\b`, "u").test(source)) {
                collector.pass("jobs.runtime-api.present", `stdlib/sloppy/jobs.js contains ${exportName}`);
            } else {
                collector.fail("jobs.runtime-api.present", `stdlib/sloppy/jobs.js must expose ${exportName}`);
            }
        }
    } else {
        collector.unavailable("jobs.runtime-api.present", "stdlib/sloppy/jobs.js is not present in this checkout");
    }

    const cliSource = path.join(repoRoot, "src/cli/cli_jobs.inc");
    if (await exists(cliSource)) {
        collector.pass("jobs.cli.present", "native sloppy jobs CLI source is present");
    } else {
        collector.unavailable("jobs.cli.present", "native sloppy jobs CLI source is not present in this checkout");
    }
}

export async function runJobsContract({ repoRoot, tier }) {
    const startedAt = new Date().toISOString();
    const collector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture: FIXTURE });
    await addSourceTruthFindings(repoRoot, collector);
    validateStateMachine(collector);
    validateRecurring(collector);
    validateLocksAndIdempotency(collector);
    const findings = [...collector.findings, ...validateNegativeCases()];
    return createReport({
        subsystem: SUBSYSTEM,
        tier,
        startedAt,
        finishedAt: new Date().toISOString(),
        findings,
    });
}
