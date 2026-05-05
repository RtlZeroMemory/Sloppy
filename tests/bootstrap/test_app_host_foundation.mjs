import assert from "node:assert/strict";

import {
    CancelledError,
    CancellationController,
    Deadline,
    InvalidDeadlineError,
    Results,
    Sloppy,
    Time,
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
    assertThrowsMessage(() => Time.fakeClock(), /SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE/);
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
