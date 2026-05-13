import assert from "node:assert/strict";

import { Jobs, Schema, SloppyJobsError } from "../../stdlib/sloppy/index.js";

class FakeJobsDb {
    constructor(provider = "sqlite") {
        this.provider = provider;
        this.schema = [];
        this.jobs = [];
        this.events = [];
        this.attempts = [];
        this.recurring = [];
        this.workers = [];
        this.locks = [];
        this.queries = [];
    }

    __debug() {
        return { kind: `${this.provider}-connection` };
    }

    async transaction(callback) {
        return await callback(this);
    }

    async exec(sql, params = [], options = undefined) {
        this.queries.push({ kind: "exec", sql, params, options });
        return await this.query(sql, params, options);
    }

    async queryOne(sql, params = [], options = undefined) {
        return (await this.query(sql, params, options))[0] ?? null;
    }

    async query(sql, params = [], options = undefined) {
        this.queries.push({ kind: "query", sql, params, options });
        const text = sql.toLowerCase();
        if (text.includes("strftime(") || text.includes("clock_timestamp") || text.includes("sysutcdatetime")) {
            return [{ now: new Date().toISOString() }];
        }
        if (text.startsWith("create ") || text.startsWith("if ")) {
            return [];
        }
        if (text.includes("from sloppy_job_schema")) {
            return this.schema;
        }
        if (text.startsWith("insert into sloppy_job_schema")) {
            this.schema.push({ version: params[0], updated_at: params[1] });
            return [];
        }
        if (text.startsWith("insert into sloppy_jobs") || text.startsWith("insert or ignore into sloppy_jobs")) {
            if (this.jobs.some((job) => job.id === params[0])) {
                const error = new Error("unique constraint failed: sloppy_jobs.id");
                error.code = "SQLITE_CONSTRAINT";
                throw error;
            }
            if (
                (text.startsWith("insert or ignore") || text.includes("on conflict")) &&
                params[20] !== null &&
                params[20] !== undefined &&
                this.jobs.some((job) => job.idempotency_key === params[20])
            ) {
                return [];
            }
            this.jobs.push({
                id: params[0],
                name: params[1],
                queue: params[2],
                status: params[3],
                payload_json: params[4],
                payload_schema: params[5],
                priority: params[6],
                run_at: params[7],
                created_at: params[8],
                updated_at: params[9],
                locked_by: params[10],
                locked_until: params[11],
                attempt_count: params[12],
                max_attempts: params[13],
                retry_policy_json: params[14],
                next_retry_at: params[15],
                last_error_code: params[16],
                last_error_message: params[17],
                diagnostic_id: params[18],
                correlation_id: params[19],
                idempotency_key: params[20],
                timeout_ms: params[21],
                metadata_json: params[22],
            });
            return [];
        }
        if (text.startsWith("insert into sloppy_job_events")) {
            if (this.events.some((event) => event.id === params[0])) {
                const error = new Error("unique constraint failed: sloppy_job_events.id");
                error.code = "SQLITE_CONSTRAINT";
                throw error;
            }
            this.events.push({
                id: params[0],
                job_id: params[1],
                event_type: params[2],
                from_status: params[3],
                to_status: params[4],
                created_at: params[5],
                worker_id: params[6],
                message: params[7],
                data_json: params[8],
            });
            return [];
        }
        if (text.startsWith("insert into sloppy_job_attempts")) {
            if (this.attempts.some((attempt) => attempt.id === params[0])) {
                const error = new Error("unique constraint failed: sloppy_job_attempts.id");
                error.code = "SQLITE_CONSTRAINT";
                throw error;
            }
            this.attempts.push({
                id: params[0],
                job_id: params[1],
                worker_id: params[2],
                attempt_number: params[3],
                started_at: params[4],
                finished_at: params[5],
                status: params[6],
                duration_ms: params[7],
                error_code: params[8],
                error_message: params[9],
                diagnostic_id: params[10],
            });
            return [];
        }
        if (text.startsWith("insert into sloppy_recurring_jobs")) {
            this.recurring.push({
                id: params[0],
                name: params[1],
                job_name: params[2],
                queue: params[3],
                cron: params[4],
                timezone: params[5],
                payload_json: params[6],
                enabled: params[7],
                misfire_policy: params[8],
                last_run_at: params[9],
                next_run_at: params[10],
                created_at: params[11],
                updated_at: params[12],
                metadata_json: params[13],
            });
            return [];
        }
        if (text.startsWith("insert into sloppy_job_workers")) {
            this.workers.push({
                id: params[0],
                worker_name: params[1],
                host: params[2],
                pid: params[3],
                queues: params[4],
                started_at: params[5],
                last_heartbeat_at: params[6],
                status: params[7],
            });
            return [];
        }
        if (text.startsWith("insert into sloppy_job_locks") || text.startsWith("insert or ignore into sloppy_job_locks")) {
            if (
                (text.startsWith("insert or ignore") || text.includes("on conflict")) &&
                this.locks.some((lock) => lock.name === params[0])
            ) {
                return [];
            }
            this.locks.push({
                name: params[0],
                owner: params[1],
                locked_until: params[2],
                updated_at: params[3],
            });
            return [];
        }
        if (text.includes("from sloppy_jobs where idempotency_key")) {
            return this.jobs.filter((job) => job.idempotency_key === params[0]);
        }
        if (text.includes("from sloppy_jobs where id =")) {
            return this.jobs.filter((job) => job.id === params[0]);
        }
        if (text.includes("from sloppy_jobs group by status")) {
            const counts = new Map();
            for (const job of this.jobs) {
                counts.set(job.status, (counts.get(job.status) ?? 0) + 1);
            }
            return Array.from(counts, ([status, count]) => ({ status, count }));
        }
        if (text.includes("from sloppy_jobs")) {
            if (text.includes("status = 'queued'") && text.includes("queue in")) {
                const limit = params.at(-1);
                const now = params.at(-2);
                const queues = params.slice(0, -2);
                return this.jobs
                    .filter((job) => job.status === "queued" && queues.includes(job.queue) && job.run_at <= now)
                    .sort((a, b) => b.priority - a.priority || a.run_at.localeCompare(b.run_at))
                    .slice(0, limit);
            }
            if (text.includes("status = 'succeeded'") && text.includes("status = 'dead'")) {
                const [succeededBefore, deadBefore, cancelledBefore, limit] = params;
                return this.jobs
                    .filter((job) =>
                        (job.status === "succeeded" && job.updated_at <= succeededBefore) ||
                        (job.status === "dead" && job.updated_at <= deadBefore) ||
                        (job.status === "cancelled" && job.updated_at <= cancelledBefore))
                    .sort((a, b) => a.updated_at.localeCompare(b.updated_at) || a.id.localeCompare(b.id))
                    .slice(0, limit);
            }
            return [...this.jobs];
        }
        if (text.startsWith("update sloppy_jobs set status = 'queued', locked_by")) {
            if (params.length === 3 && text.includes("where id =")) {
                const [runAt, updatedAt, id] = params;
                const job = this.jobs.find((current) => current.id === id);
                if (job) {
                    Object.assign(job, {
                        status: "queued",
                        locked_by: null,
                        locked_until: null,
                        run_at: runAt,
                        updated_at: updatedAt,
                    });
                }
                return [];
            }
            const now = params[0];
            const cutoff = params[1];
            for (const job of this.jobs) {
                if (job.status === "processing" && job.locked_until <= cutoff) {
                    Object.assign(job, { status: "queued", locked_by: null, locked_until: null, updated_at: now });
                }
            }
            return [];
        }
        if (text.startsWith("update sloppy_jobs set status = 'queued', updated_at")) {
            if (params.length === 3) {
                const [runAt, updatedAt, id] = params;
                const job = this.jobs.find((current) => current.id === id);
                if (job) {
                    Object.assign(job, {
                        status: "queued",
                        locked_by: null,
                        locked_until: null,
                        run_at: runAt,
                        updated_at: updatedAt,
                    });
                }
                return [];
            }
            const now = params[0];
            const cutoff = params[1];
            for (const job of this.jobs) {
                if ((job.status === "scheduled" || job.status === "retrying") && job.run_at <= cutoff) {
                    Object.assign(job, { status: "queued", updated_at: now });
                }
            }
            return [];
        }
        if (text.startsWith("update sloppy_jobs set status = 'processing'")) {
            const [worker, lockedUntil, attempt, updatedAt, id] = params;
            const job = this.jobs.find((current) => current.id === id && current.status === "queued");
            if (job) {
                Object.assign(job, {
                    status: "processing",
                    locked_by: worker,
                    locked_until: lockedUntil,
                    attempt_count: attempt,
                    updated_at: updatedAt,
                });
            }
            return [];
        }
        if (text.startsWith("update sloppy_jobs set status = ?, locked_by = null")) {
            const [status, updatedAt, nextRetryAt, runAt, code, message, diagnosticId, id, worker] = params.length >= 8
                ? params
                : [params[0], params[1], null, null, null, null, null, params[2]];
            const job = this.jobs.find((current) => current.id === id);
            if (job && (worker === undefined || job.locked_by === worker)) {
                Object.assign(job, {
                    status,
                    locked_by: null,
                    locked_until: null,
                    updated_at: updatedAt,
                    next_retry_at: nextRetryAt,
                    run_at: runAt ?? job.run_at,
                    last_error_code: code,
                    last_error_message: message,
                    diagnostic_id: diagnosticId,
                });
            }
            return [];
        }
        if (text.startsWith("update sloppy_jobs set status = ?")) {
            const [status, updatedAt, id] = params;
            const job = this.jobs.find((current) => current.id === id);
            if (job) {
                Object.assign(job, { status, updated_at: updatedAt });
            }
            return [];
        }
        if (text.startsWith("update sloppy_job_attempts")) {
            const jobId = params.at(-2);
            const attemptNumber = params.at(-1);
            const attempt = this.attempts.find((current) => current.job_id === jobId && current.attempt_number === attemptNumber);
            if (attempt) {
                attempt.finished_at = params[0];
                attempt.status = params[1];
                attempt.error_code = params.length > 5 ? params[2] : null;
                attempt.error_message = params.length > 5 ? params[3] : null;
            }
            return [];
        }
        if (text.includes("from sloppy_job_attempts")) {
            return this.attempts.filter((attempt) => attempt.job_id === params[0]);
        }
        if (text.includes("from sloppy_job_events")) {
            return this.events.filter((event) => event.job_id === params[0]);
        }
        if (text.includes("from sloppy_recurring_jobs where name")) {
            return this.recurring.filter((job) => job.name === params[0]);
        }
        if (text.includes("from sloppy_recurring_jobs")) {
            if (text.includes("enabled = 1")) {
                const [now, limit] = params;
                return this.recurring
                    .filter((job) => Number(job.enabled) === 1 && job.next_run_at <= now)
                    .sort((a, b) => a.next_run_at.localeCompare(b.next_run_at) || a.name.localeCompare(b.name))
                    .slice(0, limit);
            }
            return [...this.recurring];
        }
        if (text.startsWith("update sloppy_recurring_jobs set enabled")) {
            const [enabled, updatedAt, name] = params;
            const recurring = this.recurring.find((job) => job.name === name);
            Object.assign(recurring, { enabled, updated_at: updatedAt });
            return [];
        }
        if (text.startsWith("update sloppy_recurring_jobs set last_run_at")) {
            const [lastRunAt, nextRunAt, updatedAt, name] = params;
            const recurring = this.recurring.find((job) => job.name === name);
            Object.assign(recurring, { last_run_at: lastRunAt, next_run_at: nextRunAt, updated_at: updatedAt });
            return [];
        }
        if (text.includes("from sloppy_job_workers")) {
            return [...this.workers];
        }
        if (text.startsWith("update sloppy_job_workers")) {
            if (text.includes("worker_name")) {
                const [workerName, host, pid, queues, startedAt, heartbeat, status, id] = params;
                const worker = this.workers.find((current) => current.id === id);
                if (worker) {
                    Object.assign(worker, {
                        worker_name: workerName,
                        host,
                        pid,
                        queues,
                        started_at: startedAt,
                        last_heartbeat_at: heartbeat,
                        status,
                    });
                }
                return [];
            }
            const [heartbeat, status, id] = params;
            const worker = this.workers.find((current) => current.id === id);
            if (worker) {
                Object.assign(worker, { last_heartbeat_at: heartbeat, status });
            }
            return [];
        }
        if (text.startsWith("delete from sloppy_job_locks") || (text.startsWith("if exists") && text.includes("delete from sloppy_job_locks"))) {
            const name = params.length >= 4 ? params[2] : params[0];
            const owner = params.length >= 4 ? params[3] : params[1];
            this.locks = this.locks.filter((lock) => !(lock.name === name && lock.owner === owner));
            return [];
        }
        if (text.startsWith("update sloppy_job_locks") || (text.startsWith("if exists") && text.includes("update sloppy_job_locks"))) {
            if (text.startsWith("if exists")) {
                const lock = this.locks.find((current) => current.name === params[4] && current.owner === params[5]);
                if (lock) {
                    Object.assign(lock, { locked_until: params[2], updated_at: params[3] });
                }
                return [];
            }
            const [ownerOrUntil, lockedUntilOrUpdated, updatedOrName, maybeName] = params;
            const name = maybeName ?? updatedOrName;
            const lock = this.locks.find((current) => current.name === name);
            if (lock) {
                if (maybeName === undefined) {
                    Object.assign(lock, { locked_until: ownerOrUntil, updated_at: lockedUntilOrUpdated });
                } else if (
                    text.includes("or locked_until <=") &&
                    (lock.owner === params[4] || lock.locked_until <= params[5])
                ) {
                    Object.assign(lock, { owner: ownerOrUntil, locked_until: lockedUntilOrUpdated, updated_at: updatedOrName });
                } else if (text.includes("or locked_until <=")) {
                    return [];
                } else {
                    Object.assign(lock, { owner: ownerOrUntil, locked_until: lockedUntilOrUpdated, updated_at: updatedOrName });
                }
            }
            return [];
        }
        if (text.includes("from sloppy_job_locks where name")) {
            return this.locks.filter((lock) => lock.name === params[0]);
        }
        if (text.includes("from sloppy_job_locks")) {
            return [...this.locks];
        }
        if (text.startsWith("delete from sloppy_job_locks")) {
            this.locks = this.locks.filter((lock) => !(lock.name === params[0] && lock.owner === params[1]));
            return [];
        }
        throw new Error(`unhandled fake SQL: ${sql}`);
    }
}

