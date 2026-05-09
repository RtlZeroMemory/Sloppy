#!/usr/bin/env node
import fs from "node:fs";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(SCRIPT_DIR, "..", "..");

const SIZES = {
  tiny: { files: 1, routes: 2, schemas: 1, services: 1, controllers: 0 },
  small: { files: 5, routes: 20, schemas: 5, services: 5, controllers: 0 },
  medium: { files: 25, routes: 200, schemas: 50, services: 25, controllers: 10 },
  large: { files: 100, routes: 1000, schemas: 200, services: 100, controllers: 50 },
  huge: { files: 500, routes: 5000, schemas: 1000, services: 500, controllers: 250 },
};

function parseArgs(argv) {
  const allowed = new Set(["out", "size", "files", "routes", "schemas", "services", "controllers"]);
  const options = {};
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === "--help" || arg === "-h") {
      options.help = true;
      continue;
    }
    if (!arg.startsWith("--")) {
      throw new Error(`unexpected argument '${arg}'`);
    }
    const key = arg.slice(2);
    if (!allowed.has(key)) {
      throw new Error(`unknown option '${arg}'`);
    }
    const value = argv[index + 1];
    if (value === undefined || value.startsWith("--")) {
      throw new Error(`${arg} requires a value`);
    }
    options[key] = value;
    index += 1;
  }
  return options;
}

function assertSafeOutputDirectory(out) {
  const root = path.parse(out).root;
  const cwd = path.resolve(process.cwd());
  const relativeToRoot = path.relative(ROOT, out);
  if (out === root) {
    throw new Error("--out must not be a filesystem root");
  }
  if (out === cwd) {
    throw new Error("--out must not be the current working directory");
  }
  if (out === ROOT) {
    throw new Error("--out must not be the repository root");
  }
  if (
    path.isAbsolute(relativeToRoot) ||
    relativeToRoot === ".." ||
    relativeToRoot.startsWith(`..${path.sep}`)
  ) {
    return;
  }
  if (relativeToRoot !== "artifacts" && !relativeToRoot.startsWith(`artifacts${path.sep}`)) {
    throw new Error("--out inside the repository must be under artifacts/");
  }
}

function usage() {
  return `Usage:
  node tools/compiler/generate-scale-project.mjs --size small --out artifacts/compiler-scale/small
  node tools/compiler/generate-scale-project.mjs --files 100 --routes 1000 --schemas 200 --controllers 50 --services 100 --out artifacts/compiler-scale/custom
`;
}

function integerOption(options, name, fallback) {
  if (options[name] === undefined) {
    return fallback;
  }
  const value = Number(options[name]);
  if (!Number.isInteger(value) || value < 0) {
    throw new Error(`--${name} must be a non-negative integer`);
  }
  return value;
}

function pad(value, width = 4) {
  return String(value).padStart(width, "0");
}

function routeMethod(index) {
  return ["get", "post", "put", "patch", "delete"][index % 5];
}

function resultExpression(index) {
  switch (index % 5) {
    case 0:
      return `Results.ok({ route: ${index}, kind: "ok" })`;
    case 1:
      return `Results.created("/generated/${pad(index)}", { route: ${index}, kind: "created" })`;
    case 2:
      return `Results.status(202, { route: ${index}, accepted: true })`;
    case 3:
      return `Results.json({ route: ${index}, kind: "json" })`;
    default:
      return `Results.text("scale-${pad(index)}")`;
  }
}

function routeLine(index, receiver) {
  const method = routeMethod(index);
  const name = `Scale.Route.${pad(index)}`;
  const pattern =
    method === "get" || method === "delete"
      ? `/route-${pad(index)}/{id:int}`
      : `/route-${pad(index)}`;
  return `${receiver}.${method}("${pattern}", { name: "${name}", tags: ["scale", "${method}"] }, (ctx) => ${resultExpression(index)});`;
}

function schemaLines(count) {
  const lines = [];
  for (let index = 1; index <= count; index += 1) {
    lines.push(`type ScaleBody${pad(index)} = {`);
    lines.push("  name: string;");
    lines.push("  email?: string;");
    lines.push("  active?: boolean;");
    lines.push("};");
  }
  if (count === 0) {
    lines.push("type ScaleBody0001 = { name: string; };");
  }
  return lines;
}

