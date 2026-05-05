import assert from "node:assert/strict";

import {
    CancelledError,
    CancellationController,
    Deadline,
    Directory,
    File,
    InvalidDeadlineError,
    Results,
    Sloppy,
    Time,
    TimerDisposedError,
    TimeoutError,
    schema,
} from "../../stdlib/sloppy/index.js";
import { sqlite } from "../../stdlib/sloppy/providers/sqlite.js";

function assertThrowsMessage(fn, expected) {
    assert.throws(fn, (error) => {
        assert.match(String(error.message), expected);
        return true;
    });
}

async function flushMicrotasks(count = 6) {
    for (let i = 0; i < count; i += 1) {
        await Promise.resolve();
    }
}

{
    const deadline = Deadline.after(50);
    assert.equal(deadline.kind, "after");
    assert.equal(deadline.expired, false);
    assert.equal(typeof deadline.remainingMs(), "number");
    assert.equal(Deadline.never().remainingMs(), Infinity);

    const controller = new CancellationController();
    let observedReason = undefined;
    controller.signal.addEventListener("abort", () => {
        observedReason = controller.signal.reason;
    });
    assert.equal(controller.cancel("done"), true);
    assert.equal(controller.cancel("again"), false);
    assert.equal(controller.signal.aborted, true);
    assert.equal(controller.signal.reason, "done");
    assert.equal(observedReason, "done");
    assert.throws(() => controller.signal.throwIfCancelled(), CancelledError);

    const fanoutController = new CancellationController();
    let fanoutObserved = false;
    fanoutController.signal.addEventListener("abort", () => {
        throw new Error("listener failed");
    });
    fanoutController.signal.addEventListener("abort", () => {
        fanoutObserved = true;
    });
    assert.throws(() => fanoutController.cancel("fanout"), AggregateError);
    assert.equal(fanoutObserved, true);

    await assert.rejects(Time.delay(10, { signal: controller.signal }), CancelledError);
    let cancelledTimeoutInvoked = false;
    await assert.rejects(
        Time.timeout(
            () => {
                cancelledTimeoutInvoked = true;
            },
            { afterMs: 10, signal: controller.signal },
        ),
        CancelledError,
    );
    assert.equal(cancelledTimeoutInvoked, false);

    let expiredTimeoutInvoked = false;
    await assert.rejects(
        Time.timeout(
            () => {
                expiredTimeoutInvoked = true;
            },
            { afterMs: 0 },
        ),
        TimeoutError,
    );
    assert.equal(expiredTimeoutInvoked, false);

    assert.throws(() => Time.delay(-1), InvalidDeadlineError);
    assert.throws(() => Time.delay(0x100000000), InvalidDeadlineError);

    const previousSloppy = globalThis.__sloppy;
    try {
        globalThis.__sloppy = {
            time: {
                delay() {
                    return Promise.reject(new Error("Sloppy timer was disposed before completion"));
                },
                monotonicMs() {
                    return Date.now();
                },
            },
        };
        await assert.rejects(Time.delay(1), TimerDisposedError);
    } finally {
        if (previousSloppy === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previousSloppy;
        }
    }

    const clock = Time.fakeClock({ now: new Date("2026-01-01T00:00:00.000Z") });
    assert.equal(clock.kind, "fake");
    assert.equal(clock.now().toISOString(), "2026-01-01T00:00:00.000Z");

    let delayed = false;
    const delayPromise = Time.delay(1000, { clock }).then(() => {
        delayed = true;
    });
    clock.advanceBy(999);
    await flushMicrotasks();
    assert.equal(delayed, false);
    clock.advanceBy(1);
    await delayPromise;
    assert.equal(delayed, true);

    const timeoutPromise = Time.timeout(new Promise(() => {}), { afterMs: 500, clock });
    clock.advanceBy(500);
    await assert.rejects(timeoutPromise, TimeoutError);

    const promiseTimeoutClock = Time.fakeClock();
    assert.equal(
        await Time.timeout(Promise.resolve("fast"), { afterMs: 1000, clock: promiseTimeoutClock }),
        "fast",
    );
    assert.equal(promiseTimeoutClock._timers.length, 0);

    const functionTimeoutClock = Time.fakeClock();
    assert.equal(
        await Time.timeout(() => "fast", { afterMs: 1000, clock: functionTimeoutClock }),
        "fast",
    );
    assert.equal(functionTimeoutClock._timers.length, 0);

    assert.throws(
        () => Time.delay(100, { clock: Time.fakeClock(), deadline: Deadline.after(100) }),
        InvalidDeadlineError,
    );
    assert.throws(
        () => Time.timeout(Promise.resolve("x"), { clock: Time.fakeClock(), deadline: Deadline.after(100) }),
        InvalidDeadlineError,
    );

    const orderedClock = Time.fakeClock();
    const timerOrder = [];
    const laterTimer = Time.delay(200, { clock: orderedClock }).then(() => {
        timerOrder.push("later");
    });
    const earlierTimer = Time.delay(100, { clock: orderedClock }).then(() => {
        timerOrder.push("earlier");
    });
    orderedClock.advanceBy(200);
    await Promise.all([laterTimer, earlierTimer]);
    assert.deepEqual(timerOrder, ["earlier", "later"]);

    const immediateInterval = Time.interval(1000, { clock, immediate: true, maxTicks: 1 });
    assert.equal((await immediateInterval.next()).value.index, 1);
    assert.equal((await immediateInterval.next()).done, true);

    const guardedInterval = Time.interval(1000, { clock, maxTicks: 1 });
    const guardedTick = guardedInterval.next();
    await assert.rejects(guardedInterval.next(), /overlapping next\(\) calls/);
    clock.advanceBy(1000);
    assert.equal((await guardedTick).value.index, 1);

    const boundedIntervalController = new CancellationController();
    const boundedInterval = Time.interval(1000, {
        clock,
        maxTicks: 1,
        signal: boundedIntervalController.signal,
    });
    const boundedTick = boundedInterval.next();
    clock.advanceBy(1000);
    assert.equal((await boundedTick).value.index, 1);
    assert.equal(boundedIntervalController.signal._listeners.size, 0);

    const interval = Time.interval("1s", { clock, maxTicks: 2 });
    const firstTick = interval.next();
    clock.advanceBy(1000);
    assert.equal((await firstTick).value.index, 1);
    const secondTick = interval.next();
    clock.advanceBy(1000);
    assert.equal((await secondTick).value.index, 2);
    assert.equal((await interval.next()).done, true);

    let scheduledRuns = 0;
    const job = Time.every(
        "1s",
        () => {
            scheduledRuns += 1;
        },
        { clock },
    );
    clock.advanceBy(1000);
    await flushMicrotasks();
    assert.equal(scheduledRuns, 1);
    job.pause();
    clock.advanceBy(2000);
    await flushMicrotasks();
    assert.equal(scheduledRuns, 1);
    job.resume();
    clock.advanceBy(1000);
    await flushMicrotasks();
    assert.equal(scheduledRuns, 2);
    await job.stop();
    assert.equal(job.stopped, true);

    let immediateJobRuns = 0;
    const immediateJob = Time.every(
        1000,
        () => {
            immediateJobRuns += 1;
        },
        { clock, immediate: true, maxRuns: 1 },
    );
    await flushMicrotasks();
    assert.equal(immediateJobRuns, 1);
    assert.equal(immediateJob.stopped, true);

    let releaseJob = undefined;
    let noOverlapRuns = 0;
    const noOverlapJob = Time.every(
        1000,
        async () => {
            noOverlapRuns += 1;
            await new Promise((resolve) => {
                releaseJob = resolve;
            });
        },
        { clock },
    );
    clock.advanceBy(1000);
    await flushMicrotasks();
    assert.equal(noOverlapRuns, 1);
    clock.advanceBy(3000);
    await flushMicrotasks();
    assert.equal(noOverlapRuns, 1);
    assert.equal(noOverlapJob.skippedRuns, 3);
    releaseJob();
    await flushMicrotasks();
    await noOverlapJob.stop();

    const cancelJobClock = Time.fakeClock();
    const cancelJobController = new CancellationController();
    const cancelJob = Time.every(1000, () => {}, {
        clock: cancelJobClock,
        signal: cancelJobController.signal,
    });
    cancelJobController.cancel("done");
    await flushMicrotasks();
    assert.equal(cancelJob.stopped, true);
    assert.equal(cancelJob.nextRun, null);

    const disposedJobClock = Time.fakeClock();
    const disposedJob = Time.every(1000, () => {}, { clock: disposedJobClock });
    disposedJobClock.dispose();
    await flushMicrotasks();
    assert.equal(disposedJob.stopped, true);
    assert.equal(disposedJob.nextRun, null);

    const disposedClock = Time.fakeClock();
    const disposedDelay = Time.delay(100, { clock: disposedClock });
    disposedClock.dispose();
    await assert.rejects(disposedDelay, TimerDisposedError);
    assert.throws(() => disposedClock.advanceBy(1), TimerDisposedError);
}