function assertJobsError(error, code) {
    assert(error instanceof SloppyJobsError);
    assert.equal(error.code, code);
    return true;
}

const db = new FakeJobsDb("sqlite");
const storage = Jobs.storage.sqlite(db);
await storage.init();
assert.equal(db.schema[0].version, Jobs.schemaVersion);
assert(db.queries.some((entry) => entry.sql.includes("sloppy_job_locks")));

const runtime = Jobs.create({ storage });
const EmailPayload = Schema.object({
    to: Schema.string().email(),
    token: Schema.string().min(1),
});
const delivered = [];
runtime.define("send-email", {
    input: EmailPayload,
    queue: "emails",
    retries: { maxAttempts: 2, backoff: "fixed", initialDelayMs: 1, maxDelayMs: 1 },
    payloadRedactionKeys: ["token"],
}, async (_ctx, input) => {
    delivered.push(input.to);
});
assert.throws(
    () => runtime.define("send-email", {}, async () => {}),
    (error) => assertJobsError(error, "SLOPPY_E_JOBS_DUPLICATE_JOB"),
);

const fixedNow = Date.now;
Date.now = () => 1800000000000;
try {
    const randomIdDb = new FakeJobsDb("sqlite");
    const randomIdStorage = Jobs.storage.sqlite(randomIdDb);
    await randomIdStorage.init();
    const randomIdRuntime = Jobs.create({ storage: randomIdStorage });
    randomIdRuntime.define("random-id", {}, async () => {});
    const randomJobs = await Promise.all(Array.from({ length: 512 }, async (_, index) =>
        randomIdRuntime.enqueue("random-id", { index }, { idempotencyKey: `random-id:${index}` })));
    assert.equal(new Set(randomJobs.map((job) => job.id)).size, randomJobs.length);
    assert.equal(new Set(randomIdDb.events.map((event) => event.id)).size, randomIdDb.events.length);

    const jobsModuleUrl = new URL("../../stdlib/sloppy/jobs.js", import.meta.url).href;
    const [{ Jobs: FreshJobsA }, { Jobs: FreshJobsB }] = await Promise.all([
        import(`${jobsModuleUrl}?fresh=random-a`),
        import(`${jobsModuleUrl}?fresh=random-b`),
    ]);
    const freshDbA = new FakeJobsDb("sqlite");
    const freshDbB = new FakeJobsDb("sqlite");
    const freshRuntimeA = FreshJobsA.create({ storage: FreshJobsA.storage.sqlite(freshDbA) });
    const freshRuntimeB = FreshJobsB.create({ storage: FreshJobsB.storage.sqlite(freshDbB) });
    await freshRuntimeA.storage.init();
    await freshRuntimeB.storage.init();
    freshRuntimeA.define("fresh-id", {}, async () => {});
    freshRuntimeB.define("fresh-id", {}, async () => {});
    const [freshA, freshB] = await Promise.all([
        freshRuntimeA.enqueue("fresh-id", {}),
        freshRuntimeB.enqueue("fresh-id", {}),
    ]);
    assert.notEqual(freshA.id, freshB.id);
} finally {
    Date.now = fixedNow;
}