function serviceLines(count) {
  const lines = [];
  lines.push("type ScaleService = { value: string; };");
  for (let index = 1; index <= count; index += 1) {
    lines.push(`app.services.addSingleton("ScaleService${pad(index)}", () => ({ value: "service-${pad(index)}" }));`);
  }
  return lines;
}

function controllerClassLines(count) {
  const lines = [];
  for (let index = 1; index <= count; index += 1) {
    lines.push(`class ScaleController${pad(index)} {`);
    lines.push(`  summary(ctx) { return Results.ok({ controller: "${pad(index)}", route: ctx.routeName }); }`);
    lines.push("}");
  }
  return lines;
}

function controllerMappingLines(count) {
  const lines = [];
  for (let index = 1; index <= count; index += 1) {
    lines.push(`app.mapController("/controllers/${pad(index)}", ScaleController${pad(index)}, (mapper) => {`);
    lines.push(`  mapper.get("/", "summary", { tags: ["controller"] }).withName("Scale.Controller.${pad(index)}");`);
    lines.push("});");
  }
  return lines;
}

function moduleFile(routeStart, routeCount, moduleIndex) {
  const lines = [
    'import { Results } from "sloppy";',
    "",
    `export function scaleRoutes${pad(moduleIndex)}(app) {`,
    `  const group = app.group("/generated/${pad(moduleIndex)}").withTags("generated", "file-${pad(moduleIndex)}");`,
  ];
  for (let offset = 0; offset < routeCount; offset += 1) {
    lines.push(`  ${routeLine(routeStart + offset, "group")}`);
  }
  lines.push("}");
  lines.push("");
  return lines.join("\n");
}

function mainFile(options, modules, mainRouteCount, controllerCount) {
  const lines = [
    "import {",
    "  Body,",
    "  Config,",
    "  Header,",
    "  Query,",
    "  RequestContext,",
    "  RequestId,",
    "  RequestLogging,",
    "  Results,",
    "  Route,",
    "  Service,",
    "  Sloppy,",
    "} from \"sloppy\";",
    'import { sqlite, Sqlite } from "sloppy/providers/sqlite";',
  ];
  for (const module of modules) {
    lines.push(`import { scaleRoutes${pad(module.index)} } from "./routes/route-${pad(module.index)}.ts";`);
  }
  lines.push("");
  lines.push(...schemaLines(options.schemas));
  lines.push("");
  lines.push(...controllerClassLines(controllerCount));
  lines.push("");
  lines.push("function scaleAudit(ctx, next) {");
  lines.push("  return next();");
  lines.push("}");
  lines.push("");
  lines.push("const app = Sloppy.create();");
  lines.push('app.use(sqlite("main", { database: ":memory:" }));');
  lines.push(...serviceLines(options.services));
  lines.push('const greeting = app.config.getString("Scale:Greeting", "hello");');
  lines.push("app.use(RequestId.defaults({ header: \"x-request-id\", responseHeader: true, trustIncoming: true }));");
  lines.push("app.use(RequestLogging.defaults({ includeRoute: true, includeDuration: false, includeRequestId: true }));");
  lines.push("app.use(scaleAudit);");
  lines.push("app.useCors({");
  lines.push('  origins: ["https://scale.example.com"],');
  lines.push('  methods: ["GET", "POST", "PUT", "PATCH", "DELETE"],');
  lines.push('  headers: ["content-type", "x-scale", "x-trace-id"],');
  lines.push('  exposedHeaders: ["x-request-id"],');
  lines.push("  credentials: true,");
  lines.push("  maxAgeSeconds: 600,");
  lines.push("});");
  lines.push("");
  lines.push("app.mapHealthChecks({");
  lines.push('  path: "/health",');
  lines.push('  livenessPath: "/health/live",');
  lines.push('  readinessPath: "/health/ready",');
  lines.push("  checks: [");
  lines.push('    { name: "compiler-scale", liveness: true, readiness: true, check: () => true },');
  lines.push("  ],");
  lines.push("});");
  lines.push("");
  if (mainRouteCount > 0) {
    lines.push("const direct = app.group(\"/direct\").withTags(\"direct\");");
    lines.push("direct.use((ctx, next) => next());");
    lines.push("direct.post(\"/typed/{id:int}\", { name: \"Scale.Typed\", tags: [\"typed\"] }, (");
    lines.push("  id: Route<number>,");
    lines.push("  q: Query<string>,");
    lines.push("  trace: Header<\"x-trace-id\">,");
    lines.push(`  body: Body<ScaleBody${pad(Math.max(1, Math.min(options.schemas, 1)))}>,`);
    lines.push("  greetingValue: Config<\"Scale:Greeting\">,");
    lines.push("  database: Sqlite<\"main\">,");
    lines.push("  service: Service<ScaleService>,");
    lines.push("  ctx: RequestContext,");
    lines.push(") => Results.ok({ id, q, trace, name: body.name, greetingValue, providerReady: database !== undefined, service: service.value, hasContext: ctx !== undefined }));");
    for (let index = 2; index <= mainRouteCount; index += 1) {
      lines.push(routeLine(index, "direct"));
    }
  }
  for (const module of modules) {
    lines.push(`app.useModule(scaleRoutes${pad(module.index)});`);
  }
  lines.push(...controllerMappingLines(controllerCount));
  lines.push("");
  lines.push("export default app;");
  lines.push("");
  return lines.join("\n");
}

