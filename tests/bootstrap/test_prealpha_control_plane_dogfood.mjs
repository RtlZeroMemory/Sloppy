import assert from "node:assert/strict";
import { cp, mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

import {
    ProblemDetails,
    RequestId,
    RequestLogging,
    Results,
    Sloppy,
} from "../../stdlib/sloppy/index.js";
import { createTestHost } from "../../stdlib/sloppy/testing.js";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const exampleRoot = path.join(repoRoot, "examples", "prealpha-control-plane");

class FakeControlPlaneDb {
    #nextProjectId = 3;
    #nextAppId = 3;
    #nextBuildId = 2;
    #nextDeploymentId = 1;

    constructor() {
        this.projects = [];
        this.apps = [];
        this.builds = [];
        this.deployments = [];
        this.diagnostics = [];
    }

    async exec(sql, params = []) {
        const statement = sql.toLowerCase();
        if (statement.startsWith("create table")) {
            return;
        }

        if (statement.includes("insert into projects (id,")) {
            this.#insertSeed(this.projects, params, ["id", "slug", "name", "owner"]);
            return;
        }
        if (statement.includes("insert into projects (slug,")) {
            this.projects.push({
                id: this.#nextProjectId,
                slug: params[0],
                name: params[1],
                owner: params[2],
            });
            this.#nextProjectId += 1;
            return;
        }

        if (statement.includes("insert into apps (id,")) {
            this.#insertSeed(this.apps, params, ["id", "project_id", "name", "environment"]);
            return;
        }
        if (statement.includes("insert into apps (project_id,")) {
            this.apps.push({
                id: this.#nextAppId,
                project_id: Number(params[0]),
                name: params[1],
                environment: params[2],
            });
            this.#nextAppId += 1;
            return;
        }

        if (statement.includes("insert into builds (id,")) {
            this.#insertSeed(this.builds, params, ["id", "app_id", "commit_sha", "status"]);
            return;
        }
        if (statement.includes("insert into builds (app_id,")) {
            this.builds.push({
                id: this.#nextBuildId,
                app_id: Number(params[0]),
                commit_sha: params[1],
                status: params[2],
            });
            this.#nextBuildId += 1;
            return;
        }

        if (statement.includes("insert into deployments (app_id,")) {
            this.deployments.push({
                id: this.#nextDeploymentId,
                app_id: Number(params[0]),
                build_id: Number(params[1]),
                status: params[2],
            });
            this.#nextDeploymentId += 1;
            return;
        }

        if (statement.includes("insert into diagnostics (id,")) {
            this.#insertSeed(this.diagnostics, params, ["id", "level", "message"]);
            return;
        }

        throw new Error(`unexpected exec statement: ${sql}`);
    }

    async query(sql, params = []) {
        const statement = sql.toLowerCase();
        if (statement.includes("from projects where owner")) {
            return this.projects.filter((project) => project.owner === params[0]).sort(compareById);
        }
        if (statement.includes("from projects order by id")) {
            return [...this.projects].sort(compareById);
        }
        if (statement.includes("from apps order by id")) {
            return [...this.apps].sort(compareById);
        }
        if (statement.includes("from builds where app_id")) {
            return this.builds.filter((build) => build.app_id === Number(params[0])).sort(compareById);
        }
        if (statement.includes("from diagnostics order by id desc")) {
            return [...this.diagnostics].sort((left, right) => right.id - left.id);
        }

        throw new Error(`unexpected query statement: ${sql}`);
    }

    async queryOne(sql, params = []) {
        const statement = sql.toLowerCase();
        if (statement.includes("from projects where slug")) {
            return this.projects.find((project) => project.slug === params[0]) ?? null;
        }
        if (statement.includes("from projects where id")) {
            return this.projects.find((project) => project.id === Number(params[0])) ?? null;
        }
        if (statement.includes("from apps where name")) {
            return this.apps.find((app) => app.name === params[0]) ?? null;
        }
        if (statement.includes("from apps where id")) {
            return this.apps.find((app) => app.id === Number(params[0])) ?? null;
        }
        if (statement.includes("from builds where commit_sha")) {
            return this.builds.find((build) => build.commit_sha === params[0]) ?? null;
        }
        if (statement.includes("from deployments where app_id")) {
            return this.deployments.find((deployment) => deployment.app_id === Number(params[0])) ?? null;
        }

        throw new Error(`unexpected queryOne statement: ${sql}`);
    }

    #insertSeed(collection, params, columns) {
        const id = Number(params[0]);
        if (collection.some((row) => row.id === id)) {
            return;
        }

        const row = {};
        for (const [index, column] of columns.entries()) {
            row[column] = column.endsWith("_id") || column === "id" ? Number(params[index]) : params[index];
        }
        collection.push(row);
    }
}