await assert.rejects(
    runtime.enqueue("send-email", { to: "bad", token: "secret" }),
    (error) => assertJobsError(error, "SLOPPY_E_JOBS_INVALID_PAYLOAD"),
);

const first = await runtime.enqueue("send-email", { to: "a@example.com", token: "secret" }, {
    queue: "emails",
    priority: 10,
    idempotencyKey: "welcome:1",
});
const duplicate = await runtime.enqueue("send-email", { to: "a@example.com", token: "secret" }, {
    idempotencyKey: "welcome:1",
});
assert.equal(duplicate.id, first.id);
assert.equal(first.payloadPreview.token, "<redacted>");

const worker = runtime.createWorker({ id: "worker-1", queues: ["emails"], concurrency: 2 });
await storage.registerWorker(worker);
await storage.registerWorker(worker);
assert.equal((await storage.listWorkers()).length, 1);
assert.equal(await worker.runOnce(), 1);
assert.deepEqual(delivered, ["a@example.com"]);
assert.equal((await storage.getJob(first.id)).status, "succeeded");
assert.equal((await storage.attempts(first.id)).length, 1);
assert((await storage.events(first.id)).some((event) => event.event_type === "claimed"));
await assert.rejects(
    storage.transition(first.id, "queued"),
    (error) => assertJobsError(error, "SLOPPY_E_JOBS_TRANSITION_INVALID"),
);