{
    const previousSloppy = globalThis.__sloppy;
    let readCalls = 0;
    try {
        globalThis.__sloppy = {
            fs: {
                readText() {
                    readCalls += 1;
                    return Promise.resolve("ok");
                },
                stat() {
                    readCalls += 1;
                    return Promise.resolve({ exists: true, kind: "directory" });
                },
            },
        };

        const cancelledController = new CancellationController();
        cancelledController.cancel("caller stopped");
        await assert.rejects(
            File.readText("data:/users.json", { signal: cancelledController.signal }),
            CancelledError,
        );
        assert.equal(readCalls, 0);

        await assert.rejects(
            File.readText("data:/users.json", { deadline: Deadline.after(0) }),
            TimeoutError,
        );
        assert.equal(readCalls, 0);

        await assert.rejects(
            File.readText("data:/users.json", { timeoutMs: 0 }),
            TimeoutError,
        );
        assert.equal(readCalls, 0);

        assert.throws(
            () => File.readText("data:/users.json", { timeoutMs: -1 }),
            InvalidDeadlineError,
        );
        assert.equal(readCalls, 0);

        assert.equal(await File.readText("data:/users.json", { deadline: Deadline.never() }), "ok");
        assert.equal(await Directory.exists("data:/", { deadline: Deadline.never() }), true);
        assert.equal(readCalls, 2);

        const pendingController = new CancellationController();
        const pendingRead = File.readText("data:/later.txt", { signal: pendingController.signal });
        pendingController.cancel("not needed");
        await assert.rejects(pendingRead, CancelledError);
        assert.equal(readCalls, 3);
    } finally {
        if (previousSloppy === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previousSloppy;
        }
    }
}