function compareById(left, right) {
    return left.id - right.id;
}

async function importRouteModulesFromExampleCopy() {
    const tempRoot = await mkdtemp(path.join(tmpdir(), "sloppy-prealpha-control-plane-"));
    const copiedExample = path.join(tempRoot, "prealpha-control-plane");
    await cp(exampleRoot, copiedExample, { recursive: true });

    const sloppyPackage = path.join(copiedExample, "node_modules", "sloppy");
    await mkdir(sloppyPackage, { recursive: true });
    await writeFile(path.join(sloppyPackage, "package.json"), JSON.stringify({
        type: "module",
        exports: {
            ".": "./index.js",
        },
    }, null, 2));
    await writeFile(
        path.join(sloppyPackage, "index.js"),
        `export * from ${JSON.stringify(pathToFileURL(path.join(repoRoot, "stdlib", "sloppy", "index.js")).href)};\n`,
    );

    const routesRoot = path.join(copiedExample, "src", "routes");
    const imports = await Promise.all([
        import(pathToFileURL(path.join(routesRoot, "health.js")).href),
        import(pathToFileURL(path.join(routesRoot, "projects.js")).href),
        import(pathToFileURL(path.join(routesRoot, "apps.js")).href),
        import(pathToFileURL(path.join(routesRoot, "builds.js")).href),
        import(pathToFileURL(path.join(routesRoot, "deployments.js")).href),
        import(pathToFileURL(path.join(routesRoot, "diagnostics.js")).href),
    ]);

    return {
        tempRoot,
        modules: [
            imports[0].healthModule,
            imports[1].projectsModule,
            imports[2].appsModule,
            imports[3].buildsModule,
            imports[4].deploymentsModule,
            imports[5].diagnosticsModule,
        ],
    };
}

function appWithProvider(app, db) {
    return new Proxy(app, {
        get(target, property, receiver) {
            if (property === "provider") {
                return (name) => {
                    assert.equal(name, "sqlite:main");
                    return db;
                };
            }
            return Reflect.get(target, property, receiver);
        },
    });
}