runtime.define("fails-once", { retries: { maxAttempts: 1, backoff: "fixed", initialDelayMs: 1, maxDelayMs: 1 } }, async () => {
    throw Object.assign(new Error("planned"), { code: "SLOPPY_E_TEST" });
});
const failing = await runtime.enqueue("fails-once", {}, { queue: "emails" });
assert.equal(await worker.runOnce(), 1);
assert.equal((await storage.getJob(failing.id)).status, "dead");

const admin = runtime.admin();
assert((await admin.overview()).jobs.dead >= 1);
await storage.manualRetry(failing.id);
assert.equal((await storage.getJob(failing.id)).status, "queued");
await admin.cancel(failing.id);
assert.equal((await storage.getJob(failing.id)).status, "cancelled");

const recurring = await runtime.recurring("sync-users-every-minute", "send-email", {
    to: "b@example.com",
    token: "another",
}, {
    cron: "* * * * *",
    queue: "emails",
    timezone: "UTC",
});
assert.equal(recurring.enabled, true);
await admin.pauseRecurring("sync-users-every-minute");
assert.equal((await storage.getRecurring("sync-users-every-minute")).enabled, false);
await admin.resumeRecurring("sync-users-every-minute");
assert.equal((await storage.getRecurring("sync-users-every-minute")).enabled, true);
db.recurring.find((job) => job.name === "sync-users-every-minute").next_run_at = new Date(Date.now() - 60000).toISOString();
const recurringEnqueues = await runtime.tickRecurring({ owner: "recurring-owner", ttlMs: 1000, limit: 10 });
assert.equal(recurringEnqueues.length, 1);
assert.equal(recurringEnqueues[0].metadata.recurring, "sync-users-every-minute");
assert.match(Jobs.nextCronRun("*/5 * * * *", new Date("2026-05-12T12:01:00Z")), /^2026-05-12T12:05:00/);