{
    const builder = Sloppy.createBuilder();

    builder.config.addObject({
        "app.name": "first",
        "server.port": 3000,
    });
    builder.config.addObject({
        "app.name": "second",
    });

    assert.equal(builder.config.get("app.name"), "second");
    assert.equal(builder.config.get("APP.NAME"), "second");
    assert.equal(builder.config.get("missing", "fallback"), "fallback");
    assert.equal(builder.config.has("server.port"), true);
    assert.equal(builder.config.require("server.port"), 3000);
    assert.equal(builder.config.getInt("server.port"), 3000);
    assert.equal(builder.config.getString("Sloppy:Server:Host", "127.0.0.1"), "127.0.0.1");
    assert.equal(builder.config.getBool("Feature:X", false), false);
    assert.equal(builder.config.getNumber("Feature:Limit", 1.5), 1.5);
    assertThrowsMessage(() => builder.config.getInt("app.name"), /number/);
    assertThrowsMessage(() => builder.config.require("missing"), /required/);
    assertThrowsMessage(() => builder.config.get(""), /non-empty string/);
    assertThrowsMessage(() => builder.config.get("A::B"), /empty segments/);

    const memorySink = builder.logging.addMemorySink();
    builder.logging.setMinimumLevel("info");
    assertThrowsMessage(() => builder.logging.setMinimumLevel("verbose"), /log level/);

    let singletonCalls = 0;
    let transientCalls = 0;
    builder.services.addSingleton("message", () => {
        singletonCalls += 1;
        return "Hello from Sloppy";
    });
    builder.services.addTransient("clock", () => {
        transientCalls += 1;
        return { now: () => transientCalls };
    });

    assertThrowsMessage(
        () => builder.services.addSingleton("message", "duplicate"),
        /already registered/,
    );
    assertThrowsMessage(() => builder.services.addTransient("", () => "bad"), /non-empty string/);
    assertThrowsMessage(() => builder.services.addTransient("bad", 123), /factory/);

    const app = builder.build();

    assertThrowsMessage(() => builder.config.addObject({ later: true }), /builder is frozen/);
    assertThrowsMessage(() => builder.logging.addMemorySink(), /builder is frozen/);
    assertThrowsMessage(() => builder.services.addSingleton("later", "value"), /builder is frozen/);
    assertThrowsMessage(() => builder.build(), /builder is frozen/);

    assert.equal(app.config.getInt("server.port"), 3000);

    const scope = app.services.createScope();
    assert.equal(scope.get("message"), "Hello from Sloppy");
    assert.equal(scope.get("message"), "Hello from Sloppy");
    assert.equal(singletonCalls, 1);
    assert.equal(scope.get("clock").now(), 1);
    assert.equal(scope.get("clock").now(), 2);
    assert.equal(transientCalls, 2);
    assert.equal(app.services.get("message"), "Hello from Sloppy");
    assertThrowsMessage(() => scope.get("missing"), /not registered/);

    const fields = { route: "/" };
    app.log.debug("filtered", fields);
    app.log.info("hello", fields);
    assert.equal(memorySink.entries().length, 1);
    assert.deepEqual(memorySink.entries()[0], {
        level: "info",
        message: "hello",
        fields,
    });
    assert.equal(memorySink.entries()[0].fields, fields);

    app.mapGet("/", ({ config, log, services }) => {
        log.info("handler", { route: "/" });
        return Results.text(`${config.require("app.name")}: ${services.get("message")}`);
    }).withName("Hello.Index");

    const querySchema = schema.object({
        q: schema.string().min(1),
    });

    const users = app
        .mapGroup("/users/")
        .withTags("Users")
        .withName("Users");

    assertThrowsMessage(() => app.mapGroup("users"), /starting with/);

    users.mapGet("{id:int}", { query: querySchema }, ({ route }) => {
        return Results.ok({ id: route.id ?? "demo" });
    }).withName("Users.Get");

    const beforeFreeze = app.__getRoutes();
    assert.equal(beforeFreeze.length, 2);
    assert.equal(beforeFreeze[0].method, "GET");
    assert.equal(beforeFreeze[0].pattern, "/");
    assert.equal(beforeFreeze[0].name, "Hello.Index");
    assert.equal(beforeFreeze[0].handler().body, "second: Hello from Sloppy");
    assert.equal(beforeFreeze[1].method, "GET");
    assert.equal(beforeFreeze[1].pattern, "/users/{id:int}");
    assert.equal(beforeFreeze[1].name, "Users.Get");
    assert.deepEqual(beforeFreeze[1].metadata.tags, ["Users"]);
    assert.equal(beforeFreeze[1].metadata.groupName, "Users");
    assert.equal(beforeFreeze[1].metadata.groupPrefix, "/users");
    assert.equal(beforeFreeze[1].metadata.query, querySchema);
    assert.deepEqual(beforeFreeze[1].handler().body, { id: "demo" });

    assert.equal(app.isFrozen(), false);
    assert.equal(app.freeze(), app);
    assert.equal(app.freeze(), app);
    assert.equal(app.isFrozen(), true);

    const afterFreeze = app.__getRoutes();
    assert.equal(afterFreeze.length, 2);
    assert.equal(afterFreeze[0].pattern, beforeFreeze[0].pattern);
    assert.equal(afterFreeze[0].name, beforeFreeze[0].name);
    assertThrowsMessage(() => app.mapGet("/late", () => Results.text("late")), /app is frozen/);
    assertThrowsMessage(() => users.mapGet("/late", () => Results.text("late")), /app is frozen/);
}

