#[test]
fn typed_framework_metadata_fixture_expected_outputs_stay_current() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    let fixture_name = "framework-metadata";
    let fixture = root
        .join("tests/fixtures")
        .join(fixture_name)
        .join("input.ts");
    let source = fs::read_to_string(&fixture).expect("fixture input should exist");
    let mut app = extract(&fixture, &source).expect("framework fixture should extract");
    super::ConfigurationModel::load(&fixture, &CompileOptions::new(), &app.config_reads)
        .expect("fixture configuration should load")
        .apply_to_app(&mut app)
        .expect("fixture configuration should apply");

    let emitted_js = super::emit_app_js(&app);
    let expected_js = fs::read_to_string(
        root.join("tests/fixtures")
            .join(fixture_name)
            .join("expected/app.js"),
    )
    .expect("expected app.js should exist");
    assert_eq!(emitted_js.source, expected_js, "{fixture_name} app.js");

    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let emitted_plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let expected_plan = fs::read_to_string(
        root.join("tests/fixtures")
            .join(fixture_name)
            .join("expected/app.plan.json"),
    )
    .expect("expected app.plan.json should exist");
    assert_eq!(emitted_plan, expected_plan, "{fixture_name} app.plan.json");

    let expected_source_map = fs::read_to_string(
        root.join("tests/fixtures")
            .join(fixture_name)
            .join("expected/app.js.map"),
    )
    .expect("expected app.js.map should exist");
    assert_eq!(
        emitted_source_map, expected_source_map,
        "{fixture_name} app.js.map"
    );
}

#[test]
fn app_graph_dogfood_fixture_expected_outputs_stay_current() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    for fixture_name in ["app-graph-dogfood", "full-framework-app-graph"] {
        let fixture = root
            .join("tests/fixtures")
            .join(fixture_name)
            .join("input.ts");
        let source = fs::read_to_string(&fixture).expect("fixture input should exist");
        let mut app = extract(&fixture, &source).expect("app graph dogfood fixture should extract");
        super::ConfigurationModel::load(&fixture, &CompileOptions::new(), &app.config_reads)
            .expect("fixture configuration should load")
            .apply_to_app(&mut app)
            .expect("fixture configuration should apply");

        let emitted_js = super::emit_app_js(&app);
        let expected_js = fs::read_to_string(
            root.join("tests/fixtures")
                .join(fixture_name)
                .join("expected/app.js"),
        )
        .expect("expected app.js should exist");
        assert_eq!(emitted_js.source, expected_js, "{fixture_name} app.js");

        let emitted_source_map = super::emit_source_map(&app, &emitted_js);
        let emitted_plan = super::emit_plan(
            &app,
            &super::sha256_hex(&emitted_js.source),
            &super::sha256_hex(&emitted_source_map),
        )
        .expect("plan should emit");
        let expected_plan = fs::read_to_string(
            root.join("tests/fixtures")
                .join(fixture_name)
                .join("expected/app.plan.json"),
        )
        .expect("expected app.plan.json should exist");
        assert_eq!(emitted_plan, expected_plan, "{fixture_name} app.plan.json");

        let expected_source_map = fs::read_to_string(
            root.join("tests/fixtures")
                .join(fixture_name)
                .join("expected/app.js.map"),
        )
        .expect("expected app.js.map should exist");
        assert_eq!(
            emitted_source_map, expected_source_map,
            "{fixture_name} app.js.map"
        );
    }
}