await runtime.recurring("catch-up-email", "send-email", {
    to: "c@example.com",
    token: "catch-up",
}, {
    cron: "* * * * *",
    queue: "emails",
    timezone: "UTC",
    misfirePolicy: "catch-up-limited",
    catchUpLimit: 2,
});
db.recurring.find((job) => job.name === "catch-up-email").next_run_at = new Date(Date.now() - 120000).toISOString();
const catchUpEnqueues = await runtime.tickRecurring({ owner: "recurring-owner", ttlMs: 1000, limit: 10 });
assert.equal(catchUpEnqueues.length, 2);
assert.notEqual(catchUpEnqueues[0].idempotencyKey, catchUpEnqueues[1].idempotencyKey);

const ignoreMisfireDb = new FakeJobsDb("sqlite");
const ignoreMisfireRuntime = Jobs.create({ storage: Jobs.storage.sqlite(ignoreMisfireDb) });
await ignoreMisfireRuntime.storage.init();
ignoreMisfireRuntime.define("send-email", {}, async () => {});
await ignoreMisfireRuntime.recurring("ignore-misfire-email", "send-email", {
    to: "ignore@example.com",
}, {
    cron: "* * * * *",
    queue: "emails",
    timezone: "UTC",
    misfirePolicy: "ignore",
});
ignoreMisfireDb.recurring.find((job) => job.name === "ignore-misfire-email").next_run_at = new Date(Date.now() - 60000).toISOString();
assert.equal((await ignoreMisfireRuntime.tickRecurring({ owner: "ignore-owner", ttlMs: 1000, limit: 10 })).length, 0);
const recurringRaceDb = new FakeJobsDb("sqlite");
const recurringRaceStorage = Jobs.storage.sqlite(recurringRaceDb);
await recurringRaceStorage.init();
const recurringRaceRuntime = Jobs.create({ storage: recurringRaceStorage });
const recurringRacePeer = Jobs.create({ storage: recurringRaceStorage });
recurringRaceRuntime.define("race-recurring", {}, async () => {});
recurringRacePeer.define("race-recurring", {}, async () => {});
await recurringRaceRuntime.recurring("race-recurring-every-minute", "race-recurring", {}, {
    cron: "* * * * *",
    timezone: "UTC",
    misfirePolicy: "run-once",
});
recurringRaceDb.recurring[0].next_run_at = "2026-05-12T12:00:00.000Z";
const concurrentTicks = await Promise.all([
    recurringRaceRuntime.tickRecurring({ owner: "concurrent-recurring-1", ttlMs: 1000, limit: 10 }),
    recurringRacePeer.tickRecurring({ owner: "concurrent-recurring-2", ttlMs: 1000, limit: 10 }),
]);
assert.equal(concurrentTicks.flat().filter((job) => job.metadata.recurring === "race-recurring-every-minute").length, 1);
await assert.rejects(
    runtime.recurring("bad-cron", "send-email", {}, { cron: "not valid", timezone: "UTC" }),
    (error) => assertJobsError(error, "SLOPPY_E_JOBS_RECURRING_INVALID_CRON"),
);

