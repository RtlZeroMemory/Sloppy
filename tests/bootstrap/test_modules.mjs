import assert from "node:assert/strict";

import { Sloppy, Results } from "../../stdlib/sloppy/index.js";

function assertThrowsMessage(fn, expected) {
    assert.throws(fn, (error) => {
        assert.match(String(error.message), expected);
        return true;
    });
}

{
    assert.equal(typeof Sloppy.module, "function");

    const metadata = {
        owner: "tests",
        tags: ["bootstrap"],
    };
    const users = Sloppy.module("users")
        .dependsOn("data")
        .metadata("details", metadata)
        .services(() => {})
        .routes(() => {});
    metadata.owner = "mutated";
    metadata.tags.push("changed");

    assert.equal(users.name, "users");
    assert.deepEqual(users.dependencies, ["data"]);
    assert.equal(users.__debug().services, 1);
    assert.equal(users.__debug().routes, 1);
    assert.deepEqual(users.__debug().metadata, {
        details: {
            owner: "tests",
            tags: ["bootstrap"],
        },
    });
    assert.equal(Object.isFrozen(users.__debug().metadata.details), true);
    assert.equal(Object.isFrozen(users.__debug().metadata.details.tags), true);
    assert.deepEqual(Object.getOwnPropertySymbols(users), []);
    assertThrowsMessage(() => Sloppy.module(""), /non-empty string/);
    assertThrowsMessage(() => Sloppy.module("Users"), /lowercase/);
    Sloppy.createBuilder().addModule(users);
    assertThrowsMessage(() => users.dependsOn("later"), /frozen/);
    assert.deepEqual(Object.getOwnPropertySymbols(users), []);
}

{
    const calls = [];

    const DataModule = Sloppy.module("data")
        .services((services) => {
            calls.push("data:services");
            services.addSingleton("data.message", () => "hello");
        })
        .routes(() => {
            calls.push("data:routes");
        });

    const UsersModule = Sloppy.module("users")
        .dependsOn("data")
        .metadata("area", "accounts")
        .services((services) => {
            calls.push("users:services");
            services.addSingleton("users.message", (scope) => `${scope.get("data.message")} users`);
        })
        .routes((app) => {
            calls.push("users:routes");
            app.mapGroup("/users")
                .withTags("Users")
                .mapGet("/{id:int}", ({ route, services }) => Results.ok({
                    id: route.id ?? "demo",
                    message: services.get("users.message"),
                }))
                .withName("Users.Get");
        });

    const builder = Sloppy.createBuilder();
    assert.equal(builder.addModule(UsersModule), builder);
    builder.addModule(DataModule);

    const app = builder.build();

    assert.deepEqual(calls, [
        "data:services",
        "users:services",
        "data:routes",
        "users:routes",
    ]);
    assert.equal(app.services.get("users.message"), "hello users");

    const routes = app.__getRoutes();
    assert.equal(routes.length, 1);
    assert.equal(routes[0].method, "GET");
    assert.equal(routes[0].pattern, "/users/{id:int}");
    assert.equal(routes[0].name, "Users.Get");
    assert.equal(routes[0].metadata.module, "users");
    assert.deepEqual(routes[0].metadata.tags, ["Users"]);
    assert.deepEqual(routes[0].handler().body, {
        id: "demo",
        message: "hello users",
    });

    const modules = app.__debug().modules;
    assert.deepEqual(modules.map((module) => module.name), ["data", "users"]);
    assert.deepEqual(app.__getModuleGraph().map((module) => module.name), ["data", "users"]);
    assert.deepEqual(app.__getPlanContributions().modules.map((module) => module.name), [
        "data",
        "users",
    ]);
    assert.deepEqual(modules[0].services, ["data.message"]);
    assert.deepEqual(modules[0].routes, []);
    assert.deepEqual(modules[1].dependencies, ["data"]);
    assert.deepEqual(modules[1].services, ["users.message"]);
    assert.deepEqual(modules[1].routes, ["GET /users/{id:int}"]);
    assert.deepEqual(modules[1].metadata, { area: "accounts" });
    assert.deepEqual(modules[1].contributes, ["services", "routes", "metadata"]);

    assertThrowsMessage(() => builder.addModule(Sloppy.module("later")), /builder is frozen/);
}

{
    const real = Sloppy.module("real");
    const symbols = Object.getOwnPropertySymbols(real);
    const builder = Sloppy.createBuilder();
    builder.addModule(Sloppy.module("alpha"));
    assertThrowsMessage(() => builder.addModule(Sloppy.module("alpha")), /already registered/);
    assertThrowsMessage(() => Sloppy.createBuilder().addModule({ name: "fake" }), /Sloppy.module/);

    const fake = {
        name: "fake",
    };

    for (const symbol of symbols) {
        fake[symbol] = {
            name: "fake",
            dependencies: [],
            serviceCallbacks: [],
            routeCallbacks: [],
            metadata: Object.create(null),
            finalized: false,
        };
    }

    assertThrowsMessage(() => Sloppy.createBuilder().addModule(fake), /Sloppy.module/);
}

{
    let ran = false;
    const module = Sloppy.module("locked");
    const builder = Sloppy.createBuilder();

    builder.addModule(module);

    for (const value of Object.values(module)) {
        if (Array.isArray(value)) {
            assertThrowsMessage(() => value.push("mutated"), /read only|not extensible|frozen/);
        }
    }

    assertThrowsMessage(() => module.routes(() => {
        ran = true;
    }), /frozen/);
    builder.build();
    assert.equal(ran, false);
}

{
    const builder = Sloppy.createBuilder();
    builder.addModule(Sloppy.module("users").dependsOn("data"));

    assertThrowsMessage(() => builder.build(), /dependency missing[\s\S]*users[\s\S]*data/);
}

{
    const builder = Sloppy.createBuilder();
    builder.addModule(Sloppy.module("a").dependsOn("b"));
    builder.addModule(Sloppy.module("b").dependsOn("a"));

    assertThrowsMessage(() => builder.build(), /cycle detected[\s\S]*a -> b -> a/);
}

{
    const builder = Sloppy.createBuilder();
    builder.addModule(Sloppy.module("first"));
    builder.addModule(Sloppy.module("second"));

    assert.deepEqual(builder.build().__debug().modules.map((module) => module.name), [
        "first",
        "second",
    ]);
}

{
    const builder = Sloppy.createBuilder();
    builder.addModule(Sloppy.module("broken").services(() => {
        throw new Error("boom");
    }));

    assertThrowsMessage(() => builder.build(), /phase failed[\s\S]*broken[\s\S]*services[\s\S]*boom/);
}

{
    const builder = Sloppy.createBuilder();
    builder.addModule(Sloppy.module("broken").routes(() => {
        throw new Error("route boom");
    }));

    assertThrowsMessage(
        () => builder.build(),
        /phase failed[\s\S]*broken[\s\S]*routes[\s\S]*route boom/,
    );
}

{
    assertThrowsMessage(
        () => Sloppy.module("broken").capabilities(async () => {}),
        /capabilities phase callback must be synchronous/,
    );

    const builder = Sloppy.createBuilder();
    builder.addModule(Sloppy.module("broken").capabilities(() => Promise.resolve()));

    assertThrowsMessage(
        () => builder.build(),
        /phase failed[\s\S]*broken[\s\S]*capabilities[\s\S]*synchronous/,
    );
}