{
    class SqliteOptions {
        constructor(values) {
            this.database = values.database;
            this.queueCapacity = values.queueCapacity;
        }
    }

    const builder = Sloppy.createBuilder();
    builder.config.addObject({
        Sloppy: {
            Server: {
                MaxRequestBodyBytes: 16384,
                RequestTimeoutMs: 15000,
            },
            Providers: {
                sqlite: {
                    main: {
                        database: "./app.db",
                        queueCapacity: 8,
                    },
                },
            },
        },
    });
    const app = builder.build();
    const options = app.config.bind("sqlite:main", SqliteOptions);
    assert.equal(options.database, "./app.db");
    assert.equal(options.queueCapacity, 8);
    const serverOptions = app.config.bind("Sloppy:Server");
    assert.equal(serverOptions.maxRequestBodyBytes, 16384);
    assert.equal(serverOptions.requestTimeoutMs, 15000);

    const provider = app.use(sqlite("main", { database: ":memory:" }));
    assert.equal(provider.kind, "sqlite");
    assert.equal(provider.name, "main");
    assert.equal(provider.options.database, ":memory:");
    assert.equal(provider.options.queueCapacity, 8);
}

{
    const app = Sloppy.create();

    assertThrowsMessage(() => app.use(sqlite("missing")), /database option/);
    assertThrowsMessage(() => sqlite("bad:name"), /provider name/);
    assertThrowsMessage(() => sqlite(" main "), /provider name/);

    const provider = app.use({
        __sloppyProvider: true,
        kind: "sqlite",
        name: "main",
        token: "wrong.token",
        options: { database: ":memory:" },
    });
    assert.equal(provider.token, "data.main");
}