#[test]
fn typed_framework_negative_diagnostics_are_source_aware() {
    for (source, code) in [
        (
            r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/users", (id: number) => Results.ok({ id }));
export default app;
"#,
            "SLOPPYC_E_AMBIGUOUS_BINDING",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
type BodyA = { name: string };
type BodyB = { name: string };
const app = Sloppy.create();
app.post("/users", (a: BodyA, b: BodyB) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_MULTIPLE_BODY_BINDINGS",
        ),
        (
            r#"import { Sloppy, Results, Header } from "sloppy";
const app = Sloppy.create();
app.get("/users", (trace: Header<string>) => Results.ok({ trace }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_HEADER_BINDING",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
import { Postgres } from "sloppy/providers/postgres";
const app = Sloppy.create();
app.get("/users", (db: Postgres<string>) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_DYNAMIC_PROVIDER_NAME",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/users", (db: Mongo<"main">) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNKNOWN_INJECTION_MARKER",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.post("/users", (input: MissingModel) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNRESOLVED_TYPE",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
type Body = { child: MissingModel };
const app = Sloppy.create();
app.post("/users", (input: Body) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNRESOLVED_TYPE",
        ),
        (
            r#"import { Sloppy, Results, Body } from "sloppy";
const app = Sloppy.create();
app.post("/users", (input: Body<MissingModel>) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNRESOLVED_TYPE",
        ),
        (
            r#"import { Sloppy, Results, Body } from "sloppy";
type BodyA = { name: string };
type BodyB = { note: string };
const app = Sloppy.create();
app.post("/users", (a: Body<BodyA>, b: Body<BodyB>) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_MULTIPLE_BODY_BINDINGS",
        ),
        (
            r#"import { Sloppy, Results, Body } from "sloppy";
type BodyA = { name: string };
type BodyB = { note: string };
const app = Sloppy.create();
app.post("/users", (a: Body<BodyA>, b: BodyB) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_MULTIPLE_BODY_BINDINGS",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
type UserCreate = { name: string };
const app = Sloppy.create();
app.post("/users", (input: Paginated<UserCreate>) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNRESOLVED_TYPE",
        ),
        (
            r#"import { Sloppy, Results, Route } from "sloppy";
const app = Sloppy.create();
app.get("/users/:id", (routeId: Route<number>) => Results.ok({ routeId }));
export default app;
"#,
            "SLOPPYC_E_ROUTE_BINDING_MISMATCH",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
type Maybe<T> = T extends string ? string : number;
const app = Sloppy.create();
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_SCHEMA",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
type Node = { next: Node };
const app = Sloppy.create();
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_SCHEMA",
        ),
    ] {
        let diagnostic = extract(std::path::Path::new("app.ts"), source)
            .expect_err("unsupported framework source should fail");
        assert_eq!(diagnostic.code, code);
        assert!(
            diagnostic.span.is_some(),
            "{code} should include a source span"
        );
    }
}

#[test]
fn typed_framework_service_wrapper_emits_service_metadata() {
    let source = r#"import { Sloppy, Results, Service } from "sloppy";
const app = Sloppy.create();
app.get("/users", (users: Service<UserService>) => Results.ok({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("explicit service wrapper should extract metadata");
    let binding = app.routes[0]
        .handler
        .bindings
        .iter()
        .find(|binding| binding.parameter.as_deref() == Some("users"))
        .expect("service wrapper binding should be present");
    assert_eq!(binding.kind, "injection");
    assert_eq!(binding.wrapper.as_deref(), Some("Service"));
    assert_eq!(binding.injection_kind.as_deref(), Some("service"));
    assert_eq!(binding.name.as_deref(), Some("UserService"));
}

#[test]
fn framework_service_registration_allows_emitted_helper_captures() {
    let source = r#"import { Sloppy, Results } from "sloppy";
function makeGreeting() {
  return { prefix: "hello" };
}
const app = Sloppy.create();
app.services.addScoped("GreetingService", () => makeGreeting());
app.get("/users", () => Results.ok({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("captured emitted helper should be available to service factories");
    assert_eq!(app.service_registrations.len(), 1);
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("function makeGreeting()"));
    assert!(emitted_js.source.contains(
        "__sloppy_framework_services.addScoped(\"GreetingService\", () => makeGreeting());"
    ));
}

#[test]
fn framework_service_registration_rejects_unemitted_app_captures() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.services.addSingleton("GreetingService", () => ({
  greeting: app.config.getString("App:Greeting", "hello")
}));
app.get("/users", () => Results.ok({ ok: true }));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.ts"), source)
        .expect_err("unemitted app capture would produce a broken generated bundle");
    assert_eq!(
        diagnostic.code,
        "SLOPPYC_E_UNSUPPORTED_SERVICE_REGISTRATION"
    );
    assert!(diagnostic.message.contains("app"));
}

#[test]
fn builder_service_registration_extracts_service_factory() {
    let source = r#"import { Sloppy, Results, Service } from "sloppy";
type GreetingService = { greeting: string };
const builder = Sloppy.createBuilder();
builder.services.addSingleton("GreetingService", () => ({ greeting: "hello" }));
const app = builder.build();
app.get("/users", (service: Service<GreetingService>) => Results.ok({ service }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("builder service registrations should extract");
    assert_eq!(app.service_registrations.len(), 1);
    assert_eq!(app.service_registrations[0].token, "GreetingService");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
        "__sloppy_framework_services.addSingleton(\"GreetingService\", () => ({ greeting: \"hello\" }));"
    ));
}

#[test]
fn typed_config_injection_uses_plan_default_when_environment_is_absent() {
    let source = r#"import { Sloppy, Results, Config } from "sloppy";
const app = Sloppy.create();
const requiredGreeting = app.config.getString("App:Greeting");
const greeting = app.config.getString("App:Greeting", "hello");
app.get("/", (message: Config<"App:Greeting">) => Results.ok({ message }));
export default app;
"#;
    let app =
        extract(std::path::Path::new("app.ts"), source).expect("config default should extract");
    assert!(app
        .config_reads
        .iter()
        .any(|read| read.key == "App:Greeting"
            && read.default_value == Some(serde_json::json!("hello"))));
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
        "const __sloppy_framework_config_defaults = new Map([[\"App:Greeting\", \"hello\"]]);"
    ));
    assert!(emitted_js.source.contains(
        "if (__sloppy_framework_config_defaults.has(binding.name)) { return __sloppy_framework_config_defaults.get(binding.name); }"
    ));
}

#[test]
fn static_framework_surfaces_are_lowered_into_generated_handlers() {
    let source = r#"import { Sloppy, Results, RequestId, RequestLogging } from "sloppy";
class UsersController {
  static inject = ["Repo"];
  constructor(repo) {
    this.repo = repo;
  }
  list(ctx) {
    return Results.ok({ users: this.repo.list(), requestId: ctx.requestId });
  }
}
const builder = Sloppy.createBuilder();
builder.services.addSingleton("Repo", () => ({ list: () => ["ada"] }));
const app = builder.build();
function requireAuth(ctx, next) {
  if (ctx.request.headers.get("authorization") !== "Bearer test") {
    return Results.status(401);
  }
  return next();
}
app.use(RequestId.defaults({ header: "x-request-id", responseHeader: true, trustIncoming: true }));
app.use(RequestLogging.defaults({ includeRoute: true, includeDuration: false, includeRequestId: true }));
app.use(requireAuth);
app.useCors({
  origins: ["https://app.example.com"],
  methods: ["GET"],
  headers: ["authorization"],
  exposedHeaders: ["x-request-id"],
  credentials: true,
  maxAgeSeconds: 600,
});
const api = app.group("/api");
api.use((ctx, next) => next());
api.get("/status", () => Results.ok({ ok: true })).withName("Status");
app.mapController("/users", UsersController, (mapper) => {
  mapper.get("/", "list", { tags: ["Users"] }).requireAuth({ role: "admin" }).withName("Users.List");
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("static framework surfaces should extract");
    assert_eq!(
        app.routes.len(),
        4,
        "two GET routes plus two CORS preflight routes"
    );
    assert!(app
        .routes
        .iter()
        .any(|route| route.method == "OPTIONS" && route.pattern == "/api/status"));
    assert!(app
        .routes
        .iter()
        .any(|route| route.method == "OPTIONS" && route.pattern == "/users"));
    let status = app
        .routes
        .iter()
        .find(|route| route.method == "GET" && route.pattern == "/api/status")
        .expect("status route should exist");
    assert_eq!(status.middleware.len(), 4);
    assert!(status.cors.is_some());
    let controller = app
        .routes
        .iter()
        .find(|route| route.name.as_deref() == Some("Users.List"))
        .expect("controller route should exist");
    assert_eq!(controller.tags, vec!["Users"]);
    let controller_auth = controller
        .auth
        .as_ref()
        .expect("controller route auth should extract");
    assert_eq!(controller_auth.roles, vec!["admin"]);
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("__sloppy_run_middleware"));
    assert!(emitted_js.source.contains("__sloppy_require_auth"));
    assert!(emitted_js.source.contains("__sloppy_cors_preflight"));
    assert!(emitted_js.source.contains("__sloppy_request_id"));
    assert!(emitted_js.source.contains("__sloppy_request_logging"));
    assert!(emitted_js.source.contains("new UsersController"));
}

#[test]
fn static_rate_limit_route_metadata_emits_plan_contract() {
    let source = r#"import { Sloppy, Results, RateLimit } from "sloppy";
const app = Sloppy.create();
app.get("/search", () => Results.text("ok"))
  .rateLimit(RateLimit.fixedWindow({
    name: "search-ip",
    limit: 10,
    windowMs: 60000,
    store: "default",
    partitionBy: RateLimit.partition.ip()
  }));
app.get("/me", () => Results.ok({ ok: true }))
  .rateLimit(RateLimit.tokenBucket({
    name: "me-user",
    capacity: 5,
    refillPerSecond: 1,
    partitionBy: RateLimit.partition.user()
  }));
app.get("/login", () => Results.text("ok"))
  .rateLimit(RateLimit.slidingWindow({
    name: "login-ip",
    limit: 5,
    windowMs: 60000,
    partitionBy: RateLimit.partition.ip()
  }));
app.get("/export", () => Results.text("ok"))
  .rateLimit(RateLimit.concurrency({
    name: "export-user",
    limit: 1,
    partitionBy: RateLimit.partition.user()
  }));
app.get("/dynamic", () => Results.text("ok"))
  .rateLimit(createPolicy());
const dynamic = createPolicy();
app.get("/dynamic-identifier", () => Results.text("ok"))
  .rateLimit(dynamic);
app.get("/dynamic-extra", () => Results.text("ok"))
  .rateLimit(dynamic, { ignoredByRuntime: true });
app.get("/static-extra", () => Results.text("ok"))
  .rateLimit(RateLimit.fixedWindow({
    name: "extra-ip",
    limit: 10,
    windowMs: 60000,
    partitionBy: RateLimit.partition.ip()
  }), { ignoredByRuntime: true });
app.get("/unknown-algorithm", () => Results.text("ok"))
  .rateLimit(RateLimit.customWindow({
    name: "custom",
    limit: 10
  }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("rate-limit route metadata should extract");
    let search = app
        .routes
        .iter()
        .find(|route| route.pattern == "/search")
        .expect("search route should exist");
    assert_eq!(search.rate_limits.len(), 1);
    assert_eq!(search.rate_limits[0].name.as_deref(), Some("search-ip"));
    assert_eq!(search.rate_limits[0].algorithm, "fixedWindow");
    assert_eq!(search.rate_limits[0].store.as_deref(), Some("default"));
    assert_eq!(search.rate_limits[0].partition.as_deref(), Some("ip"));
    assert!(!search.rate_limits[0].partial);
    let me = app
        .routes
        .iter()
        .find(|route| route.pattern == "/me")
        .expect("me route should exist");
    assert_eq!(me.rate_limits[0].algorithm, "tokenBucket");
    assert_eq!(me.rate_limits[0].partition.as_deref(), Some("user"));
    assert!(!me.rate_limits[0].partial);
    let login = app
        .routes
        .iter()
        .find(|route| route.pattern == "/login")
        .expect("login route should exist");
    assert_eq!(login.rate_limits[0].algorithm, "slidingWindow");
    assert_eq!(login.rate_limits[0].partition.as_deref(), Some("ip"));
    assert!(!login.rate_limits[0].partial);
    let export = app
        .routes
        .iter()
        .find(|route| route.pattern == "/export")
        .expect("export route should exist");
    assert_eq!(export.rate_limits[0].algorithm, "concurrency");
    assert_eq!(export.rate_limits[0].partition.as_deref(), Some("user"));
    assert!(!export.rate_limits[0].partial);
    let dynamic = app
        .routes
        .iter()
        .find(|route| route.pattern == "/dynamic")
        .expect("dynamic route should exist");
    assert_eq!(dynamic.rate_limits[0].algorithm, "dynamic");
    assert!(dynamic.rate_limits[0].partial);
    let dynamic_identifier = app
        .routes
        .iter()
        .find(|route| route.pattern == "/dynamic-identifier")
        .expect("dynamic identifier route should exist");
    assert_eq!(dynamic_identifier.rate_limits[0].algorithm, "dynamic");
    assert!(dynamic_identifier.rate_limits[0].partial);
    let dynamic_extra = app
        .routes
        .iter()
        .find(|route| route.pattern == "/dynamic-extra")
        .expect("dynamic extra route should exist");
    assert_eq!(dynamic_extra.rate_limits[0].algorithm, "dynamic");
    assert!(dynamic_extra.rate_limits[0].partial);
    let static_extra = app
        .routes
        .iter()
        .find(|route| route.pattern == "/static-extra")
        .expect("static extra route should exist");
    assert_eq!(static_extra.rate_limits[0].algorithm, "fixedWindow");
    assert_eq!(
        static_extra.rate_limits[0].name.as_deref(),
        Some("extra-ip")
    );
    assert!(static_extra.rate_limits[0].partial);
    let unknown_algorithm = app
        .routes
        .iter()
        .find(|route| route.pattern == "/unknown-algorithm")
        .expect("unknown algorithm route should exist");
    assert_eq!(unknown_algorithm.rate_limits[0].algorithm, "dynamic");
    assert!(unknown_algorithm.rate_limits[0].partial);

    let plan = super::emit_plan(&app, "bundle-hash", "map-hash")
        .expect("plan should emit rate-limit route metadata");
    let plan: serde_json::Value =
        serde_json::from_str(&plan).expect("plan output should be valid JSON");
    let routes = plan["routes"]
        .as_array()
        .expect("plan routes should be an array");
    let search = routes
        .iter()
        .find(|route| route["pattern"] == "/search")
        .expect("search route should be emitted");
    assert_eq!(
        search["rateLimit"],
        serde_json::json!([{
            "name": "search-ip",
            "algorithm": "fixedWindow",
            "store": "default",
            "partition": "ip",
            "partial": false
        }])
    );
    let dynamic = routes
        .iter()
        .find(|route| route["pattern"] == "/dynamic")
        .expect("dynamic route should be emitted");
    assert_eq!(
        dynamic["rateLimit"][0]["algorithm"],
        serde_json::json!("dynamic")
    );
    assert_eq!(dynamic["rateLimit"][0]["partial"], serde_json::json!(true));
    let static_extra = routes
        .iter()
        .find(|route| route["pattern"] == "/static-extra")
        .expect("static-extra route should be emitted");
    assert_eq!(
        static_extra["rateLimit"][0],
        serde_json::json!({
            "name": "extra-ip",
            "algorithm": "fixedWindow",
            "store": null,
            "partition": "ip",
            "partial": true
        })
    );
    let unknown_algorithm = routes
        .iter()
        .find(|route| route["pattern"] == "/unknown-algorithm")
        .expect("unknown algorithm route should be emitted");
    assert_eq!(
        unknown_algorithm["rateLimit"][0]["algorithm"],
        serde_json::json!("dynamic")
    );
    assert_eq!(
        unknown_algorithm["rateLimit"][0]["partial"],
        serde_json::json!(true)
    );
}

#[test]
fn static_auth_config_metadata_fails_closed() {
    for source in [
        r#"import { Sloppy, Results, Auth } from "sloppy";
const app = Sloppy.create();
app.use(Auth.jwtBearer({ secret: "literal-secret" }));
app.get("/", () => Results.ok({ ok: true })).requireAuth();
export default app;
"#,
        r#"import { Sloppy, Results, Auth } from "sloppy";
const app = Sloppy.create();
app.use(Auth.apiKey({ validate: (key) => key === "literal-secret" }));
app.get("/", () => Results.ok({ ok: true })).requireAuth();
export default app;
"#,
        r#"import { Sloppy, Results, Auth, Config } from "sloppy";
const app = Sloppy.create();
app.use(Auth.apiKey({ validate: (key) => key === Config.required("Auth:ApiKey") || key === Config.required("Auth:BackupKey") }));
app.get("/", () => Results.ok({ ok: true })).requireAuth();
export default app;
"#,
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const requiredDepartment = "ops";
app.auth.addPolicy("ops", (user) => user.claims.department === requiredDepartment);
app.get("/", () => Results.ok({ ok: true })).requireAuth({ policy: "ops" });
export default app;
"#,
        r#"import { Sloppy, Results, Auth } from "sloppy";
const app = Sloppy.create();
app.auth.addPolicy("ops", Auth.policyExtra((user) => user.claims.department === "ops"));
app.get("/", () => Results.ok({ ok: true })).requireAuth({ policy: "ops" });
export default app;
"#,
        r#"import { Sloppy, Results, Auth, Config } from "sloppy";
const app = Sloppy.create();
const store = Math.random() > 0 ? Auth.sessionStore.memory() : undefined;
app.use(Auth.cookieSession({ secret: Config.required("Auth:SessionSecret"), store }));
app.get("/", () => Results.ok({ ok: true })).requireAuth();
export default app;
"#,
        r#"import { Sloppy, Results, Auth, Config } from "sloppy";
const app = Sloppy.create();
app.use(Auth.cookieSession({ secret: Config.required("Auth:SessionSecret"), secure: false, csrf: true }));
app.get("/", () => Results.ok({ ok: true })).requireAuth();
export default app;
"#,
        r#"import { Sloppy, Results, Auth, Config } from "sloppy";
const app = Sloppy.create();
app.use(Auth.cookieSession({ secret: Config.required("Auth:SessionSecret"), path: "/app", csrf: true }));
app.get("/", () => Results.ok({ ok: true })).requireAuth();
export default app;
"#,
        r#"import { Sloppy, Results, Auth, Config } from "sloppy";
const app = Sloppy.create();
const sameSite = "strict";
app.use(Auth.cookieSession({ secret: Config.required("Auth:SessionSecret"), sameSite }));
app.get("/", () => Results.ok({ ok: true })).requireAuth();
export default app;
"#,
        r#"import { Sloppy, Results, Auth, Config } from "sloppy";
const app = Sloppy.create();
const ttl = 60;
app.use(Auth.cookieSession({ secret: Config.required("Auth:SessionSecret"), maxAgeSeconds: ttl }));
app.get("/", () => Results.ok({ ok: true })).requireAuth();
export default app;
"#,
        r#"import { Sloppy, Results, Auth, Config } from "sloppy";
const app = Sloppy.create();
const ttl = 60;
app.use(Auth.cookieSession({ secret: Config.required("Auth:SessionSecret"), maxAge: ttl }));
app.get("/", () => Results.ok({ ok: true })).requireAuth();
export default app;
"#,
    ] {
        let diagnostic = extract(std::path::Path::new("app.ts"), source)
            .expect_err("unsupported static auth metadata should fail closed");
        assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_AUTH");
    }
}

#[test]
fn static_cookie_session_auth_metadata_extracts_without_secret_values() {
    let source = r#"import { Sloppy, Results, Auth, Config } from "sloppy";
const app = Sloppy.create();
app.use(Auth.cookieSession({
  name: "sloppy.session",
  secret: Config.required("Auth:SessionSecret"),
  secure: true,
  httpOnly: true,
  sameSite: "lax",
  store: Auth.sessionStore.memory(),
  idleTimeoutMs: 30000,
  absoluteTimeoutMs: 60000,
  rotation: true,
  csrf: true
}));
app.get("/me", (ctx) => Results.ok({ subject: ctx.user.sub })).requireAuth();
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("cookie session auth metadata should extract");
    assert_eq!(app.auth.schemes.len(), 1);
    match &app.auth.schemes[0] {
        super::AuthSchemeMetadata::CookieSession {
            name,
            cookie,
            secure,
            http_only,
            same_site,
            path,
            max_age_seconds,
            store,
            idle_timeout_ms,
            absolute_timeout_ms,
            rotation,
            csrf,
            secret_config_key,
        } => {
            assert_eq!(name, "cookieSessionAuth");
            assert_eq!(cookie, "sloppy.session");
            assert!(*secure);
            assert!(*http_only);
            assert_eq!(same_site, "lax");
            assert_eq!(path, "/");
            assert_eq!(*max_age_seconds, None);
            assert_eq!(store.as_deref(), Some("memory"));
            assert_eq!(*idle_timeout_ms, Some(30000));
            assert_eq!(*absolute_timeout_ms, Some(60000));
            assert!(*rotation);
            assert!(*csrf);
            assert_eq!(secret_config_key.as_deref(), Some("Auth:SessionSecret"));
        }
        other => panic!("expected cookie session scheme, got {other:?}"),
    }
    let plan = super::emit_plan(&app, "bundle-hash", "map-hash")
        .expect("plan should emit cookie auth metadata");
    assert!(plan.contains("\"kind\": \"cookieSession\""));
    assert!(plan.contains("\"cookie\": \"sloppy.session\""));
    assert!(plan.contains("\"store\": \"memory\""));
    assert!(plan.contains("\"maxAgeSeconds\": null"));
    assert!(plan.contains("\"idleTimeoutMs\": 30000"));
    assert!(plan.contains("\"absoluteTimeoutMs\": 60000"));
    assert!(plan.contains("\"rotation\": true"));
    assert!(plan.contains("\"csrf\": true"));
    assert!(plan.contains("\"configKey\": \"Auth:SessionSecret\""));
    assert!(plan.contains("\"secret\": \"<redacted>\""));
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("__sloppy_auth_session"));
    assert!(emitted_js.source.contains("cookieSession"));
    assert!(emitted_js
        .source
        .contains("\"secretEnvKey\":\"Auth__SessionSecret\""));
    assert!(emitted_js.source.contains("\"csrf\":true"));
    assert!(emitted_js.source.contains("x-csrf-token"));
    assert!(emitted_js.source.contains("__sloppy_auth_csrf_failure"));
    assert!(emitted_js.source.contains("__sloppy_auth_memory_sessions"));
    assert!(emitted_js
        .source
        .contains("__sloppy_auth_memory_sessions.delete"));
    assert!(emitted_js.source.contains("__sloppy_auth_rotate_session"));
    assert!(emitted_js.source.contains("previous.expiresAt"));
    assert!(emitted_js
        .source
        .contains("return await __sloppy_auth_rotate_session(ctx, await terminal());"));
}

#[test]
fn cookie_session_csrf_boolean_metadata_and_object_runtime_form_compile() {
    for (source, expected_csrf) in [
        (
            r#"import { Sloppy, Results, Auth, Config } from "sloppy";
const app = Sloppy.create();
app.use(Auth.cookieSession({ secret: Config.required("Auth:SessionSecret"), csrf: true }));
app.get("/", () => Results.ok({ ok: true })).requireAuth();
export default app;
"#,
            true,
        ),
        (
            r#"import { Sloppy, Results, Auth, Config } from "sloppy";
const app = Sloppy.create();
app.use(Auth.cookieSession({ secret: Config.required("Auth:SessionSecret"), csrf: false }));
app.get("/", () => Results.ok({ ok: true })).requireAuth();
export default app;
"#,
            false,
        ),
    ] {
        let app = extract(std::path::Path::new("app.ts"), source)
            .expect("boolean csrf should be compiler-visible");
        match &app.auth.schemes[0] {
            super::AuthSchemeMetadata::CookieSession { csrf, .. } => {
                assert_eq!(*csrf, expected_csrf);
            }
            other => panic!("expected cookie session scheme, got {other:?}"),
        }
    }

    let object_source = r#"import { Sloppy, Results, Auth, Config } from "sloppy";
const app = Sloppy.create();
app.use(Auth.cookieSession({
  secret: Config.required("Auth:SessionSecret"),
  csrf: { header: "x-app-csrf", cookieName: "app_csrf" }
}));
app.get("/", () => Results.ok({ ok: true })).requireAuth();
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), object_source)
        .expect("runtime-valid object csrf should compile with partial metadata");
    match &app.auth.schemes[0] {
        super::AuthSchemeMetadata::CookieSession { csrf, .. } => {
            assert!(!*csrf);
        }
        other => panic!("expected cookie session scheme, got {other:?}"),
    }
}

#[test]
fn generated_memory_session_checks_csrf_before_refreshing_idle_timeout() {
    let source = r#"import { Sloppy, Results, Auth, Config } from "sloppy";
const app = Sloppy.create();
app.use(Auth.cookieSession({
  secret: Config.required("Auth:SessionSecret"),
  store: Auth.sessionStore.memory(),
  idleTimeoutMs: 1000,
  csrf: true
}));
app.post("/unsafe", () => Results.ok({ ok: true })).requireAuth();
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("memory csrf app should extract");
    let emitted = super::emit_app_js(&app).source;
    let csrf_check = emitted
        .find("if (!__sloppy_auth_check_csrf(ctx, scheme, record.csrf))")
        .expect("generated auth should check csrf");
    let last_seen = emitted
        .find("record.lastSeenAt = nowMs")
        .expect("generated auth should refresh lastSeenAt");
    let idle_refresh = emitted
        .find("record.idleExpiresAt = nowMs + scheme.idleTimeoutMs")
        .expect("generated auth should refresh idle timeout");
    assert!(csrf_check < last_seen);
    assert!(last_seen < idle_refresh);
}

#[test]
fn static_auth_route_aliases_emit_schemes_scopes_and_anonymous_metadata() {
    let source = r#"import { Sloppy, Results, Auth, Config } from "sloppy";
const app = Sloppy.create();
app.use(Auth.jwtBearer({ secret: Config.required("Auth:JwtSecret") }));
app.auth.addPolicy("ops", (user) => true);
app.get("/me", () => Results.ok({ ok: true }))
  .requiresAuth("bearerAuth")
  .requiresScope("users:read")
  .requiresRole("admin")
  .authorize("ops");
app.get("/reprotected", () => Results.ok({ ok: true }))
  .allowAnonymous()
  .requiresScope("users:read");
app.get("/anonymous-after-scope", () => Results.ok({ ok: true }))
  .requiresScope("users:read")
  .allowAnonymous();
app.get("/require-after-scope", () => Results.ok({ ok: true }))
  .requiresScope("lost:scope")
  .requiresAuth("bearerAuth");
const group = app.group("/group").requireAuth();
group.allowAnonymous();
group.get("/open", () => Results.ok({ ok: true }));
app.get("/public", () => Results.ok({ ok: true })).allowAnonymous();
export default app;
"#;
    let app =
        extract(std::path::Path::new("app.ts"), source).expect("auth route aliases should extract");
    let protected = app
        .routes
        .iter()
        .find(|route| route.pattern == "/me")
        .expect("protected route should exist")
        .auth
        .as_ref()
        .expect("protected route auth should extract");
    assert!(protected.required);
    assert_eq!(protected.schemes, vec!["bearerAuth"]);
    assert_eq!(protected.scopes, vec!["users:read"]);
    assert_eq!(protected.roles, vec!["admin"]);
    assert_eq!(protected.policy.as_deref(), Some("ops"));
    let reprotected = app
        .routes
        .iter()
        .find(|route| route.pattern == "/reprotected")
        .expect("reprotected route should exist")
        .auth
        .as_ref()
        .expect("reprotected route auth should extract");
    assert!(reprotected.required);
    assert!(!reprotected.allow_anonymous);
    assert_eq!(reprotected.scopes, vec!["users:read"]);
    let anonymous_after_scope = app
        .routes
        .iter()
        .find(|route| route.pattern == "/anonymous-after-scope")
        .expect("anonymous-after-scope route should exist")
        .auth
        .as_ref()
        .expect("anonymous-after-scope route auth should extract");
    assert!(!anonymous_after_scope.required);
    assert!(anonymous_after_scope.allow_anonymous);
    assert!(anonymous_after_scope.scopes.is_empty());
    let require_after_scope = app
        .routes
        .iter()
        .find(|route| route.pattern == "/require-after-scope")
        .expect("require-after-scope route should exist")
        .auth
        .as_ref()
        .expect("require-after-scope route auth should extract");
    assert!(require_after_scope.required);
    assert_eq!(require_after_scope.schemes, vec!["bearerAuth"]);
    assert!(require_after_scope.scopes.is_empty());
    let group_open = app
        .routes
        .iter()
        .find(|route| route.pattern == "/group/open")
        .expect("group route should exist")
        .auth
        .as_ref()
        .expect("group route auth should extract");
    assert!(!group_open.required);
    assert!(group_open.allow_anonymous);
    let public = app
        .routes
        .iter()
        .find(|route| route.pattern == "/public")
        .expect("public route should exist")
        .auth
        .as_ref()
        .expect("anonymous route auth should extract");
    assert!(!public.required);
    assert!(public.allow_anonymous);
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("\"secretEnvKey\":\"Auth__JwtSecret\""));
    assert!(emitted_js.source.contains("__sloppy_auth_config_value"));
    let plan = super::emit_plan(&app, "bundle-hash", "map-hash")
        .expect("plan should emit auth route metadata");
    let plan: serde_json::Value =
        serde_json::from_str(&plan).expect("plan output should be valid JSON");
    let routes = plan["routes"]
        .as_array()
        .expect("plan routes should be an array");
    let protected = routes
        .iter()
        .find(|route| route["pattern"] == "/me")
        .expect("protected route should be emitted");
    assert_eq!(
        protected["dispatch"]["executionKind"],
        serde_json::json!("v8-handler")
    );
    assert!(protected["response"].get("nativeBody").is_none());
    assert_eq!(
        protected["bindings"][0]["kind"],
        serde_json::json!("context")
    );
    assert_eq!(
        protected["auth"]["schemes"],
        serde_json::json!(["bearerAuth"])
    );
    assert_eq!(
        protected["auth"]["scopes"],
        serde_json::json!(["users:read"])
    );
    assert_eq!(protected["auth"]["roles"], serde_json::json!(["admin"]));
    assert_eq!(protected["auth"]["policy"], serde_json::json!("ops"));
    let reprotected = routes
        .iter()
        .find(|route| route["pattern"] == "/reprotected")
        .expect("reprotected route should be emitted");
    assert_eq!(reprotected["auth"]["required"], serde_json::json!(true));
    assert_eq!(
        reprotected["auth"]["scopes"],
        serde_json::json!(["users:read"])
    );
    let anonymous_after_scope = routes
        .iter()
        .find(|route| route["pattern"] == "/anonymous-after-scope")
        .expect("anonymous-after-scope route should be emitted");
    assert_eq!(
        anonymous_after_scope["auth"]["allowAnonymous"],
        serde_json::json!(true)
    );
    assert_eq!(
        anonymous_after_scope["auth"]["scopes"],
        serde_json::json!([])
    );
    let require_after_scope = routes
        .iter()
        .find(|route| route["pattern"] == "/require-after-scope")
        .expect("require-after-scope route should be emitted");
    assert_eq!(
        require_after_scope["auth"]["schemes"],
        serde_json::json!(["bearerAuth"])
    );
    assert_eq!(require_after_scope["auth"]["scopes"], serde_json::json!([]));
    let group_open = routes
        .iter()
        .find(|route| route["pattern"] == "/group/open")
        .expect("group route should be emitted");
    assert_eq!(
        group_open["auth"]["allowAnonymous"],
        serde_json::json!(true)
    );
    let public = routes
        .iter()
        .find(|route| route["pattern"] == "/public")
        .expect("public route should be emitted");
    assert_eq!(public["auth"]["allowAnonymous"], serde_json::json!(true));
}

#[test]
fn auth_result_helpers_emit_response_metadata() {
    let source = r#"import { Sloppy, Auth, Config } from "sloppy";
const app = Sloppy.create();
app.use(Auth.cookieSession({
  name: "sloppy.session",
  secret: Config.required("Auth:SessionSecret")
}));
app.post("/login", (ctx) => Auth.signIn(ctx, { sub: "1", roles: ["admin"] }));
app.post("/logout", (ctx) => Auth.signOut(ctx));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("auth helpers should extract response metadata");
    let login = app
        .routes
        .iter()
        .find(|route| route.pattern == "/login")
        .and_then(|route| route.handler.response.as_ref())
        .expect("login response metadata should exist");
    assert_eq!(login.status, 200);
    assert_eq!(login.kind, "json");
    let logout = app
        .routes
        .iter()
        .find(|route| route.pattern == "/logout")
        .and_then(|route| route.handler.response.as_ref())
        .expect("logout response metadata should exist");
    assert_eq!(logout.status, 204);
    assert_eq!(logout.kind, "empty");
}

#[test]
fn controller_routes_apply_fluent_schema_metadata() {
    let source = r#"import { Sloppy, Results, Schema } from "sloppy";
const CreateUser = Schema.object({ name: Schema.string() });
const User = Schema.object({ id: Schema.integer(), name: Schema.string() });
class UsersController {
  create(_ctx) {
    return Results.created("/users/1", { id: 1, name: "Ada" });
  }
}
const app = Sloppy.create();
app.mapController("/users", UsersController, (mapper) => {
  mapper.post("/", "create").accepts(CreateUser).returns(User);
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("controller fluent schema metadata should extract");
    let route = app
        .routes
        .iter()
        .find(|route| route.method == "POST" && route.pattern == "/users")
        .expect("controller route should exist");
    assert_eq!(route.handler.bindings.len(), 1);
    assert_eq!(route.handler.bindings[0].kind, "body.json");
    assert_eq!(
        route.handler.bindings[0].schema.as_deref(),
        Some("CreateUser")
    );
    assert_eq!(
        route
            .handler
            .response
            .as_ref()
            .and_then(|response| response.body_schema.as_deref()),
        Some("User")
    );
}

#[test]
fn unsupported_dynamic_framework_features_fail_closed() {
    for (source, code) in [
        (
            r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const middleware = [(_ctx, next) => next()];
app.use(middleware[0]);
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const policy = { origins: ["https://app.example.com"] };
const app = Sloppy.create();
app.useCors(policy);
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_CORS",
        ),
        (
            r#"import { Sloppy, Results, RequestId } from "sloppy";
const app = Sloppy.create();
app.use(RequestId.defaults({ generator: () => "req-1" }));
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
        ),
        (
            r#"import { Sloppy, Results, RequestLogging } from "sloppy";
const includeRoute = true;
const app = Sloppy.create();
app.use(RequestLogging.defaults({ includeRoute }));
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
class UsersController {}
const app = Sloppy.create();
app.controller("/users", UsersController, (mapper) => {
  mapper.get("/", "missing");
});
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
const controller = class UsersController {};
const app = Sloppy.create();
app.mapController("/users", controller, (mapper) => {
  mapper.get("/", "list");
});
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
        ),
        (
            r#"import { Sloppy, Results, Testing } from "sloppy";
const app = Sloppy.create();
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_TESTING_IMPORT",
        ),
        (
            r#"import { Sloppy, Results, Testing as TestHost } from "sloppy";
const app = Sloppy.create();
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_TESTING_IMPORT",
        ),
        (
            r#"import { Sloppy, Results, TestHost } from "sloppy";
const app = Sloppy.create();
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_TESTING_IMPORT",
        ),
        (
            r#"import { Sloppy, Results, TestServices } from "sloppy";
const app = Sloppy.create();
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_TESTING_IMPORT",
        ),
        (
            r#"import { Sloppy, Results, FakeClock } from "sloppy";
const app = Sloppy.create();
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_TESTING_IMPORT",
        ),
        (
            r#"import { Sloppy, Results, TestData } from "sloppy";
const app = Sloppy.create();
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_TESTING_IMPORT",
        ),
    ] {
        let diagnostic = extract(std::path::Path::new("app.ts"), source)
            .expect_err("recognized unsupported framework surface should fail");
        assert_eq!(diagnostic.code, code);
    }
}

#[test]
fn typed_framework_sync_body_bindings_emit_sync_wrapper() {
    let source = r#"import { Sloppy, Results, Body } from "sloppy";
type UserCreate = { name: string; email: string };
const app = Sloppy.create();
app.post("/users", (input: Body<UserCreate>) => Results.created(`/users/${input.email}`, {
  name: input.name,
  email: input.email,
}));
export default app;
"#;
    let app =
        extract(std::path::Path::new("app.ts"), source).expect("typed body handler should extract");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("globalThis.__sloppy_handler_1 = (ctx) =>"));
    assert!(emitted_js.source.contains("ctx.body.json()"));
    assert!(!emitted_js.source.contains("ctx.request.json()"));
    assert!(emitted_js
        .source
        .contains("return (ctx) => __sloppy_typed_handler(ctx.body.json())"));
    assert!(!emitted_js
        .source
        .contains("const __sloppy_args = await Promise.all(["));
}

#[test]
fn typed_framework_context_only_bindings_emit_sync_wrapper() {
    let source = r#"import { Sloppy, Results, RequestContext } from "sloppy";
const app = Sloppy.create();
app.get("/method", (ctx: RequestContext) => Results.text(ctx.request.method));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed context handler should extract");
    assert_eq!(
        app.routes[0].handler.bindings[0].name.as_deref(),
        Some("request.method")
    );
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("globalThis.__sloppy_handler_1 = (ctx) =>"));
    assert!(emitted_js
        .source
        .contains("return (ctx) => __sloppy_typed_handler(ctx)"));
    assert!(!emitted_js
        .source
        .contains("const __sloppy_args = await Promise.all(["));
    assert!(!emitted_js
        .source
        .contains("__sloppy_framework_services.createScope(ctx)"));
}

#[test]
fn typed_framework_string_route_binding_uses_direct_route_lookup() {
    let source = r#"import { Sloppy, Results, Route } from "sloppy";
const app = Sloppy.create();
app.get("/users/:id", (id: Route<string>) => Results.ok({ id }));
export default app;
"#;
    let app =
        extract(std::path::Path::new("app.ts"), source).expect("typed route handler should extract");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("return (ctx) => __sloppy_typed_handler(ctx.route[\"id\"])"));
    assert!(!emitted_js.source.contains("__sloppy_framework_arg(ctx, undefined"));
}

#[test]
fn typed_framework_inferred_string_route_binding_uses_direct_route_lookup() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/users/:slug", (slug: string) => Results.ok({ slug }));
export default app;
"#;
    let app =
        extract(std::path::Path::new("app.ts"), source).expect("typed route handler should extract");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("return (ctx) => __sloppy_typed_handler(ctx.route[\"slug\"])"));
}

#[test]
fn typed_framework_constrained_route_binding_keeps_generic_coercion() {
    let source = r#"import { Sloppy, Results, Route } from "sloppy";
const app = Sloppy.create();
app.get("/users/:id:int", (id: Route<number>) => Results.ok({ id }));
export default app;
"#;
    let app =
        extract(std::path::Path::new("app.ts"), source).expect("typed route handler should extract");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("__sloppy_framework_arg(ctx, undefined, {\"capability\":null,\"injectionKind\":null,\"kind\":\"route\",\"name\":\"id\""));
    assert!(!emitted_js
        .source
        .contains("__sloppy_typed_handler(ctx.route[\"id\"])"));
}

#[test]
fn typed_framework_header_bindings_use_header_facade() {
    let source = r#"import { Sloppy, Results, Header } from "sloppy";
const app = Sloppy.create();
app.get("/trace", (trace: Header<"x-trace">) => Results.ok({ trace }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed header handler should extract");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("ctx.header[__sloppy_framework_header_property(binding.name)]"));
    assert!(emitted_js
        .source
        .contains("function __sloppy_framework_header_property(name)"));
    assert!(!emitted_js
        .source
        .contains("ctx.request.headers.get(binding.name)"));
}

#[test]
fn typed_framework_service_bindings_keep_async_scope_cleanup() {
    let source = r#"import { Sloppy, Results, Service } from "sloppy";
type Clock = { now: string };
const app = Sloppy.create();
app.services.addScoped("Clock", () => ({ now: "2026-05-13T00:00:00Z" }));
app.get("/clock", (clock: Service<Clock>) => Results.ok({ now: clock.now }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed service handler should extract");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("globalThis.__sloppy_handler_1 = (() =>"));
    assert!(emitted_js.source.contains("return async (ctx) =>"));
    assert!(emitted_js
        .source
        .contains("const __sloppy_args = await Promise.all(["));
    assert!(emitted_js
        .source
        .contains("return await __sloppy_typed_handler(...__sloppy_args);"));
    assert!(emitted_js
        .source
        .contains("await __sloppy_scope.dispose();"));
}

#[test]
fn typed_framework_handlers_emit_same_file_helpers() {
    let source = r#"import { Sloppy, Results, RequestContext } from "sloppy";
import { Sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
async function seedUsers(db, ctx) {
  await db.exec("create table users (id integer primary key, name text)", [], {
    signal: ctx.signal,
    deadline: ctx.deadline,
  });
}
app.get("/users", async (db: Sqlite<"main">, ctx: RequestContext) => {
  await seedUsers(db, ctx);
  return Results.ok(await db.query("select id, name from users", []));
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed handler helper should extract");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("async function seedUsers(db, ctx)"));
    assert!(emitted_js.source.contains("await seedUsers(db, ctx);"));
    assert!(emitted_js
        .source
        .contains("return await __sloppy_typed_handler(...__sloppy_args);"));
}

#[test]
fn typed_framework_handler_erases_nested_typescript_syntax() {
    let source = r#"import { Sloppy, Results, Body } from "sloppy";
import { sql } from "sloppy/data";
import { Postgres } from "sloppy/providers/postgres";
type UserCreate = { name: string; email: string };
type UserDto = { id: number; name: string; email: string };
const app = Sloppy.create();
app.post("/users", async (input: Body<UserCreate>, db: Postgres<"main">) => {
  const first: UserCreate = input! as UserCreate;
  const checked = ({ name: first.name, email: first.email } satisfies UserCreate);
  const mapped = [first].map((item: UserCreate): UserDto => ({
    id: Number.parseInt("1", 10),
    name: item.name,
    email: item.email,
  }));
  const loaded = await Promise.all<UserDto>(mapped.map(async (item: UserDto): Promise<UserDto> => {
    const row: UserDto = await db.queryOne<UserDto>("select id, name, email from users where id = $1", [item.id]);
    return row;
  }));
  const typedQuery = sql`select ${input! as UserCreate} where id = ${loaded[0]!.id}`;
  function normalize(user: UserDto): UserDto {
    return user;
  }
  return Results.created(`/users/${loaded[0]!.id}`, normalize(loaded[0]));
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed handler with nested TypeScript syntax should extract");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("const first = input;"));
    assert!(emitted_js
        .source
        .contains("const checked = ({ name: first.name, email: first.email });"));
    assert!(emitted_js.source.contains("function normalize(user)"));
    assert!(!emitted_js.source.contains("const first:"));
    assert!(!emitted_js.source.contains(" as UserCreate"));
    assert!(!emitted_js.source.contains(" satisfies UserCreate"));
    assert!(!emitted_js.source.contains("input!"));
    assert!(!emitted_js.source.contains("loaded[0]!"));
    assert!(!emitted_js.source.contains("(item:"));
    assert!(!emitted_js.source.contains("): UserDto"));
    assert!(!emitted_js.source.contains("function normalize(user:"));
    assert!(!emitted_js.source.contains("Promise.all<UserDto>"));
    assert!(!emitted_js.source.contains("queryOne<UserDto>"));
    assert!(emitted_js
        .source
        .contains("const typedQuery = sql`select ${input} where id = ${loaded[0].id}`;"));
    assert!(!emitted_js.source.contains("input! as UserCreate"));
    assert!(!emitted_js.source.contains("loaded[0]!.id"));
}

#[test]
fn typed_framework_source_maps_point_at_user_handler_source() {
    let source = r#"import { Sloppy, Results, Body } from "sloppy";
type UserCreate = { name: string };
const app = Sloppy.create();
app.post("/users", async (input: Body<UserCreate>) => {
  const name: string = input.name;
  return Results.created(`/users/${name}`, { name });
});
export default app;
"#;
    let app =
        extract(std::path::Path::new("app.ts"), source).expect("typed handler should extract");
    let emitted_js = super::emit_app_js(&app);
    assert!(app.routes[0]
        .handler
        .source
        .contains("input: Body<UserCreate>"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("const __sloppy_typed_handler = async (input)"));
    let handler_start = emitted_js
        .handler_generated_starts
        .first()
        .expect("handler start should be recorded");
    let first_mapping = emitted_js
        .mappings
        .iter()
        .find(|mapping| mapping.generated_line == handler_start.generated_line)
        .expect("typed handler should have a same-line source map segment");
    let (original_line, original_column) =
        super::line_column(source, app.routes[0].handler.span.start);
    assert!(first_mapping.generated_column > handler_start.generated_column);
    assert_eq!(first_mapping.original_line, original_line.saturating_sub(1));
    assert_eq!(
        first_mapping.original_column,
        original_column.saturating_sub(1)
    );
}

#[test]
fn typed_framework_provider_injection_uses_configured_provider_options() {
    let source = r#"import { Sloppy, Results, RequestContext } from "sloppy";
import { sql } from "sloppy/data";
import { Postgres } from "sloppy/providers/postgres";
import { Sqlite } from "sloppy/providers/sqlite";
import { SqlServer } from "sloppy/providers/sqlserver";
const app = Sloppy.create();
app.get("/users", async (pg: Postgres<"main">, sqlite: Sqlite<"audit">, sqlserver: SqlServer<"search">, ctx: RequestContext) => {
  await sqlite.exec(sql`create table if not exists audit(id integer)`, { deadline: ctx.deadline });
  return Results.ok({ ok: true });
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed provider injection should extract");
    assert!(app.capabilities.iter().any(|capability| {
        capability.token == "data.main"
            && capability.capability_kind == "database"
            && capability.provider == "postgres"
            && capability.config_name.as_deref() == Some("main")
    }));
    assert!(app.capabilities.iter().any(|capability| {
        capability.token == "data.audit"
            && capability.capability_kind == "database"
            && capability.provider == "sqlite"
            && capability.config_name.as_deref() == Some("audit")
    }));
    assert!(app.capabilities.iter().any(|capability| {
        capability.token == "data.search"
            && capability.capability_kind == "database"
            && capability.provider == "sqlserver"
            && capability.config_name.as_deref() == Some("search")
    }));
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("\"connectionStringKey\":\"Sloppy:Providers:postgres:main:connectionString\""));
    assert!(emitted_js.source.contains(
        "\"connectionStringEnv\":\"Sloppy__Providers__postgres__main__connectionString\""
    ));
    assert!(emitted_js.source.contains(
        "\"connectionStringKey\":\"Sloppy:Providers:sqlserver:search:connectionString\""
    ));
    assert!(emitted_js.source.contains(
        "\"connectionStringEnv\":\"Sloppy__Providers__sqlserver__search__connectionString\""
    ));
    assert!(emitted_js
        .source
        .contains("data.postgres.open(__sloppy_framework_provider_open_options"));
    assert!(emitted_js
        .source
        .contains("data.sqlserver.open(__sloppy_framework_provider_open_options"));
    assert!(emitted_js.source.contains("data.sqlite(dependencyName)"));
    assert!(!emitted_js.source.contains("data.postgres.open({ provider:"));
    assert!(!emitted_js
        .source
        .contains("data.sqlserver.open({ provider:"));
}

#[test]
fn typed_framework_provider_injection_uses_placeholder_environment_source() {
    let root = fixture_temp_dir("typed-provider-config-env-source");
    let input = root.join("app.ts");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Postgres } from "sloppy/providers/postgres";
const app = Sloppy.create();
app.get("/users", async (pg: Postgres<"main">) => Results.ok({ ok: true }));
export default app;
"#;
    fs::write(&input, source).expect("input should be written");
    fs::write(
        root.join("appsettings.json"),
        r#"{"Sloppy":{"Providers":{"postgres":{"main":{"connectionString":"${SLOPPY_TEST_PG_URL}"}}}}}"#,
    )
    .expect("appsettings should be written");
    std::env::set_var(
        "SLOPPY_TEST_PG_URL",
        "postgres://user:<PASSWORD>@example/db",
    );

    let mut app = extract(&input, source).expect("typed provider injection should extract");
    let configuration = super::ConfigurationModel::load(&input, &super::CompileOptions::new(), &[])
        .expect("configuration should load");
    configuration
        .apply_to_app(&mut app)
        .expect("configuration should apply");
    let emitted_js = super::emit_app_js(&app);
    let plan = super::emit_plan(&app, "bundle-hash", "map-hash").expect("plan should emit");

    std::env::remove_var("SLOPPY_TEST_PG_URL");
    assert!(emitted_js
        .source
        .contains("\"connectionStringEnv\":\"SLOPPY_TEST_PG_URL\""));
    assert!(!emitted_js
        .source
        .contains("postgres://user:<PASSWORD>@example/db"));
    assert!(plan.contains("\"value\": \"<redacted>\""));
    assert!(!plan.contains("postgres://user:<PASSWORD>@example/db"));
}

#[test]
fn typed_framework_response_schema_inference_is_scope_aware() {
    let source = r#"import { Sloppy, Results, Route } from "sloppy";
import { Postgres } from "sloppy/providers/postgres";
type UserDto = { id: number };
type OrderDto = { id: number };
const app = Sloppy.create();
app.get("/items/:id", async (id: Route<number>, db: Postgres<"main">) => {
  if (id > 0) {
    const payload = await db.queryOne<UserDto>("select user");
    return Results.ok(payload);
  }
  const payload = await db.queryOne<OrderDto>("select order");
  return Results.ok(payload);
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("scoped response schemas should extract");
    let body_schemas: Vec<_> = app.routes[0]
        .handler
        .responses
        .iter()
        .map(|response| response.body_schema.as_deref())
        .collect();
    assert_eq!(body_schemas, vec![Some("UserDto"), Some("OrderDto")]);
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["routes"][0]["jsonResponse"]["mode"], "fallback");
    assert_eq!(
        plan["routes"][0]["jsonResponse"]["fallbackReason"],
        "multiple-json-response-schemas"
    );
}

#[test]
fn typed_framework_response_dedupe_preserves_distinct_body_schema() {
    let source = r#"import { Sloppy, Results, Route } from "sloppy";
type UserDto = { id: number };
type OrderDto = { id: number };
const app = Sloppy.create();
app.get("/items/:id", (id: Route<number>) => {
  if (id > 0) {
    return Results.ok<UserDto>({ id });
  }
  return Results.ok<OrderDto>({ id });
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("distinct typed responses should extract");
    let body_schemas: Vec<_> = app.routes[0]
        .handler
        .responses
        .iter()
        .map(|response| response.body_schema.as_deref())
        .collect();
    assert_eq!(body_schemas, vec![Some("UserDto"), Some("OrderDto")]);
}

#[test]
fn typed_framework_response_schema_ignores_arbitrary_generic_wrappers() {
    let source = r#"import { Sloppy, Results, Route } from "sloppy";
type UserDto = { id: number };
const app = Sloppy.create();
app.get("/items/:id", async (id: Route<number>) => {
  const payload = await Promise.resolve<UserDto>({ id });
  return Results.ok(payload);
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("unsupported generic wrapper should not become schema evidence");
    assert_eq!(app.routes[0].handler.responses.len(), 1);
    assert_eq!(app.routes[0].handler.responses[0].body_schema, None);
}

#[test]
fn import_call_text_in_comments_and_strings_is_not_dynamic_import() {
    let source = r#"import { Sloppy, Results } from "sloppy";
// import("./commented.js") documents unsupported syntax.
const app = Sloppy.create();
const note = "dynamic import text: import(\"./string.js\")";
app.mapGet("/", () => Results.text("Hello"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("comments and strings should not trigger dynamic import diagnostics");
    assert_eq!(app.routes.len(), 1);
}

#[test]
fn captured_member_expression_emits_dynamic_response_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const config = { message: "captured" };
app.mapGet("/", (ctx) => Results.json({ message: config.message, id: ctx.route.id }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("captured response value should fall back to dynamic metadata");
    assert!(app.routes.is_empty());
    assert_eq!(app.dynamic_routes.len(), 1);
    assert_eq!(app.dynamic_routes[0].method, Some("GET"));
    assert_eq!(app.dynamic_routes[0].pattern.as_deref(), Some("/"));
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("accepts(schema)"));
    assert!(emitted_js
        .source
        .contains("returns(schema, options = undefined)"));
}

#[test]
fn rejects_destructured_or_default_handler_parameters() {
    for source in [
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapGet("/", ({ route }) => Results.json({ id: route.id }));
export default app;
"#,
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapGet("/", ([ctx]) => Results.json({ id: ctx.route.id }));
export default app;
"#,
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapGet("/", (ctx = {}) => Results.json({ id: ctx.route.id }));
export default app;
"#,
    ] {
        let diagnostic = extract(std::path::Path::new("app.js"), source)
            .expect_err("unsupported handler parameter should fail");
        assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS");
    }
}

#[test]
fn ignores_unrelated_map_named_initializers() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const items = ["ok"];
const labels = items.map((value) => value);
app.mapGet("/", () => Results.text("Hello"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("ordinary JavaScript map initializer should not be treated as a route");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/");
}

#[test]
fn route_metadata_errors_in_initializers_are_reported() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const tags = ["users"];
const route = app.get("/users", { tags }, () => Results.ok({ ok: true }));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("dynamic route metadata in an initializer should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS");
    assert!(diagnostic.path.is_some());
}

#[test]
fn extracts_route_options_and_group_tags_into_plan() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const users = app.group("/users").withTags("users");
users.get("/", { name: "Users.List", tags: ["list"] }, () => Results.ok([]));
const admin = app.group("/admin").withTags("admin").withTags("v1");
admin.get("/audit", () => Results.ok([])).withName("Admin.Audit").withName("Admin.Audit.Latest");
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("route metadata options should extract");
    assert_eq!(app.routes.len(), 2);
    assert_eq!(app.routes[0].name.as_deref(), Some("Users.List"));
    assert_eq!(
        app.routes[0].tags,
        vec!["users".to_string(), "list".to_string()]
    );
    assert_eq!(app.routes[1].name.as_deref(), Some("Admin.Audit.Latest"));
    assert_eq!(
        app.routes[1].tags,
        vec!["admin".to_string(), "v1".to_string()]
    );

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let emitted_plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value =
        serde_json::from_str(&emitted_plan).expect("plan should be valid json");
    assert_eq!(
        value["routes"][0]["tags"],
        serde_json::json!(["users", "list"])
    );
    assert_eq!(
        value["routes"][1]["tags"],
        serde_json::json!(["admin", "v1"])
    );
}

#[test]
fn emits_route_dispatch_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/health", () => Results.json({ ok: true })).withName("Health.Get");
app.get("/users/{id:int}", (ctx) => Results.json({ id: ctx.route.id })).withName("Users.Get");
app.get("/{tenant}/users/{id:int}", (ctx) => Results.text(ctx.route.tenant)).withName("Tenant.Users.Get");
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("static and parameter routes should extract");
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("plan should parse");

    assert_eq!(value["features"]["nativeEndpointDispatch"], true);
    assert_eq!(value["strongPlan"]["evidence"]["routeDispatch"], true);
    assert_eq!(value["routeDispatch"]["mode"], "native-compiled");
    assert_eq!(value["routeDispatch"]["artifact"]["kind"], "slrt");
    assert_eq!(value["routeDispatch"]["artifact"]["path"], "routes.slrt");
    assert!(value["routeDispatch"]["artifact"]["hash"]
        .as_str()
        .expect("route artifact hash should be a string")
        .starts_with("sha256:"));
    assert_eq!(value["routeDispatch"]["routeCount"], 3);
    assert_eq!(value["routeDispatch"]["staticRoutes"], 1);
    assert_eq!(value["routeDispatch"]["parameterRoutes"], 2);
    assert_eq!(
        value["routeDispatch"]["dispatchStats"]["parameterCandidateBuckets"],
        2
    );
    assert_eq!(
        value["routeDispatch"]["dispatchStats"]["constraints"],
        serde_json::json!(["int"])
    );
    assert_eq!(
        value["routeDispatch"]["dispatchStats"]["segmentTrieNodes"],
        6
    );
    assert_eq!(
        value["routes"][0]["dispatch"]["strategy"],
        "exact-static-hash"
    );
    assert_eq!(value["routes"][1]["dispatch"]["strategy"], "segment-trie");
    assert_eq!(
        value["routes"][1]["dispatch"]["executionKind"],
        "v8-handler"
    );
    assert_eq!(value["routes"][2]["dispatch"]["strategy"], "segment-trie");
}

#[test]
fn extracts_chained_group_tags_and_auth_requirements() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const secure = app.group("/v1").withTags("v1").requireAuth({ role: "admin" });
const users = secure.group("/users").withTags("users");
users.get("/", () => Results.ok([])).withName("Users.List");
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("chained group tags and auth should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/v1/users");
    assert_eq!(
        app.routes[0].tags,
        vec!["v1".to_string(), "users".to_string()]
    );
    let auth = app.routes[0].auth.as_ref().expect("auth should inherit");
    assert!(auth.required);
    assert_eq!(auth.roles, vec!["admin".to_string()]);

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let emitted_plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value =
        serde_json::from_str(&emitted_plan).expect("plan should be valid json");
    assert_eq!(value["routes"][0]["auth"]["required"], true);
    assert_eq!(
        value["routes"][0]["auth"]["roles"],
        serde_json::json!(["admin"])
    );
}

#[test]
fn success_fixture_expected_outputs_stay_current() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    for fixture_name in [
        "hello-mapget",
        "builder-mapget",
        "grouped-route",
        "results-json",
        "function-handler",
        "http-methods",
        "async-handler",
        "provider-capability",
        "metadata-extraction",
        "effects-capability",
        "realistic-users-api",
        "partial-body-without-schema",
        "partial-dynamic-status",
        "provider-metadata-multiple-databases",
        "function-module-empty",
        "function-module",
        "source-map",
    ] {
        let fixture = root
            .join("tests/fixtures")
            .join(fixture_name)
            .join("input.js");
        let source = fs::read_to_string(&fixture).expect("fixture input should exist");
        let mut app = extract(&fixture, &source).expect("fixture should extract");
        super::ConfigurationModel::load(&fixture, &CompileOptions::new(), &app.config_reads)
            .expect("fixture configuration should load")
            .apply_to_app(&mut app)
            .expect("fixture configuration should apply");

        let emitted_js = super::emit_app_js(&app);
        let expected_js = fs::read_to_string(
            root.join("tests/fixtures")
                .join(fixture_name)
                .join("expected/app.js"),
        )
        .expect("expected app.js should exist");
        assert_eq!(emitted_js.source, expected_js, "{fixture_name} app.js");

        let emitted_source_map = super::emit_source_map(&app, &emitted_js);
        let emitted_js_hash = super::sha256_hex(&emitted_js.source);
        let emitted_map_hash = super::sha256_hex(&emitted_source_map);
        let emitted_plan =
            super::emit_plan(&app, &emitted_js_hash, &emitted_map_hash).expect("plan should emit");
        let expected_plan = fs::read_to_string(
            root.join("tests/fixtures")
                .join(fixture_name)
                .join("expected/app.plan.json"),
        )
        .expect("expected app.plan.json should exist");
        assert_eq!(emitted_plan, expected_plan, "{fixture_name} app.plan.json");

        let expected_source_map = fs::read_to_string(
            root.join("tests/fixtures")
                .join(fixture_name)
                .join("expected/app.js.map"),
        )
        .expect("expected app.js.map should exist");
        assert_eq!(
            emitted_source_map, expected_source_map,
            "{fixture_name} app.js.map"
        );
    }
}

#[test]
fn realtime_routes_emit_kind_metadata_and_runtime_wrappers() {
    let source = r#"
import { Sloppy, Results, Realtime, Schema } from "sloppy";

const app = Sloppy.create();
const Chat = Realtime.channel("chat", {
    client: {
        sendMessage: Schema.object({ text: Schema.string() })
    },
    server: {
        messageCreated: Schema.object({ text: Schema.string() })
    }
});
app.sse("/events", async (ctx, stream) => {
    await stream.event("ready", { ok: true });
}).requireAuth();
const group = app.group("/live");
group.websocket("/ws", async (ctx, socket) => {
    await socket.sendJson({ ok: true });
}, { protocols: ["sloppy.realtime"], maxMessageBytes: 64 * 1024, maxSendQueueBytes: 1024 * 1024, heartbeatMs: 15000, idleTimeoutMs: 30000, closeTimeoutMs: 5000, compression: false, slowClientPolicy: "close" }).requiresAuth().requiresScope("realtime").allowedOrigins(["https://app.example.com"]);
app.ws("/option-first", { protocols: ["option.first"], origins: "*", maxMessageBytes: 4096 }, async (socket) => {
    await socket.accept();
});
app.realtime("/chat", Chat, async (ctx) => {
    await ctx.accept();
}, { presence: true, protocols: ["sloppy.realtime.chat.v1"], maxMessageBytes: 8192 });
export default app;
"#;
    let path = std::path::Path::new("realtime.js");
    let app = extract(path, source).expect("realtime app should extract");
    assert_eq!(app.routes.len(), 4);
    assert_eq!(app.routes[0].method, "GET");
    assert_eq!(app.routes[0].kind, "sse");
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("Realtime.sse("));
    assert_eq!(app.routes[1].pattern, "/live/ws");
    assert_eq!(app.routes[1].kind, "websocket");
    assert_eq!(
        app.routes[1].websocket.as_ref().unwrap().protocols[0],
        "sloppy.realtime"
    );
    assert!(matches!(
        app.routes[1].websocket.as_ref().unwrap().origins,
        Some(WebSocketOriginsMetadata::List(_))
    ));
    assert!(app.routes[1]
        .handler
        .emitted_source
        .contains("Realtime.websocket("));
    assert_eq!(app.routes[2].pattern, "/option-first");
    assert_eq!(app.routes[2].kind, "websocket");
    assert_eq!(
        app.routes[2].websocket.as_ref().unwrap().protocols[0],
        "option.first"
    );
    assert!(matches!(
        app.routes[2].websocket.as_ref().unwrap().origins,
        Some(WebSocketOriginsMetadata::Any)
    ));
    assert_eq!(app.routes[3].pattern, "/chat");
    assert_eq!(app.routes[3].kind, "websocket");
    assert!(app.routes[3].realtime.is_some());
    assert!(app.routes[3]
        .handler
        .emitted_source
        .contains("Realtime.__route(Chat,"));
    assert_eq!(
        app.routes[3].websocket.as_ref().unwrap().protocols[0],
        "sloppy.realtime.chat.v1"
    );

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("Results, Realtime, SloppyRealtimeError, schema, Schema"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("plan should parse");
    assert_eq!(value["routes"][0]["kind"], "sse");
    assert_eq!(value["routes"][1]["kind"], "websocket");
    assert_eq!(
        value["routes"][1]["websocket"]["protocols"][0],
        "sloppy.realtime"
    );
    assert_eq!(
        value["routes"][1]["websocket"]["origins"][0],
        "https://app.example.com"
    );
    assert_eq!(value["routes"][1]["websocket"]["maxMessageBytes"], 65536);
    assert_eq!(
        value["routes"][1]["websocket"]["maxSendQueueBytes"],
        1048576
    );
    assert_eq!(value["routes"][1]["websocket"]["heartbeatMs"], 15000);
    assert_eq!(value["routes"][1]["websocket"]["idleTimeoutMs"], 30000);
    assert_eq!(value["routes"][1]["websocket"]["closeTimeoutMs"], 5000);
    assert_eq!(value["routes"][1]["websocket"]["compression"], false);
    assert_eq!(value["routes"][1]["websocket"]["slowClientPolicy"], "close");
    assert_eq!(value["routes"][2]["websocket"]["origins"], "*");
    assert_eq!(value["routes"][3]["realtime"]["kind"], "framework");
    assert_eq!(value["routes"][3]["realtime"]["metadataStatus"], "partial");
    assert_eq!(value["routes"][3]["realtime"]["channelExpression"], "Chat");
    assert_eq!(
        value["routes"][3]["websocket"]["protocols"][0],
        "sloppy.realtime.chat.v1"
    );
    assert_eq!(value["routes"][3]["websocket"]["maxMessageBytes"], 8192);
    assert_eq!(value["routes"][1]["auth"]["scopes"][0], "realtime");
    assert_eq!(value["features"]["realtime"], true);
    assert!(value["requiredFeatures"]
        .as_array()
        .expect("requiredFeatures should be an array")
        .contains(&serde_json::json!("runtime.realtime")));
}

#[test]
fn realtime_routes_without_options_keep_websocket_metadata_omitted() {
    let source = r#"
import { Sloppy, Realtime, Schema } from "sloppy";

const app = Sloppy.create();
const Chat = Realtime.channel("chat", {
    client: { sendMessage: Schema.object({ text: Schema.string() }) },
    server: { messageCreated: Schema.object({ text: Schema.string() }) }
});
app.realtime("/chat", Chat, async (ctx) => {
    await ctx.accept();
});
export default app;
"#;
    let app = extract(std::path::Path::new("realtime-defaults.js"), source)
        .expect("realtime route should extract");
    assert_eq!(app.routes.len(), 1);
    assert!(app.routes[0].realtime.is_some());
    assert!(app.routes[0].websocket.is_none());

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("plan should parse");
    assert_eq!(value["features"]["metadataInference"], true);
    assert_eq!(value["routes"][0]["realtime"]["kind"], "framework");
    assert!(value["routes"][0].get("websocket").is_none());
}

#[test]
fn invalid_realtime_route_arity_reports_diagnostic() {
    let source = r#"
import { Sloppy, Realtime, Schema } from "sloppy";

const app = Sloppy.create();
const Chat = Realtime.channel("chat", {
    client: { sendMessage: Schema.object({ text: Schema.string() }) },
    server: { messageCreated: Schema.object({ text: Schema.string() }) }
});
app.realtime("/chat", Chat);
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("bad-realtime.js"), source)
        .expect_err("invalid realtime arity should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_INVALID_REALTIME_ROUTE_ARGS");
}

#[test]
fn wrapped_realtime_descriptors_do_not_emit_schema_metadata() {
    let source = r#"
import { Sloppy, Realtime, Schema } from "sloppy";

const app = Sloppy.create();
const Chat = Object.freeze(Realtime.channel("chat", {
    client: { sendMessage: Schema.object({ text: Schema.string() }) },
    server: { messageCreated: Schema.object({ text: Schema.string() }) }
}));
const Typing = helper(Realtime.event(Schema.object({ roomId: Schema.string() })));
app.realtime("/chat", Chat, async (ctx) => {
    await ctx.accept();
});
export default app;
"#;
    let app = extract(std::path::Path::new("wrapped-realtime.js"), source)
        .expect("wrapped realtime descriptors should extract");
    assert!(app.schemas.is_empty());
    assert_eq!(app.routes.len(), 1);
    assert!(app.routes[0].realtime.is_some());
}

#[test]
fn websocket_route_options_reject_unsupported_static_shapes() {
    for source in [
        r#"import { Sloppy } from "sloppy";
const app = Sloppy.create();
const options = { protocols: ["chat"] };
app.ws("/ws", options, async (socket) => {
    await socket.accept();
});
export default app;
"#,
        r#"import { Sloppy } from "sloppy";
const app = Sloppy.create();
app.websocket("/ws", async (ctx, socket) => {
    await socket.accept();
}, { protocols: ["bad token"] });
export default app;
"#,
        r#"import { Sloppy } from "sloppy";
const app = Sloppy.create();
app.ws("/ws", { compression: true }, async (socket) => {
    await socket.accept();
});
export default app;
"#,
        r#"import { Sloppy } from "sloppy";
const app = Sloppy.create();
const origin = "https://app.example.com";
app.ws("/ws", async (socket) => {
    await socket.accept();
}).allowedOrigins(origin);
export default app;
"#,
    ] {
        let diagnostic = extract(std::path::Path::new("app.js"), source)
            .expect_err("unsupported websocket options should fail");
        assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS");
    }
}

#[test]
fn realtime_root_import_emits_runtime_export_for_ordinary_routes() {
    let source = r#"
import { Sloppy, Results, Realtime } from "sloppy";

const app = Sloppy.create();
app.get("/debug", () => Results.text("ok"));
export default app;
"#;
    let path = std::path::Path::new("realtime-debug.js");
    let app = extract(path, source).expect("realtime import app should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].kind, "http");
    assert!(app.uses_realtime_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("Results, Realtime"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("plan should parse");
    assert_eq!(value["features"]["realtime"], true);
    assert!(value["requiredFeatures"]
        .as_array()
        .expect("requiredFeatures should be an array")
        .contains(&serde_json::json!("runtime.realtime")));
}

#[test]
fn realtime_provider_routes_keep_async_cleanup_wrapper() {
    let source = r#"
import { Sloppy } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";

const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.sse("/events", async (ctx, stream) => stream.send(await db.query("select id from users", [])));
export default app;
"#;
    let app = extract(std::path::Path::new("realtime-provider.js"), source)
        .expect("provider-backed realtime app should extract");

    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].kind, "sse");
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("async function(ctx)"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("return await (Realtime.sse("));
}

#[test]
fn rejected_fixture_diagnostics_stay_current() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    for (fixture_name, input_name) in [
        ("unsupported-handler-parameter", "input.js"),
        ("unsupported-handler-shape", "input.js"),
        ("unsupported-import-alias", "input.js"),
        ("unsupported-data-import-alias", "input.js"),
        ("unsupported-sloppy-default-import", "input.js"),
        ("unsupported-import-specifier", "input.js"),
        ("node-fs-import", "input.js"),
        ("missing-app", "input.js"),
        ("multiple-apps", "input.js"),
        ("unsupported-http-method", "input.js"),
        ("unsupported-async-handler-body", "input.js"),
        ("unsupported-secret-capability", "input.js"),
        ("unsupported-typescript-handler", "input.ts"),
        ("unsupported-dynamic-import", "input.js"),
        ("missing-relative-import", "input.js"),
        ("missing-provider-effect", "input.js"),
        ("non-sqlite-provider-bridge", "input.js"),
        ("unsupported-provider-method", "input.js"),
        ("unsupported-route-options-dynamic-tags", "input.js"),
        ("unsupported-cors-dynamic", "input.js"),
        ("unsupported-request-id-dynamic", "input.js"),
        ("unsupported-request-logging-dynamic", "input.js"),
        ("unsupported-controller-mapping", "input.js"),
        ("unsupported-testing-import", "input.js"),
        ("unsupported-health-captured-check", "input.js"),
    ] {
        let fixture = root
            .join("tests/fixtures")
            .join(fixture_name)
            .join(input_name);
        let source = fs::read_to_string(&fixture).expect("fixture input should exist");
        let diagnostic = extract(&fixture, &source).expect_err("fixture should be rejected");
        let expected = fs::read_to_string(
            root.join("tests/fixtures")
                .join(fixture_name)
                .join("expected-diagnostics.txt"),
        )
        .expect("expected diagnostic should exist");
        let rendered = diagnostic
            .render(Some(&source))
            .replace(&crate::source::display_path(root), "<compiler>");
        assert_eq!(format!("{rendered}\n"), expected, "{fixture_name}");
    }
}

#[test]
fn rejected_build_does_not_emit_success_artifacts() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    let input = root.join("tests/fixtures/unsupported-dynamic-import/input.js");
    let out_dir = std::env::temp_dir().join(format!(
        "sloppyc-rejected-build-test-{}",
        std::process::id()
    ));

    if out_dir.exists() {
        fs::remove_dir_all(&out_dir).expect("stale test output directory should be removable");
    }

    let failure = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("fixture should fail to build");
    assert_eq!(
        failure.diagnostic.code,
        "SLOPPYC_E_UNSUPPORTED_DYNAMIC_IMPORT"
    );
    assert!(
        !out_dir.join("app.plan.json").exists()
            && !out_dir.join("app.js").exists()
            && !out_dir.join("app.js.map").exists(),
        "rejected compiler input must not leave success artifacts"
    );

    if out_dir.exists() {
        fs::remove_dir_all(&out_dir).expect("test output directory should be removable");
    }
}

#[test]
fn build_writes_expected_artifacts_to_requested_output_directory() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    let input = root.join("../examples/compiler-hello/app.js");
    let out_dir = std::env::temp_dir().join(format!("sloppyc-build-test-{}", std::process::id()));

    if out_dir.exists() {
        fs::remove_dir_all(&out_dir).expect("stale test output directory should be removable");
    }

    super::build(&input, &out_dir, &CompileOptions::new()).expect("compiler example should build");

    let emitted_plan =
        fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be written");
    let expected_plan =
        fs::read_to_string(root.join("../examples/compiler-hello/expected/app.plan.json"))
            .expect("expected plan should exist");
    assert_eq!(emitted_plan, expected_plan);

    let emitted_js = fs::read_to_string(out_dir.join("app.js")).expect("app.js should be written");
    let expected_js = fs::read_to_string(root.join("../examples/compiler-hello/expected/app.js"))
        .expect("expected app.js should exist");
    assert_eq!(emitted_js, expected_js);

    let emitted_map =
        fs::read_to_string(out_dir.join("app.js.map")).expect("source map should be written");
    let expected_map =
        fs::read_to_string(root.join("../examples/compiler-hello/expected/app.js.map"))
            .expect("expected app.js.map should exist");
    assert_eq!(emitted_map, expected_map);

    fs::remove_dir_all(&out_dir).expect("test output directory should be removable");
}

#[test]
fn compiler_hello_artifacts_are_repeatable_and_path_clean() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    let input = root.join("../examples/compiler-hello/app.js");
    let base = std::env::temp_dir().join(format!(
        "sloppyc-main-determinism-test-{}",
        std::process::id()
    ));
    let first = base.join("first");
    let second = base.join("second");

    if base.exists() {
        fs::remove_dir_all(&base).expect("stale test output directory should be removable");
    }

    super::build(&input, &first, &CompileOptions::new()).expect("first build should succeed");
    super::build(&input, &second, &CompileOptions::new()).expect("second build should succeed");

    for artifact in ["app.plan.json", "app.js", "app.js.map"] {
        let first_text =
            fs::read_to_string(first.join(artifact)).expect("first artifact should exist");
        let second_text =
            fs::read_to_string(second.join(artifact)).expect("second artifact should exist");
        assert_eq!(first_text, second_text, "{artifact} should be repeatable");

        assert!(
            !first_text.contains(env!("CARGO_MANIFEST_DIR")),
            "{artifact} must not contain the local compiler manifest path"
        );
        assert!(
            !first_text.contains("\\Slop\\") && !first_text.contains("/Slop/"),
            "{artifact} must not contain checkout-local paths"
        );
        assert!(
            !first_text.contains("timestamp") && !first_text.contains("random"),
            "{artifact} must not contain volatility marker text"
        );
    }

    let plan = fs::read_to_string(first.join("app.plan.json")).expect("plan should exist");
    assert!(
        plan.contains("\"id\": 1") && plan.contains("\"handlerId\": 1"),
        "MAIN hello handler IDs must remain stable"
    );

    fs::remove_dir_all(&base).expect("test output directory should be removable");
}
