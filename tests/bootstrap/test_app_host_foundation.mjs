import assert from "node:assert/strict";

import { Sloppy, Results } from "../../stdlib/sloppy/index.js";

function assertThrowsMessage(fn, expected) {
    assert.throws(fn, (error) => {
        assert.match(String(error.message), expected);
        return true;
    });
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
    assert.equal(builder.config.get("missing", "fallback"), "fallback");
    assert.equal(builder.config.has("server.port"), true);
    assert.equal(builder.config.require("server.port"), 3000);
    assertThrowsMessage(() => builder.config.require("missing"), /required/);
    assertThrowsMessage(() => builder.config.get(""), /non-empty string/);

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

    const beforeFreeze = app.__getRoutes();
    assert.equal(beforeFreeze.length, 1);
    assert.equal(beforeFreeze[0].method, "GET");
    assert.equal(beforeFreeze[0].pattern, "/");
    assert.equal(beforeFreeze[0].name, "Hello.Index");
    assert.equal(beforeFreeze[0].handler().body, "second: Hello from Sloppy");

    assert.equal(app.isFrozen(), false);
    assert.equal(app.freeze(), app);
    assert.equal(app.freeze(), app);
    assert.equal(app.isFrozen(), true);

    const afterFreeze = app.__getRoutes();
    assert.equal(afterFreeze.length, 1);
    assert.equal(afterFreeze[0].pattern, beforeFreeze[0].pattern);
    assert.equal(afterFreeze[0].name, beforeFreeze[0].name);
    assertThrowsMessage(() => app.mapGet("/late", () => Results.text("late")), /app is frozen/);
}

{
    const app = Sloppy.create();

    assert.equal(typeof app.config.get, "function");
    assert.equal(typeof app.log.info, "function");
    assert.equal(typeof app.services.createScope, "function");

    app.mapGet("/tiny", () => Results.text("tiny"));
    assert.equal(app.__getRoutes()[0].handler().body, "tiny");
}