const locks = runtime.locks("owner-1");
assert.equal(await locks.acquire("nightly-report", { ttlMs: 1000 }), true);
assert.equal(await Jobs.create({ storage }).locks("owner-2").acquire("nightly-report", { ttlMs: 1000 }), false);
await assert.rejects(
    Jobs.create({ storage }).locks("owner-2").release("nightly-report"),
    (error) => assertJobsError(error, "SLOPPY_E_JOBS_LOCK_CONFLICT"),
);
assert.equal(await locks.release("nightly-report"), true);
const lockRaceRuntime = Jobs.create({ storage });
const lockRace = await Promise.all([
    runtime.locks("race-owner-1").acquire("single-owner", { ttlMs: 1000 }),
    lockRaceRuntime.locks("race-owner-2").acquire("single-owner", { ttlMs: 1000 }),
]);
assert.equal(lockRace.filter(Boolean).length, 1);
db.locks.find((lock) => lock.name === "single-owner").locked_until = new Date(Date.now() - 1000).toISOString();
assert.equal(await lockRaceRuntime.locks("race-owner-2").acquire("single-owner", { ttlMs: 1000 }), true);
await assert.rejects(
    runtime.locks("race-owner-1").release("single-owner"),
    (error) => assertJobsError(error, "SLOPPY_E_JOBS_LOCK_CONFLICT"),
);

let timeoutSignalAborted = false;
runtime.define("times-out", { timeoutMs: 1, retries: { maxAttempts: 1 } }, async (ctx) => {
    await new Promise((resolve) => {
        ctx.signal.addEventListener("abort", () => {
            timeoutSignalAborted = true;
            setTimeout(resolve, 5);
        });
    });
    return "late";
});
const timeout = await runtime.enqueue("times-out", {}, { queue: "emails", priority: 100 });
assert((await worker.runOnce()) >= 1);
assert.equal((await storage.getJob(timeout.id)).lastErrorCode, "SLOPPY_E_JOBS_TIMEOUT");
assert.equal(timeoutSignalAborted, true);
await new Promise((resolve) => setTimeout(resolve, 10));
assert.equal((await storage.events(timeout.id)).filter((event) => event.event_type === "succeeded").length, 0);

const cleanup = await admin.cleanup({ keepSucceededMs: 0, keepDeadMs: 0, keepCancelledMs: 0, batchSize: 20 });
assert(cleanup.deleted >= 1);