const imported = await importRouteModulesFromExampleCopy();
try {
    const builder = Sloppy.createBuilder();
    const sink = builder.logging.addMemorySink();
    let disposedScopes = 0;
    builder.services.addScoped("dogfood.request", () => ({
        dispose() {
            disposedScopes += 1;
        },
    }));

    const app = builder.build();
    const db = new FakeControlPlaneDb();
    app.use(ProblemDetails.defaults());
    app.use(RequestId.defaults({ generator: () => "req-dogfood-1" }));
    app.use(RequestLogging.defaults({ includeDuration: false }));
    app.use((ctx, next) => {
        ctx.services.get("dogfood.request");
        return next();
    });
    app.useCors({
        origins: ["https://console.example"],
        headers: ["content-type", "x-request-id"],
        exposedHeaders: ["x-request-id", "location"],
    });

    const moduleApp = appWithProvider(app, db);
    for (const register of imported.modules) {
        assert.equal(typeof register, "function");
        register(moduleApp);
    }

    app.get("/diagnostics/throw", () => {
        throw new Error("SECRET_DOGFOOD_TOKEN");
    }).withName("Diagnostics.Throw");

    const host = createTestHost(app);

    assert.deepEqual(await (await host.get("/health")).json(), {
        status: "healthy",
        checks: [
            { name: "sqlite", status: "healthy" },
            { name: "app-host", status: "healthy" },
        ],
    });
    assert.deepEqual(await (await host.get("/health/live")).json(), {
        status: "healthy",
        checks: [
            { name: "process", status: "healthy" },
        ],
    });
    assert.deepEqual(await (await host.get("/health/ready")).json(), {
        status: "healthy",
        checks: [
            { name: "sqlite", status: "healthy" },
        ],
    });

    const projects = await (await host.get("/projects?owner=runtime")).json();
    assert.deepEqual(projects, [
        { id: 1, slug: "compiler", name: "Compiler Platform", owner: "runtime" },
    ]);

    const createdProject = await host.post("/projects", {
        headers: { Origin: "https://console.example", "X-Request-ID": "ignored-client-id" },
        json: { slug: "observability", name: "Observability Console", owner: "ops" },
    });
    assert.equal(createdProject.status, 201);
    assert.equal(createdProject.headers.get("location"), "/projects/3");
    assert.equal(createdProject.headers.get("x-request-id"), "req-dogfood-1");
    assert.equal(createdProject.headers.get("access-control-allow-origin"), "https://console.example");
    assert.deepEqual(await createdProject.json(), {
        id: 3,
        slug: "observability",
        name: "Observability Console",
        owner: "ops",
    });

    assert.equal((await host.post("/projects", { json: { slug: "" } })).status, 400);
    assert.equal((await host.get("/projects/999")).status, 404);
    assert.equal((await host.post("/projects/1", { json: {} })).status, 405);

    const apps = await (await host.get("/apps")).json();
    assert.equal(apps.length, 2);
    assert.equal(apps[0].name, "compiler-api");

    const createdApp = await host.post("/apps", {
        json: { projectId: 1, name: "dogfood-api", environment: "Development" },
    });
    assert.equal(createdApp.status, 201);
    assert.deepEqual(await createdApp.json(), {
        id: 3,
        project_id: 1,
        name: "dogfood-api",
        environment: "Development",
    });
    assert.equal((await host.post("/apps", { json: { projectId: 1 } })).status, 400);
    assert.equal((await host.get("/apps/3")).status, 200);

    const createdBuild = await host.post("/apps/1/builds", {
        json: { commit: "feedface" },
    });
    assert.equal(createdBuild.status, 201);
    assert.deepEqual(await createdBuild.json(), {
        id: 2,
        app_id: 1,
        commit_sha: "feedface",
        status: "queued",
    });
    assert.equal((await host.post("/apps/1/builds", { json: {} })).status, 400);
    assert.equal((await (await host.get("/apps/1/builds")).json()).length, 2);

    const createdDeployment = await host.post("/apps/1/deployments", {
        json: { buildId: 2 },
    });
    assert.equal(createdDeployment.status, 201);
    assert.deepEqual(await createdDeployment.json(), {
        id: 1,
        app_id: 1,
        build_id: 2,
        status: "started",
    });
    assert.equal((await host.post("/apps/1/deployments", { json: {} })).status, 400);

    const diagnostics = await (await host.get("/diagnostics/recent")).json();
    assert.deepEqual(diagnostics, [
        { id: 1, level: "info", message: "dogfood app bootstrapped" },
    ]);

    const allowedPreflight = await host.options("/projects", {
        headers: {
            Origin: "https://console.example",
            "Access-Control-Request-Method": "POST",
            "Access-Control-Request-Headers": "content-type,x-request-id",
        },
    });
    assert.equal(allowedPreflight.status, 204);
    assert.equal(allowedPreflight.headers.get("access-control-allow-origin"), "https://console.example");
    assert.equal(allowedPreflight.headers.get("access-control-allow-methods"), "GET, POST");

    const deniedPreflight = await host.options("/projects", {
        headers: {
            Origin: "https://attacker.example",
            "Access-Control-Request-Method": "POST",
        },
    });
    assert.equal(deniedPreflight.status, 403);

    const problem = await host.get("/diagnostics/throw");
    assert.equal(problem.status, 500);
    const problemBody = await problem.json();
    assert.equal(problemBody.code, "SLOPPY_E_HANDLER_ERROR");
    assert.equal(JSON.stringify(problemBody).includes("SECRET_DOGFOOD_TOKEN"), false);
    assert.equal(JSON.stringify(sink.entries()).includes("SECRET_DOGFOOD_TOKEN"), false);

    assert.equal(disposedScopes > 0, true);
    await host.close();
    await assert.rejects(() => host.get("/health"), /closed/);
} finally {
    await rm(imported.tempRoot, { recursive: true, force: true });
}