{
    const app = Sloppy.create();

    assert.equal(typeof app.config.get, "function");
    assert.equal(typeof app.log.info, "function");
    assert.equal(typeof app.services.createScope, "function");

    app.mapGet("/tiny", () => Results.text("tiny"));
    app.mapPost("/tiny", () => Results.json({ method: "POST" }));
    app.mapPut("/tiny", () => Results.json({ method: "PUT" }));
    app.mapPatch("/tiny", () => Results.json({ method: "PATCH" }));
    app.mapDelete("/tiny", () => Results.noContent());
    app.mapGroup("/health").withTags("Health").mapGet("/", () => Results.noContent());
    app.mapGroup("/jobs").withTags("Jobs").mapPost("/", () => Results.accepted());
    assert.equal(app.__getRoutes()[0].handler().body, "tiny");
    assert.equal(app.__getRoutes()[1].method, "POST");
    assert.equal(app.__getRoutes()[2].method, "PUT");
    assert.equal(app.__getRoutes()[3].method, "PATCH");
    assert.equal(app.__getRoutes()[4].method, "DELETE");
    assert.equal(app.__getRoutes()[5].pattern, "/health");
    assert.equal(app.__getRoutes()[6].method, "POST");
    assert.equal(app.__getRoutes()[6].pattern, "/jobs");
}

{
    assert.deepEqual(Results.ok({ ok: true }), {
        __sloppyResult: true,
        kind: "json",
        status: 200,
        body: { ok: true },
        contentType: "application/json; charset=utf-8",
        headers: undefined,
    });
    assert.equal(Results.created("/users/1", { id: 1 }).status, 201);
    assert.equal(Results.created("/users/1", { id: 1 }).location, "/users/1");
    assert.equal(Results.accepted({ queued: true }).status, 202);
    assert.equal(Results.noContent().status, 204);
    assert.equal(Object.prototype.hasOwnProperty.call(Results.noContent(), "body"), false);
    assert.equal(Results.notFound().status, 404);
    assert.equal(Results.badRequest({ error: "bad" }).status, 400);
    assert.deepEqual(Results.status(202, { accepted: true }).body, { accepted: true });
    assert.equal(Results.status(204).kind, "empty");
    assert.equal(Results.problem("broken").kind, "problem");
    assert.equal(Results.problem("broken").body.status, 500);
    assert.equal(Results.html("<p>ok</p>").contentType, "text/html; charset=utf-8");
    assert.deepEqual(Results.json({ ok: true }, { headers: { "x-test": "1" } }).headers, {
        "x-test": "1",
    });
    assertThrowsMessage(() => Results.ok("bad", { status: 99 }), /status/);
    assertThrowsMessage(() => Results.ok("bad", { headers: new Map() }), /plain object/);
}

{
    const Email = schema.string().min(3).email();
    assert.equal(Email.kind, "string");
    assert.deepEqual(Email.validate("a@example.com"), {
        ok: true,
        value: "a@example.com",
    });

    const invalidEmail = Email.validate("no");
    assert.equal(invalidEmail.ok, false);
    assert.deepEqual(invalidEmail.issues.map((current) => current.code), [
        "string.min",
        "string.email",
    ]);
    assert.deepEqual(Email.metadata.rules.map((rule) => rule.kind), ["min", "email"]);

    const User = schema.object({
        name: schema.string().min(1),
        age: schema.number(),
        active: schema.boolean(),
    });

    assert.equal(User.kind, "object");
    assert.deepEqual(User.validate({
        name: "Ada",
        age: 37,
        active: true,
    }).ok, true);

    const invalidUser = User.validate({
        name: "",
        age: Number.NaN,
        active: "yes",
    });

    assert.equal(invalidUser.ok, false);
    assert.deepEqual(invalidUser.issues.map((current) => current.path.join(".")), [
        "name",
        "age",
        "active",
    ]);
    assert.equal(User.metadata.shape.name.kind, "string");
    assertThrowsMessage(() => schema.object({ bad: {} }), /must be a schema/);
    assertThrowsMessage(
        () => schema.object({ bad: { validate() { return { ok: true }; } } }),
        /must be a schema/,
    );
}