await worker.stop();
assert.equal((await storage.listWorkers())[0].status, "stopped");

const delayedDb = new FakeJobsDb("sqlite");
const delayedStorage = Jobs.storage.sqlite(delayedDb);
await delayedStorage.init();
const delayedRuntime = Jobs.create({ storage: delayedStorage });
delayedRuntime.define("later", {}, async () => {});
await delayedRuntime.enqueueDelayed("later", {}, 60000);
assert.equal(await delayedRuntime.createWorker({ id: "delayed-worker" }).runOnce(), 0);
assert.equal((await delayedStorage.listWorkers()).length, 1);

const terminalRaceDb = new FakeJobsDb("sqlite");
const terminalRaceStorage = Jobs.storage.sqlite(terminalRaceDb);
await terminalRaceStorage.init();
const terminalRaceRuntime = Jobs.create({ storage: terminalRaceStorage });
terminalRaceRuntime.define("terminal-race-fail", {}, async () => {
    throw new Error("handler failed after lease moved");
});
await terminalRaceRuntime.enqueue("terminal-race-fail", {}, { queue: "terminal-race" });
const terminalRaceWorker = terminalRaceRuntime.createWorker({ id: "terminal-race-worker", queues: ["terminal-race"] });
const [terminalRaceFailJob] = await terminalRaceStorage.claim(terminalRaceWorker, { leaseMs: 1 });
terminalRaceDb.jobs.find((job) => job.id === terminalRaceFailJob.id).status = "cancelled";
await terminalRaceWorker._executeInner(terminalRaceFailJob);

terminalRaceRuntime.define("terminal-race-complete", {}, async () => "late");
await terminalRaceRuntime.enqueue("terminal-race-complete", {}, { queue: "terminal-race-complete" });
const terminalRaceCompleteWorker = terminalRaceRuntime.createWorker({ id: "terminal-race-complete-worker", queues: ["terminal-race-complete"] });
const [terminalRaceCompleteJob] = await terminalRaceStorage.claim(terminalRaceCompleteWorker, { leaseMs: 1 });
terminalRaceDb.jobs.find((job) => job.id === terminalRaceCompleteJob.id).locked_by = "other-worker";
await terminalRaceCompleteWorker._executeInner(terminalRaceCompleteJob);

const leaseDb = new FakeJobsDb("sqlite");
const leaseStorage = Jobs.storage.sqlite(leaseDb);
await leaseStorage.init();
const leaseRuntime = Jobs.create({ storage: leaseStorage });
leaseRuntime.define("lease", {}, async () => {});
await leaseRuntime.enqueue("lease", {}, { queue: "leases" });
const [claimedByFirst] = await leaseStorage.claim({ id: "lease-worker-1", queues: ["leases"], concurrency: 1 }, { leaseMs: 1 });
assert.equal(claimedByFirst.lockedBy, "lease-worker-1");
leaseDb.jobs[0].locked_until = new Date(Date.now() - 1000).toISOString();
const [claimedBySecond] = await leaseStorage.claim({ id: "lease-worker-2", queues: ["leases"], concurrency: 1 }, { leaseMs: 1000 });
assert.equal(claimedBySecond.lockedBy, "lease-worker-2");