function writeJson(file, value) {
  fs.writeFileSync(file, `${JSON.stringify(value, null, 2)}\n`);
}

function buildProject(options) {
  const out = path.resolve(options.out);
  assertSafeOutputDirectory(out);
  fs.rmSync(out, { recursive: true, force: true });
  fs.mkdirSync(path.join(out, "src", "routes"), { recursive: true });

  const controllerCount = Math.min(options.controllers, Math.max(0, options.routes - 1));
  const moduleFileCount = Math.max(0, options.files - 1);
  const mainRouteCount =
    moduleFileCount === 0
      ? Math.max(0, options.routes - controllerCount)
      : options.routes > controllerCount
        ? 1
        : 0;
  const moduleRouteCount = Math.max(0, options.routes - controllerCount - mainRouteCount);
  const modules = [];
  let nextRoute = mainRouteCount + 1;
  for (let index = 1; index <= moduleFileCount; index += 1) {
    const remainingFiles = moduleFileCount - index + 1;
    const remainingRoutes = moduleRouteCount - modules.reduce((sum, module) => sum + module.routes, 0);
    const count = remainingFiles === 0 ? 0 : Math.ceil(remainingRoutes / remainingFiles);
    modules.push({ index, routes: count, start: nextRoute });
    nextRoute += count;
  }

  for (const module of modules) {
    fs.writeFileSync(
      path.join(out, "src", "routes", `route-${pad(module.index)}.ts`),
      moduleFile(module.start, module.routes, module.index),
    );
  }
  fs.writeFileSync(path.join(out, "src", "main.ts"), mainFile(options, modules, mainRouteCount, controllerCount));
  writeJson(path.join(out, "sloppy.json"), {
    entry: "src/main.ts",
    outDir: ".sloppy",
    environment: "Development",
  });
  writeJson(path.join(out, "appsettings.json"), {
    Scale: { Greeting: "hello" },
    Sloppy: {
      Providers: {
        sqlite: {
          main: {
            database: ":memory:",
          },
        },
      },
    },
  });
  writeJson(path.join(out, "scale-project.json"), {
    schemaVersion: 1,
    size: options.sizeName ?? "custom",
    files: options.files,
    routes: options.routes,
    schemas: options.schemas,
    services: options.services,
    controllers: options.controllers,
    generatedRoutes: {
      direct: mainRouteCount,
      modules: moduleRouteCount,
      controllers: controllerCount,
    },
  });
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) {
    process.stdout.write(usage());
    return;
  }
  if (!args.out) {
    throw new Error("--out is required");
  }
  const sizeName = args.size ?? "small";
  const base = SIZES[sizeName];
  if (!base) {
    throw new Error(`--size must be one of: ${Object.keys(SIZES).join(", ")}`);
  }
  const options = {
    sizeName,
    out: args.out,
    files: integerOption(args, "files", base.files),
    routes: integerOption(args, "routes", base.routes),
    schemas: integerOption(args, "schemas", base.schemas),
    services: integerOption(args, "services", base.services),
    controllers: integerOption(args, "controllers", base.controllers),
  };
  if (options.files < 1) {
    throw new Error("--files must be at least 1");
  }
  buildProject(options);
}

try {
  main();
} catch (error) {
  process.stderr.write(`generate-scale-project: ${error.message}\n`);
  process.exit(2);
}