const manyDb = new FakeJobsDb("sqlite");
const manyStorage = Jobs.storage.sqlite(manyDb);
await manyStorage.init();
const manyRuntime = Jobs.create({ storage: manyStorage });
manyRuntime.define("many", {}, async () => {});
for (let index = 0; index < 130; index += 1) {
    await manyRuntime.enqueue("many", { index }, { idempotencyKey: `many:${index}` });
}
manyDb.events.push(...Array.from({ length: 130 }, (_, index) => ({
    id: `extra-event-${index}`,
    job_id: manyDb.jobs[0].id,
    event_type: "extra",
    from_status: null,
    to_status: null,
    created_at: new Date(Date.now() + index).toISOString(),
    worker_id: null,
    message: null,
    data_json: "{}",
})));
manyDb.attempts.push(...Array.from({ length: 130 }, (_, index) => ({
    id: `extra-attempt-${index}`,
    job_id: manyDb.jobs[0].id,
    worker_id: "worker",
    attempt_number: index + 1,
    started_at: new Date(Date.now() + index).toISOString(),
    finished_at: null,
    status: "processing",
    duration_ms: null,
    error_code: null,
    error_message: null,
    diagnostic_id: null,
})));
manyDb.workers.push(...Array.from({ length: 130 }, (_, index) => ({
    id: `worker-${index}`,
    worker_name: `worker-${index}`,
    host: null,
    pid: null,
    queues: "[\"default\"]",
    started_at: new Date(Date.now() + index).toISOString(),
    last_heartbeat_at: new Date(Date.now() + index).toISOString(),
    status: "running",
})));
manyDb.locks.push(...Array.from({ length: 130 }, (_, index) => ({
    name: `lock-${index}`,
    owner: "owner",
    locked_until: new Date(Date.now() + 60000).toISOString(),
    updated_at: new Date(Date.now() + index).toISOString(),
})));
manyDb.recurring.push(...Array.from({ length: 130 }, (_, index) => ({
    id: `recurring-${index}`,
    name: `recurring-${index}`,
    job_name: "many",
    queue: "default",
    cron: "* * * * *",
    timezone: "UTC",
    payload_json: "{}",
    enabled: 1,
    misfire_policy: "run-once",
    last_run_at: null,
    next_run_at: new Date(Date.now() + 60000 + index).toISOString(),
    created_at: new Date(Date.now() + index).toISOString(),
    updated_at: new Date(Date.now() + index).toISOString(),
    metadata_json: "{}",
})));
assert.equal((await manyStorage.listJobs({ pageSize: 130 })).length, 130);
assert.equal((await manyStorage.events(manyDb.jobs[0].id)).length, 131);
assert.equal((await manyStorage.attempts(manyDb.jobs[0].id)).length, 130);
assert.equal((await manyStorage.listWorkers()).length, 130);
assert.equal((await manyStorage.listLocks()).length, 130);
assert.equal((await manyStorage.listRecurring()).length, 130);
for (const job of manyDb.jobs) {
    Object.assign(job, {
        status: "succeeded",
        updated_at: "2026-01-01T00:00:00.000Z",
    });
}
assert.equal((await manyStorage.cleanup({ batchSize: 130, keepSucceededMs: 0 })).deleted, 130);
for (const fragment of [
    "from sloppy_jobs",
    "from sloppy_job_events",
    "from sloppy_job_attempts",
    "from sloppy_job_workers",
    "from sloppy_job_locks",
    "from sloppy_recurring_jobs",
]) {
    assert(manyDb.queries.some((entry) =>
        entry.kind === "query" &&
        entry.sql.toLowerCase().includes(fragment) &&
        entry.options?.maxRows !== undefined &&
        entry.options.maxRows > 128));
}

const pg = new FakeJobsDb("postgres");
const pgStorage = Jobs.storage.postgres(pg);
await pgStorage.init();
const pgRuntime = Jobs.create({ storage: pgStorage });
pgRuntime.define("pg-job", {}, async () => {});
await pgRuntime.enqueue("pg-job", {}, { queue: "default", idempotencyKey: "pg:1" });
await pgRuntime.enqueue("pg-job", {}, { queue: "default", idempotencyKey: "pg:1" });
await pgStorage.claim({ id: "pg-worker", queues: ["default"], concurrency: 1 }, { leaseMs: 1000 });
assert(pg.queries.some((entry) =>
    entry.sql.includes("for update skip locked") &&
    entry.sql.includes("$1") &&
    entry.sql.includes("$2") &&
    entry.sql.includes("$3")));
assert(pg.queries.some((entry) => entry.sql.includes("where idempotency_key = $1")));
assert(!pg.queries.some((entry) => entry.params.length > 0 && entry.sql.includes("?")));

const sqlserver = new FakeJobsDb("sqlserver");
const sqlserverStorage = Jobs.storage.sqlserver(sqlserver);
await sqlserverStorage.init();
assert(sqlserver.queries.some((entry) => entry.sql.includes("object_id(N'dbo.sloppy_jobs'")));
const sqlserverRuntime = Jobs.create({ storage: sqlserverStorage });
sqlserverRuntime.define("sqlserver-job", {}, async () => {});
await sqlserverRuntime.enqueue("sqlserver-job", {}, { queue: "default" });
await sqlserverStorage.claim({ id: "sqlserver-worker", queues: ["default"], concurrency: 1 }, { leaseMs: 1000 });
assert(sqlserver.queries.some((entry) =>
    entry.sql.toLowerCase().includes("with (updlock, readpast, rowlock)") &&
    entry.sql.toLowerCase().includes("select top")));
