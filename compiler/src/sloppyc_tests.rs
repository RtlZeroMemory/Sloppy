use std::{
    collections::{BTreeMap, BTreeSet},
    ffi::OsString,
    fs,
    path::{Path, PathBuf},
};

use oxc_allocator::Allocator;
use oxc_ast::ast::{Expression, Statement};
use oxc_parser::Parser;
use oxc_span::SourceType;

use super::{
    arrow_requires_results_import, canonical_config_key, checksum_security_context_visible,
    command_from_args, config_key_is_diagnostic_sensitive, config_key_is_sensitive, extract,
    help_text, noncrypto_hash_security_context_visible, redact_config_value,
    route_pattern_supported, CliCommand, CompileOptions, ConfigurationModel,
    WebSocketOriginsMetadata,
};

fn fixture_temp_dir(name: &str) -> PathBuf {
    let root = std::env::temp_dir().join(format!("sloppyc-{name}-{}", std::process::id()));
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale test directory should be removable");
    }
    fs::create_dir_all(&root).expect("test directory should be created");
    root
}

fn extract_temp_input(root: &Path, source: &str) -> Result<super::ExtractedApp, super::Diagnostic> {
    let input = root.join("input.js");
    fs::write(&input, source).expect("fixture input should be writable");
    extract(&input, source)
}

fn parsed_arrow_requires_results_import(handler_source: &str) -> bool {
    let allocator = Allocator::default();
    let source = format!("const handler = {handler_source};");
    let parsed = Parser::new(&allocator, &source, SourceType::mjs()).parse();
    assert!(
        parsed.errors.is_empty(),
        "handler fixture should parse: {:?}",
        parsed.errors
    );
    let Statement::VariableDeclaration(declaration) = &parsed.program.body[0] else {
        panic!("fixture should declare a handler");
    };
    let init = declaration.declarations[0]
        .init
        .as_ref()
        .expect("handler declaration should have an initializer");
    let Expression::ArrowFunctionExpression(function) = init else {
        panic!("handler fixture should be an arrow function");
    };
    arrow_requires_results_import(function)
}

#[test]
fn no_argument_prints_help() {
    assert_eq!(command_from_args(Vec::<OsString>::new()), CliCommand::Help);
}

#[test]
fn version_flag_prints_version() {
    assert_eq!(
        command_from_args([OsString::from("--version")]),
        CliCommand::Version
    );
}

#[test]
fn build_requires_input_and_output() {
    assert_eq!(
        command_from_args([OsString::from("build")]),
        CliCommand::Invalid("build requires an input file".to_string())
    );
}

#[test]
fn build_args_accept_environment_and_runtime_overrides() {
    assert_eq!(
        command_from_args([
            OsString::from("build"),
            OsString::from("app.js"),
            OsString::from("--out"),
            OsString::from(".sloppy"),
            OsString::from("--environment"),
            OsString::from("Development"),
            OsString::from("--host"),
            OsString::from("127.0.0.1"),
            OsString::from("--port"),
            OsString::from("5173"),
            OsString::from("--config"),
            OsString::from("Auth:Issuer=cli"),
        ]),
        CliCommand::Build {
            input: std::path::PathBuf::from("app.js"),
            out_dir: std::path::PathBuf::from(".sloppy"),
            options: Box::new(CompileOptions {
                kind: None,
                environment: Some("Development".to_string()),
                host: Some("127.0.0.1".to_string()),
                port: Some(5173),
                config_dir: None,
                config_overrides: vec![("Auth:Issuer".to_string(), "cli".to_string())],
                declared_capabilities: Vec::new(),
                declared_capabilities_from_sloppy_json: false,
                module_include: Vec::new(),
                asset_include: Vec::new(),
                timings_json: None,
            }),
        }
    );
}

#[test]
fn build_args_accept_timings_json_output() {
    assert_eq!(
        command_from_args([
            OsString::from("build"),
            OsString::from("app.js"),
            OsString::from("--out"),
            OsString::from(".sloppy"),
            OsString::from("--timings-json"),
            OsString::from("artifacts/bench/timings.json"),
        ]),
        CliCommand::Build {
            input: std::path::PathBuf::from("app.js"),
            out_dir: std::path::PathBuf::from(".sloppy"),
            options: Box::new(CompileOptions {
                timings_json: Some(std::path::PathBuf::from("artifacts/bench/timings.json")),
                ..CompileOptions::default()
            }),
        }
    );
}

#[test]
fn build_args_accept_project_kind() {
    assert_eq!(
        command_from_args([
            OsString::from("build"),
            OsString::from("main.ts"),
            OsString::from("--out"),
            OsString::from(".sloppy"),
            OsString::from("--kind"),
            OsString::from("program"),
        ]),
        CliCommand::Build {
            input: std::path::PathBuf::from("main.ts"),
            out_dir: std::path::PathBuf::from(".sloppy"),
            options: Box::new(CompileOptions {
                kind: Some(super::ProjectKind::Program),
                ..CompileOptions::default()
            }),
        }
    );
}

#[test]
fn build_args_accept_declared_capability_handoff() {
    assert_eq!(
        command_from_args([
            OsString::from("build"),
            OsString::from("main.ts"),
            OsString::from("--out"),
            OsString::from(".sloppy"),
            OsString::from("--capability"),
            OsString::from("fs"),
        ]),
        CliCommand::Build {
            input: std::path::PathBuf::from("main.ts"),
            out_dir: std::path::PathBuf::from(".sloppy"),
            options: Box::new(CompileOptions {
                declared_capabilities: vec!["fs".to_string()],
                ..CompileOptions::default()
            }),
        }
    );
}

#[test]
fn build_args_accept_sloppy_json_capability_origin_handoff() {
    assert_eq!(
        command_from_args([
            OsString::from("build"),
            OsString::from("main.ts"),
            OsString::from("--out"),
            OsString::from(".sloppy"),
            OsString::from("--capability-origin"),
            OsString::from("sloppy.json"),
            OsString::from("--capability"),
            OsString::from("fs"),
        ]),
        CliCommand::Build {
            input: std::path::PathBuf::from("main.ts"),
            out_dir: std::path::PathBuf::from(".sloppy"),
            options: Box::new(CompileOptions {
                declared_capabilities: vec!["fs".to_string()],
                declared_capabilities_from_sloppy_json: true,
                ..CompileOptions::default()
            }),
        }
    );
}

#[test]
fn build_args_accept_module_and_asset_includes() {
    assert_eq!(
        command_from_args([
            OsString::from("build"),
            OsString::from("main.ts"),
            OsString::from("--out"),
            OsString::from(".sloppy"),
            OsString::from("--module-include"),
            OsString::from("plugins/**/*.js"),
            OsString::from("--asset-include"),
            OsString::from("assets/**/*"),
        ]),
        CliCommand::Build {
            input: std::path::PathBuf::from("main.ts"),
            out_dir: std::path::PathBuf::from(".sloppy"),
            options: Box::new(CompileOptions {
                module_include: vec!["plugins/**/*.js".to_string()],
                asset_include: vec!["assets/**/*".to_string()],
                ..CompileOptions::default()
            }),
        }
    );
}

include!("sloppyc_tests/program_mode.rs");

include!("sloppyc_tests/assets_and_runtime_imports.rs");

#[test]
fn help_text_lists_diagnostics_timing_json_alias() {
    let help = help_text();
    assert!(help.contains("--timings-json|--diagnostics-timing-json <file>"));
}

#[test]
fn module_graph_root_level_entry_uses_current_directory_as_source_root() {
    let graph = super::ModuleGraph::new(Path::new("app.js"), None);
    let current_dir =
        fs::canonicalize(std::env::current_dir().expect("current directory should exist"))
            .expect("current directory should canonicalize");
    assert_eq!(graph.entry_dir, current_dir);
}

#[test]
fn keep_alive_environment_override_keys_are_canonicalized() {
    assert_eq!(
        canonical_config_key("SLOPPY:SERVER:KEEPALIVEENABLED"),
        "Sloppy:Server:KeepAliveEnabled"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:SERVER:KEEPALIVEIDLETIMEOUTMS"),
        "Sloppy:Server:KeepAliveIdleTimeoutMs"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:SERVER:MAXREQUESTSPERCONNECTION"),
        "Sloppy:Server:MaxRequestsPerConnection"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:SERVER:TLS:PRIVATEKEYPATH"),
        "Sloppy:Server:Tls:PrivateKeyPath"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:SERVER:TLS:CERTIFICATEPATH"),
        "Sloppy:Server:Tls:CertificatePath"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:SERVER:TLS:PASSPHRASE"),
        "Sloppy:Server:Tls:Passphrase"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:LOGGING:MINIMUMLEVEL"),
        "Sloppy:Logging:MinimumLevel"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:LOGGING:CONSOLE:FORMAT"),
        "Sloppy:Logging:Console:Format"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:LOGGING:FILE:PATH"),
        "Sloppy:Logging:File:Path"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:PROVIDERS:POSTGRES:MAIN:CONNECTIONSTRING"),
        "Sloppy:Providers:postgres:MAIN:connectionString"
    );
    assert_eq!(
        canonical_config_key("AUTH:CLIENTSECRET"),
        "AUTH:clientSecret"
    );
    assert_eq!(canonical_config_key("AUTH:PRIVATEKEY"), "AUTH:privateKey");
}

#[test]
fn configuration_files_overlay_and_bind_sqlite_provider() {
    let root = std::env::temp_dir().join(format!("sloppyc-config-test-{}", std::process::id()));
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale config test directory should be removable");
    }
    fs::create_dir_all(&root).expect("config test directory should be created");
    let input = root.join("app.js");
    fs::write(&input, "export default {};").expect("input should be written");
    fs::write(
            root.join("appsettings.json"),
            r#"{"Sloppy":{"Server":{"Port":5000},"Providers":{"sqlite":{"main":{"database":"base.db"}}}}}"#,
        )
        .expect("base appsettings should be written");
    fs::write(
            root.join("appsettings.Development.json"),
            r#"{"Sloppy":{"Server":{"Port":5173},"Providers":{"sqlite":{"main":{"database":"dev.db"}}}}}"#,
        )
        .expect("environment appsettings should be written");

    let options = CompileOptions {
        kind: None,
        environment: Some("Development".to_string()),
        host: Some("0.0.0.0".to_string()),
        port: Some(6000),
        config_dir: None,
        config_overrides: Vec::new(),
        declared_capabilities: Vec::new(),
        declared_capabilities_from_sloppy_json: false,
        module_include: Vec::new(),
        asset_include: Vec::new(),
        timings_json: None,
    };
    let config =
        super::ConfigurationModel::load(&input, &options, &[]).expect("configuration should load");
    assert_eq!(
        config
            .get_string("Sloppy:Providers:sqlite:main:database")
            .expect("database key should be string"),
        Some("dev.db".to_string())
    );
    assert_eq!(
        &config
            .get("Sloppy:Server:Port")
            .expect("port should exist")
            .value,
        &serde_json::json!(6000)
    );
    assert_eq!(
        &config
            .get("Sloppy:Server:Host")
            .expect("host should exist")
            .value,
        &serde_json::json!("0.0.0.0")
    );

    let mut app = super::ExtractedApp {
        kind: super::ProjectKind::Web,
        program_entry: None,
        program_modules: Vec::new(),
        uses_data_runtime: true,
        uses_sql_runtime: false,
        uses_orm_runtime: false,
        orm_tables: Vec::new(),
        orm_relations: Vec::new(),
        orm_extraction_partial: false,
        uses_migrations_runtime: false,
        uses_provider_health_runtime: false,
        uses_redis_runtime: false,
        source_files: Vec::new(),
        routes: Vec::new(),
        dynamic_routes: Vec::new(),
        dynamic_entry_source: None,
        auth: super::AuthMetadata::default(),
        service_registrations: Vec::new(),
        modules: Vec::new(),
        helper_sources: Vec::new(),
        capabilities: vec![super::DatabaseCapability {
            token: "data.main".to_string(),
            capability_kind: "database".to_string(),
            provider: "sqlite".to_string(),
            config_name: Some("main".to_string()),
            config_key: None,
            access: "readwrite".to_string(),
            database: None,
            config_source: None,
            source_name: "app.js".to_string(),
            source: String::new(),
            span: super::Span::new(0, 0),
            from_provider_use: true,
        }],
        configuration: None,
        schemas: Vec::new(),
        config_reads: Vec::new(),
        uses_time_runtime: false,
        uses_fs_runtime: false,
        uses_crypto_runtime: false,
        noncrypto_hash_security_context_visible: false,
        uses_codec_runtime: false,
        checksum_security_context_visible: false,
        uses_cache_runtime: false,
        uses_net_runtime: false,
        uses_os_runtime: false,
        uses_http_client_runtime: false,
        uses_webhooks_runtime: false,
        uses_realtime_runtime: false,
        uses_workers_runtime: false,
        uses_ffi_runtime: false,
        ffi: Vec::new(),
        ffi_structs: Vec::new(),
        uses_health: false,
        problem_details: None,
        dependency_graph: super::DependencyGraph::default(),
    };
    config
        .apply_to_app(&mut app)
        .expect("provider config should bind");
    assert_eq!(app.capabilities[0].database.as_deref(), Some("dev.db"));
    assert!(app.configuration.is_some());

    fs::remove_dir_all(&root).expect("config test directory should be removable");
}

#[test]
fn configuration_precedence_includes_local_secrets_env_and_cli_overrides() {
    let root = fixture_temp_dir("config-precedence");
    let input = root.join("app.js");
    fs::write(
        &input,
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const jwt = app.config.getSecret("Auth:JwtSecret");
const issuer = app.config.getString("Auth:Issuer");
app.get("/", () => Results.text("ok"));
export default app;
"#,
    )
    .expect("input should be written");
    fs::write(
        root.join("appsettings.json"),
        r#"{"Auth":{"Issuer":"base","JwtSecret":"base-value"}}"#,
    )
    .expect("base appsettings should be written");
    fs::write(
        root.join("appsettings.Development.json"),
        r#"{"Auth":{"Issuer":"environment"}}"#,
    )
    .expect("environment appsettings should be written");
    fs::write(
        root.join("appsettings.local.json"),
        r#"{"Auth":{"Issuer":"local"}}"#,
    )
    .expect("local appsettings should be written");
    fs::write(
        root.join("appsettings.Development.local.json"),
        r#"{"Auth":{"Issuer":"environment-local"}}"#,
    )
    .expect("environment local appsettings should be written");
    fs::create_dir_all(root.join(".sloppy")).expect("secret directory should be created");
    fs::write(
        root.join(".sloppy").join("secrets.json"),
        r#"{"Auth":{"JwtSecret":"store-value"}}"#,
    )
    .expect("user secrets should be written");

    std::env::set_var("Auth__JwtSecret", "env-value");
    let mut app = extract(
        &input,
        &fs::read_to_string(&input).expect("source should read"),
    )
    .expect("config app should extract");
    let options = CompileOptions {
        kind: None,
        environment: Some("Development".to_string()),
        host: None,
        port: None,
        config_dir: None,
        config_overrides: vec![("Auth:Issuer".to_string(), "cli".to_string())],
        declared_capabilities: Vec::new(),
        declared_capabilities_from_sloppy_json: false,
        module_include: Vec::new(),
        asset_include: Vec::new(),
        timings_json: None,
    };
    let config = super::ConfigurationModel::load(&input, &options, &app.config_reads)
        .expect("configuration should load");
    assert_eq!(
        config
            .get_string("Auth:JwtSecret")
            .expect("secret key should be string"),
        Some("env-value".to_string())
    );
    assert_eq!(
        config
            .get_string("Auth:Issuer")
            .expect("issuer key should be string"),
        Some("cli".to_string())
    );
    config
        .apply_to_app(&mut app)
        .expect("configuration should apply");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("Results"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    assert!(!plan.contains("env-value"));
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert!(plan["configuration"]["requirements"]
        .as_array()
        .expect("requirements should be an array")
        .iter()
        .any(|requirement| requirement["key"] == "Auth:JwtSecret"
            && requirement["status"] == "present"
            && requirement["redaction"] == "secret"));
    assert!(plan["configuration"]["packageManifest"]["required"]
        .as_array()
        .expect("required manifest should be an array")
        .iter()
        .any(|entry| entry["env"] == "Auth__JwtSecret" && entry["secret"] == true));
    std::env::remove_var("Auth__JwtSecret");
    fs::remove_dir_all(&root).expect("config test directory should be removable");
}

#[test]
fn config_bind_descriptors_emit_required_optional_and_secret_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const auth = app.config.bind("Auth", {
  jwtSecret: "secret",
  tokenTtlMinutes: { type: "number", default: 60, min: 1, max: 1440 },
  claims: { type: "object", default: { issuer: "local" } },
  issuer: { key: "Jwt:Issuer", type: "string", required: true }
});
app.get("/", () => Results.json({
  claims: auth.claims,
  issuer: auth.issuer,
  tokenTtlMinutes: auth.tokenTtlMinutes
}));
export default app;
"#;
    let mut app =
        extract(std::path::Path::new("app.js"), source).expect("bind descriptors should extract");
    assert_eq!(app.config_reads.len(), 4);
    assert!(app
        .config_reads
        .iter()
        .any(|read| { read.key == "Auth:JwtSecret" && read.sensitive && read.required }));
    assert!(app
        .config_reads
        .iter()
        .any(|read| { read.key == "Auth:TokenTtlMinutes" && read.has_default && !read.required }));
    assert!(app
        .config_reads
        .iter()
        .any(|read| read.key == "Auth:Jwt:Issuer" && !read.sensitive && read.required));
    let config = super::ConfigurationModel {
        environment: "Development".to_string(),
        values: std::collections::BTreeMap::new(),
    };
    config
        .apply_to_app(&mut app)
        .expect("bind metadata should apply without requiring dev values");
    let emitted_js = super::emit_app_js(&app);
    let plain_object_offset = emitted_js
        .source
        .find("function __sloppy_is_plain_object")
        .expect("config object bindings should emit plain-object helper");
    let config_read_offset = emitted_js
        .source
        .find("function __sloppy_config_read")
        .expect("config bind helper should emit config read");
    assert!(plain_object_offset < config_read_offset);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    let requirements = plan["configuration"]["requirements"]
        .as_array()
        .expect("requirements should be an array");
    assert!(requirements
        .iter()
        .any(|requirement| requirement["key"] == "Auth:JwtSecret"
            && requirement["status"] == "missing"
            && requirement["secret"] == true));
    assert!(requirements
        .iter()
        .any(|requirement| requirement["key"] == "Auth:Jwt:Issuer"
            && requirement["status"] == "missing"
            && requirement["secret"] == false));
    assert!(plan["configuration"]["packageManifest"]["optional"]
        .as_array()
        .expect("optional manifest should be an array")
        .iter()
        .any(|entry| entry["key"] == "Auth:TokenTtlMinutes"
            && entry["default"].as_f64() == Some(60.0)));
    assert!(plan["configuration"]["packageManifest"]["optional"]
        .as_array()
        .expect("optional manifest should be an array")
        .iter()
        .any(|entry| entry["key"] == "Auth:Claims" && entry["default"]["issuer"] == "local"));
}

#[test]
fn provider_config_preserves_declared_name_for_dotted_sqlite_names() {
    let root =
        std::env::temp_dir().join(format!("sloppyc-config-dotted-test-{}", std::process::id()));
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale config test directory should be removable");
    }
    fs::create_dir_all(&root).expect("config test directory should be created");
    let input = root.join("app.js");
    fs::write(
        &input,
        r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("data.main"));
app.mapGet("/", () => Results.text("ok"));
export default app;
"#,
    )
    .expect("input should be written");
    fs::write(
            root.join("appsettings.json"),
            r#"{"Sloppy":{"Providers":{"sqlite":{"data.main":{"database":"dotted.db"},"main":{"database":"wrong.db"}}}}}"#,
        )
        .expect("appsettings should be written");
    let out_dir = root.join(".sloppy");

    super::build(&input, &out_dir, &CompileOptions::new())
        .expect("dotted provider config should bind");
    let plan = fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should exist");
    assert!(plan.contains("\"database\": \"dotted.db\""));
    assert!(plan.contains("\"prefix\": \"Sloppy:Providers:sqlite:data.main\""));
    assert!(!plan.contains("\"database\": \"wrong.db\""));

    fs::remove_dir_all(&root).expect("config test directory should be removable");
}

#[test]
fn repeated_sqlite_provider_use_keeps_latest_declaration() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main"));
app.use(sqlite("main", { database: ":memory:" }));
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let mut app = extract(std::path::Path::new("app.js"), source)
        .expect("repeated provider use should extract");
    assert_eq!(app.capabilities.len(), 1);
    assert_eq!(app.capabilities[0].database.as_deref(), Some(":memory:"));
    let config = super::ConfigurationModel {
        environment: "Development".to_string(),
        values: std::collections::BTreeMap::new(),
    };
    config
        .apply_to_app(&mut app)
        .expect("latest inline provider declaration should not require config");
}

#[test]
fn configuration_plan_redacts_sensitive_values() {
    let mut config = super::ConfigurationModel {
        environment: "Development".to_string(),
        values: std::collections::BTreeMap::new(),
    };
    config.set(
        "Sloppy:Providers:sqlite:main:password",
        serde_json::json!("secret"),
        "test",
    );
    let keys = config.plan_keys();
    assert_eq!(keys.len(), 1);
    assert!(keys[0].sensitive);
    assert_eq!(keys[0].value, serde_json::json!("<redacted>"));
    assert!(!keys[0].value.to_string().contains("secret"));
}

#[test]
fn configuration_plan_redacts_pwd_alias_values() {
    let mut config = super::ConfigurationModel {
        environment: "Development".to_string(),
        values: std::collections::BTreeMap::new(),
    };
    config.set(
        "Sloppy:Providers:sqlite:main:Pwd",
        serde_json::json!("secret"),
        "test",
    );
    let keys = config.plan_keys();
    assert_eq!(keys.len(), 1);
    assert!(keys[0].sensitive);
    assert_eq!(keys[0].value, serde_json::json!("<redacted>"));
    assert!(!keys[0].value.to_string().contains("secret"));
}

#[test]
fn configuration_plan_keeps_tls_paths_for_runtime_but_redacts_diagnostic_hints() {
    let mut config = super::ConfigurationModel {
        environment: "Development".to_string(),
        values: std::collections::BTreeMap::new(),
    };
    config.set(
        "Sloppy:Server:Tls:CertificatePath",
        serde_json::json!("certs/server.crt"),
        "test",
    );
    config.set(
        "Sloppy:Server:Tls:PrivateKeyPath",
        serde_json::json!("C:/keys/server.key"),
        "test",
    );
    config.set(
        "Sloppy:Server:Tls:Passphrase",
        serde_json::json!("secret"),
        "test",
    );
    let keys = config.plan_keys();
    assert_eq!(keys.len(), 3);
    let certificate_path = keys
        .iter()
        .find(|key| key.key == "Sloppy:Server:Tls:CertificatePath")
        .expect("certificate path should be present");
    let key_path = keys
        .iter()
        .find(|key| key.key == "Sloppy:Server:Tls:PrivateKeyPath")
        .expect("private key path should be present");
    let passphrase = keys
        .iter()
        .find(|key| key.key == "Sloppy:Server:Tls:Passphrase")
        .expect("passphrase should be present");
    assert!(!certificate_path.sensitive);
    assert_eq!(
        certificate_path.value,
        serde_json::json!("certs/server.crt")
    );
    assert!(!key_path.sensitive);
    assert_eq!(key_path.value, serde_json::json!("C:/keys/server.key"));
    assert!(passphrase.sensitive);
    assert_eq!(passphrase.value, serde_json::json!("<redacted>"));
    assert!(!keys
        .iter()
        .any(|key| key.value.to_string().contains("secret")));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:Server:Tls:CertificatePath"
    ));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:Server:Tls:PrivateKeyPath"
    ));
    assert_eq!(
        redact_config_value("Sloppy:Server:Tls:CertificatePath", "certs/server.crt"),
        "<redacted>"
    );
    assert_eq!(
        redact_config_value("Sloppy:Server:Tls:PrivateKeyPath", "C:/keys/server.key"),
        "<redacted>"
    );
}

#[test]
fn configuration_load_anchors_tls_paths_to_config_dir() {
    let root = fixture_temp_dir("config-tls-path-root");
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale config test directory should be removable");
    }
    fs::create_dir_all(&root).expect("config test directory should be created");
    fs::write(
        root.join("appsettings.json"),
        r#"{
  "Sloppy": {
    "Server": {
      "Tls": {
        "CertificatePath": "certs/dev.crt",
        "PrivateKeyPath": "keys/dev.key"
      }
    }
  }
}"#,
    )
    .expect("appsettings should be written");

    let options = CompileOptions {
        config_dir: Some(root.clone()),
        ..Default::default()
    };
    let model = ConfigurationModel::load(&root.join("src/main.ts"), &options, &[])
        .expect("configuration should load");
    let keys = model.plan_keys();
    let certificate_path = keys
        .iter()
        .find(|key| key.key == "Sloppy:Server:Tls:CertificatePath")
        .and_then(|key| key.value.as_str())
        .expect("certificate path should be present");
    let private_key_path = keys
        .iter()
        .find(|key| key.key == "Sloppy:Server:Tls:PrivateKeyPath")
        .and_then(|key| key.value.as_str())
        .expect("private key path should be present");

    assert!(Path::new(certificate_path).is_absolute());
    assert!(Path::new(private_key_path).is_absolute());
    assert!(
        certificate_path.ends_with("certs/dev.crt") || certificate_path.ends_with("certs\\dev.crt")
    );
    assert!(
        private_key_path.ends_with("keys/dev.key") || private_key_path.ends_with("keys\\dev.key")
    );

    fs::remove_dir_all(&root).expect("config test directory should be cleaned up");
}

#[test]
fn configuration_key_sensitivity_covers_alpha_secret_aliases() {
    assert!(config_key_is_sensitive("Auth:apiKey"));
    assert!(config_key_is_sensitive("Auth:clientSecret"));
    assert!(config_key_is_sensitive("Auth:privateKey"));
    assert!(config_key_is_sensitive(
        "Sloppy:Providers:postgres:main:connectionString"
    ));
    assert!(!config_key_is_sensitive("Sloppy:Server:Tls:PrivateKeyPath"));
    assert!(!config_key_is_sensitive(
        "Sloppy:HttpClient:Tls:ClientCaPath"
    ));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:HttpClient:Tls:ClientCertificatePath"
    ));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:HttpClient:Tls:ClientPrivateKeyPath"
    ));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:HttpClient:Tls:ClientCaPath"
    ));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:HttpClient:Tls:CaBundlePath"
    ));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:HttpClient:Tls:TrustStorePath"
    ));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:HttpClient:Tls:CaPath"
    ));
}

#[test]
fn configuration_json_rejects_empty_key_segments() {
    let root =
        std::env::temp_dir().join(format!("sloppyc-config-empty-key-{}", std::process::id()));
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale config test directory should be removable");
    }
    fs::create_dir_all(&root).expect("config test directory should be created");
    let input = root.join("app.js");
    fs::write(&input, "export default {};").expect("input should be written");
    fs::write(
        root.join("appsettings.json"),
        r#"{"Sloppy":{"Server":{"":5173}}}"#,
    )
    .expect("appsettings should be written");

    let diagnostic = super::ConfigurationModel::load(&input, &super::CompileOptions::new(), &[])
        .expect_err("empty config key segment should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_CONFIG_KEY");
    assert!(
        diagnostic.message.contains("empty config key segment"),
        "{}",
        diagnostic.message
    );

    fs::remove_dir_all(&root).expect("config test directory should be removable");
}

#[test]
fn extracts_literal_map_get() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("Hello"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/");
}

#[test]
fn extracts_minimal_api_methods() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/get", () => Results.text("get"));
app.post("/post", () => Results.text("post"));
app.put("/put", () => Results.text("put"));
app.patch("/patch", () => Results.text("patch"));
app.delete("/delete", () => Results.text("delete"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    let routes = app
        .routes
        .iter()
        .map(|route| (route.method, route.pattern.as_str()))
        .collect::<Vec<_>>();
    assert_eq!(
        routes,
        [
            ("GET", "/get"),
            ("POST", "/post"),
            ("PUT", "/put"),
            ("PATCH", "/patch"),
            ("DELETE", "/delete"),
        ]
    );
}

#[test]
fn rejects_unsupported_direct_http_methods_explicitly() {
    for method in ["head", "options"] {
        let source = format!(
            r#"import {{ Sloppy, Results }} from "sloppy";
const app = Sloppy.create();
app.{method}("/", () => Results.text("unsupported"));
export default app;
"#
        );
        let diagnostic = extract(std::path::Path::new("app.js"), &source)
            .expect_err("unsupported direct HTTP method should fail");
        assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HTTP_METHOD");
    }
}

#[test]
fn extracts_nested_route_groups() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const api = app.group("/api");
const users = api.group("/users");
users.get("/{id:int}", () => Results.json({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].method, "GET");
    assert_eq!(app.routes[0].pattern, "/api/users/{id:int}");
}

#[test]
fn typed_framework_route_bindings_use_full_grouped_pattern() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const users = app.group("/users/:userId");
users.get("/posts/:postId", (userId: number, postId: number) => Results.ok({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed grouped route should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/users/{userId}/posts/{postId}");
    assert_eq!(
        app.routes[0].framework_path.as_deref(),
        Some("/users/:userId/posts/:postId")
    );
    let bindings = app.routes[0]
        .handler
        .bindings
        .iter()
        .map(|binding| (binding.kind.as_str(), binding.name.as_deref()))
        .collect::<Vec<_>>();
    assert_eq!(
        bindings,
        [("route", Some("userId")), ("route", Some("postId")),]
    );
}

#[test]
fn typed_framework_colon_route_type_suffix_binds_name() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/users/:id:int", (id: number) => Results.ok({ id }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed colon route with type suffix should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/users/{id:int}");
    let bindings = app.routes[0]
        .handler
        .bindings
        .iter()
        .map(|binding| (binding.kind.as_str(), binding.name.as_deref()))
        .collect::<Vec<_>>();
    assert_eq!(bindings, [("route", Some("id"))]);
}

#[test]
fn extracts_direct_and_nested_function_module_routes() {
    let root = fixture_temp_dir("function-module-routes");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    app.get("/module-health", () => Results.text("ok"));
    const api = app.group("/api");
    const users = api.group("/users");
    users.post("/", () => Results.json({ ok: true }));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { usersModule } from "./modules/users.js";

const app = Sloppy.create();
app.useModule(usersModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert_eq!(app.modules.len(), 1);
    assert_eq!(app.modules[0].name, "usersModule");
    let routes = app
        .routes
        .iter()
        .map(|route| {
            (
                route.method,
                route.pattern.as_str(),
                route.module.as_deref(),
            )
        })
        .collect::<Vec<_>>();
    assert_eq!(
        routes,
        [
            ("GET", "/health", None),
            ("GET", "/module-health", Some("usersModule")),
            ("POST", "/api/users", Some("usersModule")),
        ]
    );

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    assert!(plan.contains("\"module\": \"usersModule\""));
    assert!(plan.contains("\"path\": \"users.js\""));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn protected_function_module_param_routes_request_full_context() {
    let root = fixture_temp_dir("function-module-auth-param-context");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("tickets.js"),
        r#"import { Results } from "sloppy";

export function ticketsModule(app) {
    const tickets = app.group("/tickets").requiresAuth();
    tickets.get("/{id:int}", (ctx) => Results.json({ id: ctx.route.id }));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Auth, Config, Sloppy } from "sloppy";
import { ticketsModule } from "./modules/tickets.js";

const app = Sloppy.create();
app.use(Auth.apiKey({ configKey: "Auth:ApiKey" }));
app.useModule(ticketsModule);
export default app;
"#;

    let app = extract_temp_input(&root, source).expect("fixture should extract");
    let route = app
        .routes
        .iter()
        .find(|route| route.pattern == "/tickets/{id:int}")
        .expect("module param route should extract");
    assert!(route.auth.as_ref().expect("auth should inherit").required);
    assert!(route
        .handler
        .bindings
        .iter()
        .any(|binding| binding.kind == "route" && binding.name.as_deref() == Some("id")));
    assert!(route
        .handler
        .bindings
        .iter()
        .any(|binding| binding.kind == "context"));

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("__sloppy_require_auth"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("plan should be valid json");
    let bindings = value["routes"][0]["bindings"]
        .as_array()
        .expect("route bindings should be emitted");
    assert!(bindings.iter().any(|binding| binding["kind"] == "route"));
    assert!(bindings.iter().any(|binding| binding["kind"] == "context"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_exports_local_schema_metadata() {
    let root = fixture_temp_dir("function-module-local-schema-metadata");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"import { Results, Schema } from "sloppy";

export function usersModule(app) {
    const CreateUser = Schema.object({
        name: Schema.string().min(1),
    });
    const User = Schema.object({
        id: Schema.integer(),
        name: Schema.string(),
    });

    app.post("/users", async (ctx) =>
        Results.created("/users/1", await ctx.body.validate(CreateUser))
    ).accepts(CreateUser).returns(User).withName("Users.Create");
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { usersModule } from "./modules/users.js";

const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("module schemas should extract");
    assert_eq!(app.schemas.len(), 2);
    let route = &app.routes[0];
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

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_duplicate_schema_names_fail_closed() {
    let root = fixture_temp_dir("function-module-duplicate-schema");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"import { Results, Schema } from "sloppy";

export function usersModule(app) {
    const User = Schema.object({ id: Schema.integer() });
    app.get("/users", () => Results.ok({ id: 1 })).returns(User);
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Schema } from "sloppy";
import { usersModule } from "./modules/users.js";

const User = Schema.object({ id: Schema.integer() });
const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let diagnostic =
        extract_temp_input(&root, source).expect_err("duplicate module schema should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_DUPLICATE_SCHEMA");

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_can_register_health_checks() {
    let root = fixture_temp_dir("function-module-health");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("health.js"),
        r#"export function healthModule(app) {
    app.mapHealthChecks({
        path: "/health",
        livenessPath: "/health/live",
        readinessPath: "/health/ready",
        checks: [
            { name: "database", readiness: true, check: () => true },
        ],
    });
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { healthModule } from "./modules/health.js";

const app = Sloppy.create();
app.useModule(healthModule);
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("module health routes should extract");
    let routes = app
        .routes
        .iter()
        .map(|route| {
            (
                route.method,
                route.pattern.as_str(),
                route.module.as_deref(),
            )
        })
        .collect::<Vec<_>>();
    assert_eq!(
        routes,
        [
            ("GET", "/health", Some("healthModule")),
            ("GET", "/health/live", Some("healthModule")),
            ("GET", "/health/ready", Some("healthModule")),
        ]
    );
    assert!(app.uses_health);

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_provider_handlers_can_use_local_helpers() {
    let root = fixture_temp_dir("function-module-provider-helpers");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    const db = app.provider("sqlite:main");

    function migrateUsers() {
        db.exec("create table if not exists users (id integer primary key, name text not null)", []);
    }

    function listUsers() {
        migrateUsers();
        return db.query("select id, name from users order by id", []);
    }

    app.get("/users", () => {
        return Results.ok(listUsers());
    }).withName("Users.List");
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { usersModule } from "./modules/users.js";

const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
app.useModule(usersModule);
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("module provider helpers should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/users");
    assert_eq!(app.routes[0].module.as_deref(), Some("usersModule"));
    assert_eq!(app.routes[0].handler.effects.len(), 2);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("function migrateUsers()"));
    assert!(emitted_js.source.contains("function listUsers()"));
    assert!(emitted_js.source.contains("__sloppy_open_data_provider"));
    assert!(emitted_js
        .source
        .contains("return Results.ok(listUsers());"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_still_rejects_multistatement_handlers_without_runtime_effects() {
    let root = fixture_temp_dir("function-module-multistatement-no-effects");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    app.get("/users", () => {
        const users = [{ id: 1 }];
        return Results.ok(users);
    });
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { usersModule } from "./modules/users.js";

const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let diagnostic = extract_temp_input(&root, source)
        .expect_err("module handler without runtime effects should stay bounded");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HANDLER");
    assert!(diagnostic
        .path
        .as_deref()
        .is_some_and(|path| path.ends_with("modules/users.js")));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn typed_function_module_route_bindings_use_full_grouped_pattern() {
    let root = fixture_temp_dir("function-module-typed-groups");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.ts"),
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    const users = app.group("/users/:userId");
    users.get("/posts/:postId", (userId: number, postId: number) => Results.ok({ ok: true }));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { usersModule } from "./modules/users.ts";

const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let app = extract_temp_input(&root, source)
        .expect("typed grouped function module route should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/users/{userId}/posts/{postId}");
    assert_eq!(app.routes[0].module.as_deref(), Some("usersModule"));
    let bindings = app.routes[0]
        .handler
        .bindings
        .iter()
        .map(|binding| (binding.kind.as_str(), binding.name.as_deref()))
        .collect::<Vec<_>>();
    assert_eq!(
        bindings,
        [("route", Some("userId")), ("route", Some("postId")),]
    );

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_sloppy_time_import_emits_required_feature() {
    let root = fixture_temp_dir("function-module-time-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("jobs.js"),
        r#"import { Results } from "sloppy";
import { Time, Deadline } from "sloppy/time";

export function jobsModule(app) {
    app.get("/jobs", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { jobsModule } from "./modules/jobs.js";

const app = Sloppy.create();
app.useModule(jobsModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert!(app.uses_time_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("Time, Deadline"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["requiredFeatures"], serde_json::json!(["stdlib.time"]));
    assert_eq!(plan["features"]["time"], serde_json::json!(true));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_sloppy_net_import_emits_required_feature() {
    let root = fixture_temp_dir("function-module-net-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("tcp.js"),
        r#"import { Results } from "sloppy";
import { TcpClient, TcpConnection } from "sloppy/net";

export function tcpModule(app) {
    app.get("/tcp", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { tcpModule } from "./modules/tcp.js";

const app = Sloppy.create();
app.useModule(tcpModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert!(app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("TcpClient"));
    assert!(emitted_js.source.contains("TcpConnection"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["requiredFeatures"], serde_json::json!(["stdlib.net"]));
    assert_eq!(plan["features"]["network"], serde_json::json!(true));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_sloppy_net_http_client_import_emits_http_client_required_feature() {
    let root = fixture_temp_dir("function-module-http-client-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("billing.js"),
        r#"import { Results } from "sloppy";
import { HttpClient } from "sloppy/net";

export function billingModule(app) {
    app.get("/billing", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { billingModule } from "./modules/billing.js";

const app = Sloppy.create();
app.useModule(billingModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert!(app.uses_http_client_runtime);
    assert!(!app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("HttpClient"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(
        plan["requiredFeatures"],
        serde_json::json!(["stdlib.httpclient"])
    );
    assert_eq!(plan["features"]["httpClient"], serde_json::json!(true));
    assert_eq!(
        plan["strongPlan"]["evidence"]["httpClient"],
        serde_json::json!(true)
    );
    assert_eq!(
        plan["doctorChecks"][0]["id"],
        serde_json::json!("stdlib.httpclient.contract")
    );

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_sloppy_workers_import_emits_required_feature() {
    let root = fixture_temp_dir("function-module-workers-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("jobs.js"),
        r#"import { Results } from "sloppy";
import { WorkerPool } from "sloppy/workers";

export function jobsModule(app) {
    app.get("/jobs", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { jobsModule } from "./modules/jobs.js";

const app = Sloppy.create();
app.useModule(jobsModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert!(app.uses_workers_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("WorkerPool"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(
        plan["requiredFeatures"],
        serde_json::json!(["stdlib.workers"])
    );
    assert_eq!(plan["features"]["workers"], serde_json::json!(true));
    assert_eq!(
        plan["strongPlan"]["evidence"]["workers"],
        serde_json::json!(true)
    );

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_sloppy_codec_import_emits_required_feature() {
    let root = fixture_temp_dir("function-module-codec-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("payloads.js"),
        r#"import { Results } from "sloppy";
import { Base64, Base64Url, Hex, Text, Binary, Compression, Checksums } from "sloppy/codec";

export function payloadsModule(app) {
    app.get("/payloads", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { payloadsModule } from "./modules/payloads.js";

const app = Sloppy.create();
app.useModule(payloadsModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert!(app.uses_codec_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("Base64"));
    assert!(emitted_js.source.contains("Checksums"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(
        plan["requiredFeatures"],
        serde_json::json!(["stdlib.codec"])
    );
    assert_eq!(plan["features"]["codec"], serde_json::json!(true));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_type_only_sloppy_net_import_does_not_emit_required_feature() {
    let root = fixture_temp_dir("function-module-type-only-net-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("tcp.ts"),
        r#"import { Results } from "sloppy";
import type { TcpClient } from "sloppy/net";

export function tcpModule(app) {
    app.get("/tcp", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { tcpModule } from "./modules/tcp.ts";

const app = Sloppy.create();
app.useModule(tcpModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let input = root.join("input.ts");
    fs::write(&input, source).expect("fixture input should be writable");
    let app = extract(&input, source).expect("fixture should extract");
    assert!(!app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const { Results } = __sloppyRuntime;"));
    assert!(!emitted_js.source.contains("TcpClient"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert!(plan.get("requiredFeatures").is_none());
    assert!(plan["features"].get("network").is_none());

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_type_only_sloppy_net_http_client_import_does_not_emit_required_feature() {
    let root = fixture_temp_dir("function-module-type-only-http-client-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("billing.ts"),
        r#"import { Results } from "sloppy";
import type { HttpClient } from "sloppy/net";

export function billingModule(app) {
    app.get("/billing", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { billingModule } from "./modules/billing.ts";

const app = Sloppy.create();
app.useModule(billingModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let input = root.join("input.ts");
    fs::write(&input, source).expect("fixture input should be writable");
    let app = extract(&input, source).expect("fixture should extract");
    assert!(!app.uses_http_client_runtime);
    assert!(!app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const { Results } = __sloppyRuntime;"));
    assert!(!emitted_js.source.contains("HttpClient"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert!(plan.get("requiredFeatures").is_none());
    assert!(plan["features"].get("httpClient").is_none());
    assert!(plan["features"].get("network").is_none());

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_type_only_sloppy_time_import_does_not_emit_required_feature() {
    let root = fixture_temp_dir("function-module-type-only-time-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("jobs.ts"),
        r#"import { Results } from "sloppy";
import type { Deadline } from "sloppy/time";

export function jobsModule(app) {
    app.get("/jobs", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { jobsModule } from "./modules/jobs.ts";

const app = Sloppy.create();
app.useModule(jobsModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let input = root.join("input.ts");
    fs::write(&input, source).expect("fixture input should be writable");
    let app = extract(&input, source).expect("fixture should extract");
    assert!(!app.uses_time_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const { Results } = __sloppyRuntime;"));
    assert!(!emitted_js.source.contains("Deadline"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert!(plan.get("requiredFeatures").is_none());
    assert!(plan["features"].get("time").is_none());

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_invalid_sloppy_time_import_uses_import_diagnostic() {
    let root = fixture_temp_dir("function-module-invalid-time-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("jobs.js"),
        r#"import { Results } from "sloppy";
import { Time as Clock } from "sloppy/time";

export function jobsModule(app) {
    app.get("/jobs", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { jobsModule } from "./modules/jobs.js";

const app = Sloppy.create();
app.useModule(jobsModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract_temp_input(&root, source)
        .expect_err("invalid sloppy/time import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic
        .message
        .contains("unsupported sloppy import \"Time\""));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn emits_used_function_modules_without_routes() {
    let root = fixture_temp_dir("empty-function-module");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("empty.js"),
        r#"export function emptyModule(app) {
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { emptyModule } from "./modules/empty.js";

const app = Sloppy.create();
app.useModule(emptyModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.modules.len(), 1);
    assert_eq!(app.modules[0].name, "emptyModule");
    assert!(app.modules[0].source_name.ends_with("empty.js"));

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    assert!(plan.contains("\"name\": \"emptyModule\""));
    assert!(plan.contains("empty.js"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn results_import_detection_uses_handler_ast() {
    assert!(parsed_arrow_requires_results_import(
        "() => Results.json({ ok: true })"
    ));
    assert!(parsed_arrow_requires_results_import(
        "() => Results .json({ ok: true })"
    ));
    assert!(parsed_arrow_requires_results_import(
        "() => Results/*comment*/.json({ ok: true })"
    ));
    assert!(parsed_arrow_requires_results_import(
        "() => Results?.json({ ok: true })"
    ));
    assert!(!parsed_arrow_requires_results_import(
        "() => \"Results.json\""
    ));
    assert!(!parsed_arrow_requires_results_import(
        "() => notResults.json({ ok: true })"
    ));
    assert!(!parsed_arrow_requires_results_import(
        "() => ({ notResults: \"Results.json\" })"
    ));
}

#[test]
fn entry_without_results_import_can_use_results_in_function_module() {
    let root = fixture_temp_dir("module-only-entry-without-results");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    app.get("/users", () => Results.json([{ id: "ada" }]));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { usersModule } from "./modules/users.js";

const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("module-only entry should extract");

    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/users");
    assert_eq!(app.routes[0].module.as_deref(), Some("usersModule"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn direct_results_handler_requires_results_import_in_same_file() {
    let root = fixture_temp_dir("direct-results-requires-import");
    let source = r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.get("/health", () => Results.json({ ok: true }));
export default app;
"#;
    let diagnostic =
        extract_temp_input(&root, source).expect_err("direct Results handler should fail");

    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic.message.contains("call Results"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn direct_results_handler_with_space_requires_results_import() {
    let root = fixture_temp_dir("direct-results-space-requires-import");
    let source = r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.get("/health", () => Results .json({ ok: true }));
export default app;
"#;
    let diagnostic =
        extract_temp_input(&root, source).expect_err("direct Results handler should fail");

    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic.message.contains("call Results"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn direct_results_handler_with_comment_requires_results_import() {
    let root = fixture_temp_dir("direct-results-comment-requires-import");
    let source = r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.get("/health", () => Results/*comment*/.json({ ok: true }));
export default app;
"#;
    let diagnostic =
        extract_temp_input(&root, source).expect_err("direct Results handler should fail");

    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic.message.contains("call Results"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn middleware_results_handler_requires_results_import_in_same_file() {
    let root = fixture_temp_dir("middleware-results-requires-import");
    let source = r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.use((ctx, next) => Results.status(401));
app.mapHealthChecks({ path: "/health", checks: [] });
export default app;
"#;
    let diagnostic =
        extract_temp_input(&root, source).expect_err("middleware Results usage should fail");

    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic.message.contains("call Results"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn middleware_results_handler_with_comment_requires_results_import() {
    let root = fixture_temp_dir("middleware-results-comment-requires-import");
    let source = r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.use((ctx, next) => Results /* comment */ .status(401));
app.mapHealthChecks({ path: "/health", checks: [] });
export default app;
"#;
    let diagnostic =
        extract_temp_input(&root, source).expect_err("middleware Results usage should fail");

    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic.message.contains("call Results"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn middleware_string_mentioning_results_does_not_require_results_import() {
    let root = fixture_temp_dir("middleware-results-string-no-import");
    let source = r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.use((ctx, next) => {
  const text = "Results.status";
  // Results.status(401) should not affect import validation.
  return next();
});
app.mapHealthChecks({ path: "/health", checks: [] });
export default app;
"#;
    let app = extract_temp_input(&root, source)
        .expect("middleware string/comment mention should not require Results import");
    assert_eq!(app.routes.len(), 3);

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn middleware_results_handler_with_import_compiles() {
    let root = fixture_temp_dir("middleware-results-with-import");
    let source = r#"import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
app.use((ctx, next) => Results.status(401));
app.get("/health", () => Results.ok({ ok: true }));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("middleware Results import should compile");
    assert_eq!(app.routes.len(), 1);

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_results_handler_requires_module_results_import() {
    let root = fixture_temp_dir("module-results-requires-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"export function usersModule(app) {
    app.get("/users", () => Results.json([{ id: "ada" }]));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { usersModule } from "./modules/users.js";

const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let diagnostic =
        extract_temp_input(&root, source).expect_err("module Results handler should fail");

    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic.message.contains("same source file"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_results_import_is_not_source_order_dependent() {
    let root = fixture_temp_dir("module-results-import-after-export");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"export function usersModule(app) {
    app.get("/users", () => Results.json([{ id: "ada" }]));
}

import { Results } from "sloppy";
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { usersModule } from "./modules/users.js";

const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let app = extract_temp_input(&root, source)
        .expect("module Results import should be honored regardless of source order");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/users");

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn rejects_invalid_composed_function_module_route_pattern() {
    let root = fixture_temp_dir("invalid-module-route-pattern");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    const api = app.group("/api");
    api.get("/users/", () => Results.text("bad"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { usersModule } from "./modules/users.js";
const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let diagnostic =
        extract_temp_input(&root, source).expect_err("invalid module route should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_ROUTE_PATTERN");
    assert!(diagnostic
        .path
        .as_deref()
        .is_some_and(|path| path.ends_with("modules/users.js")));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn rejects_duplicate_method_and_path_routes() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/dupe", () => Results.text("one"));
app.get("/dupe", () => Results.text("two"));
export default app;
"#;
    let diagnostic =
        extract(std::path::Path::new("app.js"), source).expect_err("duplicate routes should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_DUPLICATE_ROUTE");
}

#[test]
fn duplicate_module_routes_report_module_source() {
    let root = fixture_temp_dir("duplicate-module-routes");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    let module_path = modules.join("users.js");
    fs::write(
        &module_path,
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    app.get("/dupe", () => Results.text("module"));
}
"#,
    )
    .expect("module fixture should be writable");
    let input = root.join("input.js");
    fs::write(
        &input,
        r#"import { Sloppy, Results } from "sloppy";
import { usersModule } from "./modules/users.js";
const app = Sloppy.create();
app.useModule(usersModule);
app.get("/dupe", () => Results.text("entry"));
export default app;
"#,
    )
    .expect("input fixture should be writable");
    let out_dir = root.join("out");

    let failure = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("duplicate route should fail");
    assert_eq!(failure.diagnostic.code, "SLOPPYC_E_DUPLICATE_ROUTE");
    let canonical_module_path =
        fs::canonicalize(&module_path).expect("module path should canonicalize");
    assert_eq!(
        failure.diagnostic.path.as_deref(),
        Some(canonical_module_path.as_path())
    );
    let rendered = failure.diagnostic.render(failure.source.as_deref());
    assert!(rendered.contains("users.js:4:5"), "{rendered}");
    assert!(
        rendered.contains(r#"4 |     app.get("/dupe", () => Results.text("module"));"#),
        "{rendered}"
    );

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn duplicate_module_route_names_report_module_source() {
    let root = fixture_temp_dir("duplicate-module-route-names");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    let module_path = modules.join("users.js");
    fs::write(
        &module_path,
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    app.get("/module", () => Results.text("module")).withName("Users.Get");
}
"#,
    )
    .expect("module fixture should be writable");
    let input = root.join("input.js");
    fs::write(
        &input,
        r#"import { Sloppy, Results } from "sloppy";
import { usersModule } from "./modules/users.js";
const app = Sloppy.create();
app.useModule(usersModule);
app.get("/entry", () => Results.text("entry")).withName("Users.Get");
export default app;
"#,
    )
    .expect("input fixture should be writable");
    let out_dir = root.join("out");

    let failure = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("duplicate route name should fail");
    assert_eq!(failure.diagnostic.code, "SLOPPYC_E_DUPLICATE_ROUTE_NAME");
    let canonical_module_path =
        fs::canonicalize(&module_path).expect("module path should canonicalize");
    assert_eq!(
        failure.diagnostic.path.as_deref(),
        Some(canonical_module_path.as_path())
    );
    let rendered = failure.diagnostic.render(failure.source.as_deref());
    assert!(rendered.contains("users.js:4:5"), "{rendered}");
    assert!(
        rendered.contains(
            r#"4 |     app.get("/module", () => Results.text("module")).withName("Users.Get");"#
        ),
        "{rendered}"
    );

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn rejects_missing_module_function_binding() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.useModule(usersModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("missing module function should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE");
}

#[test]
fn rejects_wrong_module_export_shape() {
    let root = fixture_temp_dir("wrong-module-shape");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"export const usersModule = () => {};
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { usersModule } from "./modules/users.js";
const app = Sloppy.create();
app.useModule(usersModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract_temp_input(&root, source).expect_err("wrong module shape should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE");

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn constant_route_pattern_alias_stays_complete_route_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const pattern = "/";
app.mapGet(pattern, () => Results.text("Hello"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("constant route pattern alias should compile");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/");
    assert!(app.dynamic_routes.is_empty());
}

#[test]
fn dynamic_route_pattern_emits_dynamic_metadata_instead_of_failing() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
function pathFor(name) {
  return `/${name}`;
}
app.mapGet(pathFor("health"), () => Results.text("Hello"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("dynamic route metadata should not fail extraction");
    assert!(app.routes.is_empty());
    assert_eq!(app.dynamic_routes.len(), 1);
    assert_eq!(app.dynamic_routes[0].method, Some("GET"));
    assert!(app.dynamic_routes[0].handler_known);
}

#[test]
fn plain_object_and_throw_handlers_stay_on_registered_route_path() {
    let source = r#"import { Sloppy } from "sloppy";
const app = Sloppy.create();
app.get("/plain-object", (ctx) => ({
  ok: true,
  method: ctx.request.method,
}));
app.get("/exception", () => {
  throw new Error("bench exception");
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("plain object and throw handlers should extract");
    let emitted_js = super::emit_app_js(&app);

    assert_eq!(app.routes.len(), 2);
    assert!(app.dynamic_routes.is_empty());
    assert_eq!(
        emitted_js
            .source
            .matches("globalThis.__sloppy_register_handler(")
            .count(),
        2
    );
    assert!(emitted_js
        .source
        .contains("globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);"));
    assert!(emitted_js
        .source
        .contains("globalThis.__sloppy_register_handler(2, globalThis.__sloppy_handler_2);"));
}

#[test]
fn inline_json_primitive_handlers_match_runtime_contract() {
    let source = r#"import { Sloppy } from "sloppy";
const app = Sloppy.create();
app.get("/object", () => ({ ok: true }));
app.get("/array", () => [1, 2, 3]);
app.get("/number", () => 42);
app.get("/boolean", () => true);
app.get("/null", () => null);
app.get("/undefined", () => undefined);
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("JSON value handlers should extract");
    let emitted_js = super::emit_app_js(&app);

    assert_eq!(app.routes.len(), 6);
    assert!(app.dynamic_routes.is_empty());
    assert_eq!(
        emitted_js
            .source
            .matches("globalThis.__sloppy_register_handler(")
            .count(),
        6
    );
    assert!(emitted_js
        .source
        .contains("globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);"));
    assert!(emitted_js
        .source
        .contains("globalThis.__sloppy_register_handler(2, globalThis.__sloppy_handler_2);"));
    assert!(emitted_js
        .source
        .contains("globalThis.__sloppy_register_handler(3, globalThis.__sloppy_handler_3);"));
    assert!(emitted_js
        .source
        .contains("globalThis.__sloppy_register_handler(4, globalThis.__sloppy_handler_4);"));
    assert!(emitted_js
        .source
        .contains("globalThis.__sloppy_register_handler(5, globalThis.__sloppy_handler_5);"));
    assert!(emitted_js
        .source
        .contains("globalThis.__sloppy_register_handler(6, globalThis.__sloppy_handler_6);"));
}

#[test]
fn rejects_static_route_segments_with_stray_braces() {
    assert!(!route_pattern_supported("/foo{bar"));
    assert!(!route_pattern_supported("/a}b"));
    assert!(!route_pattern_supported("/{id{slug}}"));
}

#[test]
fn accepts_supported_http_result_helpers() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapGet("/ok", () => Results.ok({ ok: true }));
app.mapGet("/empty", () => Results.noContent());
app.mapGet("/created", () => Results.created("/users/1", { id: 1 }));
app.mapGet("/accepted", () => Results.accepted({ queued: true }));
app.mapGet("/not-found", () => Results.notFound({ error: "missing" }));
app.mapGet("/bad", () => Results.badRequest({ error: "bad" }));
app.mapGet("/status", () => Results.status(202, { accepted: true }));
app.mapGet("/problem", () => Results.problem("broken"));
app.mapGet("/html", () => Results.html("<p>ok</p>"));
app.mapGet("/bytes", () => Results.bytes(new Uint8Array([0, 65, 255]), { contentType: "application/x-test" }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert_eq!(app.routes.len(), 10);
    assert_eq!(app.routes[0].pattern, "/ok");
    assert_eq!(app.routes[1].pattern, "/empty");
    assert_eq!(app.routes[2].pattern, "/created");
    assert_eq!(app.routes[7].pattern, "/problem");
    assert_eq!(app.routes[8].pattern, "/html");
    assert_eq!(app.routes[9].pattern, "/bytes");
}

#[test]
fn static_status_and_problem_results_emit_native_response_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/empty", () => Results.status(204));
app.get("/problem", () => Results.problem({ status: 400, title: "Static problem", code: "SLOPPY_E_STATIC_PROBLEM" }, { status: 400 }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan_text = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan_text).expect("plan should parse");
    let routes = plan["routes"]
        .as_array()
        .expect("plan routes should be an array");
    let empty_route = routes
        .iter()
        .find(|route| route["pattern"] == serde_json::json!("/empty"))
        .expect("empty route should be present");
    let problem_route = routes
        .iter()
        .find(|route| route["pattern"] == serde_json::json!("/problem"))
        .expect("problem route should be present");

    assert_eq!(
        empty_route["dispatch"]["executionKind"],
        serde_json::json!("native-static-empty")
    );
    assert_eq!(
        empty_route["nativeResponse"]["kind"],
        serde_json::json!("empty")
    );
    assert_eq!(
        empty_route["nativeResponse"]["status"],
        serde_json::json!(204)
    );
    assert_eq!(
        problem_route["dispatch"]["executionKind"],
        serde_json::json!("native-static-problem")
    );
    assert_eq!(
        problem_route["nativeResponse"]["kind"],
        serde_json::json!("problem")
    );
    assert_eq!(
        problem_route["nativeResponse"]["contentType"],
        serde_json::json!("application/problem+json")
    );
    let problem_body: serde_json::Value =
        serde_json::from_str(problem_route["nativeResponse"]["body"].as_str().unwrap())
            .expect("problem body should parse");
    assert_eq!(
        problem_body["code"],
        serde_json::json!("SLOPPY_E_STATIC_PROBLEM")
    );
    assert_eq!(problem_body["status"].as_f64(), Some(400.0));
    assert_eq!(problem_body["title"], serde_json::json!("Static problem"));
}

#[test]
fn accepts_template_literal_result_arguments_from_context() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapGet("/apps/{id}/builds", (ctx) => Results.created(`/apps/${ctx.route.id}/builds/b_002`, {
  appId: ctx.route.id,
  status: "queued"
}));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("context-only template literal result should extract");
    assert_eq!(app.routes.len(), 1);
    assert!(app.routes[0]
        .handler
        .source
        .contains("`/apps/${ctx.route.id}/builds/b_002`"));
}

#[test]
fn problem_details_defaults_wraps_compiled_handler_errors() {
    let source = r#"import { Sloppy, Results, ProblemDetails } from "sloppy";
const app = Sloppy.create();
app.mapGet("/boom", () => Results.text("ok"));
app.use(ProblemDetails.defaults());
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert!(app.problem_details.is_some());
    assert_eq!(app.problem_details.as_ref().unwrap().detail, "never");
    assert!(app.routes[0].handler.is_async);
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("SLOPPY_E_HANDLER_ERROR"));
    assert!(app.routes[0]
        .handler
        .responses
        .iter()
        .any(|response| response.helper == "problem" && response.status == 500));

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("ProblemDetails"));
    assert!(emitted_js.source.contains("Internal Server Error"));
}

#[test]
fn map_health_checks_extracts_routes_and_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapHealthChecks({
  path: "/_health",
  livenessPath: "/_live",
  readinessPath: "/_ready",
  checks: [
    function database() { return { ok: true }; },
    { name: "worker", liveness: true, readiness: false, check: () => true },
    { name: "cache", liveness: true, check(ctx) { return ctx !== undefined; } }
  ]
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert!(app.uses_health);
    assert_eq!(app.routes.len(), 3);
    assert_eq!(app.routes[0].name.as_deref(), Some("Health"));
    assert_eq!(app.routes[1].name.as_deref(), Some("Health.Liveness"));
    assert_eq!(app.routes[2].name.as_deref(), Some("Health.Readiness"));
    assert_eq!(app.routes[0].pattern, "/_health");
    assert_eq!(app.routes[1].pattern, "/_live");
    assert_eq!(app.routes[2].pattern, "/_ready");
    assert_eq!(
        app.routes[0].health.as_ref().unwrap().checks,
        vec!["database", "worker", "cache"]
    );
    assert_eq!(
        app.routes[1].health.as_ref().unwrap().checks,
        vec!["worker", "cache"]
    );
    assert_eq!(
        app.routes[2].health.as_ref().unwrap().checks,
        vec!["database", "cache"]
    );

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("__sloppy_health_checks"));
    assert!(emitted_js.source.contains("function (ctx)"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["features"]["health"], true);
    assert_eq!(plan["strongPlan"]["evidence"]["health"], true);
    assert_eq!(plan["routes"][0]["health"]["kind"], "aggregate");
    assert_eq!(
        plan["routes"][1]["health"]["checks"],
        serde_json::json!(["worker", "cache"])
    );
    assert_eq!(plan["routes"][2]["responses"][1]["status"], 503);
}

#[test]
fn app_health_and_management_extract_routes_and_ops_metadata() {
    let source = r#"import { Sloppy, Health } from "sloppy";
const app = Sloppy.create();
app.health()
  .check("self", Health.self(), { tags: ["live", "ready", "startup"], critical: true })
  .check("runtime", Health.runtime(), { tags: ["ready", "startup"] })
  .check("custom", () => ({ status: "degraded" }), { tags: ["ready"], critical: true, degradedIsUnhealthy: true })
  .expose({ health: "/health", live: "/live", ready: "/ready", startup: "/startup" });
app.management({ path: "/_sloppy", health: true, metrics: true, info: true, runtime: true });
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("ops APIs should extract");
    assert!(app.uses_health);
    assert_eq!(
        app.routes
            .iter()
            .map(|route| route.pattern.as_str())
            .collect::<Vec<_>>(),
        vec![
            "/health",
            "/live",
            "/ready",
            "/startup",
            "/_sloppy/health",
            "/_sloppy/live",
            "/_sloppy/ready",
            "/_sloppy/startup",
            "/_sloppy/metrics",
            "/_sloppy/metrics.json",
            "/_sloppy/info",
            "/_sloppy/runtime",
        ]
    );
    assert_eq!(app.routes[3].health.as_ref().unwrap().kind, "startup");
    assert_eq!(app.routes[8].name.as_deref(), Some("Management.Metrics"));

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("sloppy_management_info"));
    assert!(emitted_js.source.contains("degradedIsUnhealthy: true"));
    assert!(emitted_js
        .source
        .contains("__sloppy_health_check.critical && __sloppy_check_status === \"degraded\""));
    assert!(emitted_js.source.contains(
        "!__sloppy_health_check.critical && __sloppy_aggregate_status === \"unhealthy\""
    ));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["features"]["ops"], true);
    assert_eq!(plan["features"]["management"], true);
    assert_eq!(plan["features"]["metrics"], true);
    assert_eq!(plan["strongPlan"]["evidence"]["ops"], true);
    assert_eq!(
        plan["ops"]["health"]["endpoints"].as_array().unwrap().len(),
        8
    );
    assert_eq!(
        plan["ops"]["metrics"],
        serde_json::json!({ "prometheus": true, "json": true })
    );
    assert!(plan["doctorChecks"]
        .as_array()
        .unwrap()
        .iter()
        .any(|check| check["id"] == "ops.management.protection"));
}

#[test]
fn app_health_rejects_silently_ignored_options_and_unsupported_builtins() {
    for source in [
        r#"import { Sloppy, Health } from "sloppy";
const app = Sloppy.create();
app.health().check("slow", Health.self(), { tags: ["ready"], timeoutMs: 1000 }).expose();
export default app;
"#,
        r#"import { Sloppy, Health } from "sloppy";
const app = Sloppy.create();
app.health().check("cached", Health.self(), { tags: ["ready"], cacheMs: 1000 }).expose();
export default app;
"#,
        r#"import { Sloppy, Health } from "sloppy";
const app = Sloppy.create();
app.health().check("config", Health.config(["Required"]), { tags: ["ready"] }).expose();
export default app;
"#,
        r#"import { Sloppy, Health } from "sloppy";
const app = Sloppy.create();
app.mapHealthChecks({ checks: [{ name: "cached", check: () => true, cacheMs: 1000 }] });
export default app;
"#,
    ] {
        let diagnostic = extract(std::path::Path::new("app.js"), source)
            .expect_err("unsupported compiler-visible health feature should fail");
        assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS");
    }
}

#[test]
fn map_health_checks_rejects_unsupported_static_shapes() {
    for source in [
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapHealthChecks({ path: "/same", livenessPath: "/same" });
export default app;
"#,
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapHealthChecks({ checks: [{ name: "none", liveness: false, readiness: false, check: () => true }] });
export default app;
"#,
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapHealthChecks({ checks: [{ name: "bad", check: (ctx: RequestContext) => true }] });
export default app;
"#,
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const ready = true;
app.mapHealthChecks({ checks: [{ name: "captured", check: () => ready }] });
export default app;
"#,
    ] {
        let diagnostic = extract(std::path::Path::new("app.ts"), source)
            .expect_err("unsupported health shape should fail");
        assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS");
    }
}

#[test]
fn extracts_schema_binding_config_and_result_metadata() {
    let source = r#"import { Sloppy, Results, schema } from "sloppy";
const UserCreate = schema.object({
  name: schema.string().min(1),
  tags: schema.array(schema.string()).optional()
});
const app = Sloppy.create();
const host = app.config.getString("Sloppy:Server:Host", "127.0.0.1");
app.post("/users/{id:int}", (ctx) => Results.json({
  id: ctx.route.id,
  search: ctx.query.q,
  agent: ctx.header.userAgent,
  requestId: ctx.header.xRequestId,
  digest: ctx.header.contentMD5,
  body: ctx.body.json(UserCreate)
}));
export default app;
"#;
    let app =
        extract(std::path::Path::new("app.js"), source).expect("metadata fixture should extract");
    assert_eq!(app.schemas.len(), 1);
    assert_eq!(app.schemas[0].name, "UserCreate");
    assert_eq!(app.config_reads.len(), 1);
    assert_eq!(app.config_reads[0].key, "Sloppy:Server:Host");
    assert_eq!(app.routes[0].handler.bindings.len(), 6);
    assert_eq!(
        app.routes[0]
            .handler
            .response
            .as_ref()
            .map(|response| (response.helper.as_str(), response.status)),
        Some(("json", 200))
    );

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("ctx.body.json(undefined)"));
    assert!(!emitted_js.source.contains("ctx.body.json(UserCreate)"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["schemas"][0]["name"], "UserCreate");
    assert_eq!(plan["configReads"][0]["key"], "Sloppy:Server:Host");
    assert_eq!(plan["routes"][0]["bindings"][0]["kind"], "route");
    assert_eq!(plan["routes"][0]["bindings"][2]["kind"], "header");
    assert_eq!(plan["routes"][0]["bindings"][2]["name"], "user-agent");
    assert_eq!(plan["routes"][0]["bindings"][3]["kind"], "header");
    assert_eq!(plan["routes"][0]["bindings"][3]["name"], "x-request-id");
    assert_eq!(plan["routes"][0]["bindings"][4]["kind"], "header");
    assert_eq!(plan["routes"][0]["bindings"][4]["name"], "content-md5");
    assert_eq!(plan["routes"][0]["response"]["helper"], "json");
    assert_eq!(plan["features"]["metadataInference"], true);
}

#[test]
fn extracts_schema_alias_body_validate_and_fluent_route_schemas() {
    let source = r#"import { Sloppy, Results, Schema, RequestContext, Route } from "sloppy";
const CreateUser = Schema.object({
  name: Schema.string().min(1).max(100),
  email: Schema.string().email()
});
const User = Schema.object({
  id: Schema.integer(),
  name: Schema.string(),
  email: Schema.string(),
  role: Schema.enum(["admin", "user"]),
  status: Schema.literal("active")
});
const app = Sloppy.create();
app.post("/users", async (ctx) =>
  Results.created("/users/1", await ctx.body.validate(CreateUser))
).accepts(CreateUser).returns(User).withName("Users.Create");
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("Schema alias and fluent schemas should extract");
    assert_eq!(app.schemas.len(), 2);
    assert_eq!(app.routes[0].name.as_deref(), Some("Users.Create"));
    assert_eq!(app.routes[0].handler.bindings.len(), 1);
    assert_eq!(app.routes[0].handler.bindings[0].kind, "body.json");
    assert_eq!(
        app.routes[0].handler.bindings[0].schema.as_deref(),
        Some("CreateUser")
    );
    assert_eq!(
        app.routes[0]
            .handler
            .response
            .as_ref()
            .and_then(|response| response.body_schema.as_deref()),
        Some("User")
    );
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("ctx.body.validate(undefined)"));
    assert!(!emitted_js.source.contains("ctx.body.validate(CreateUser)"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["routes"][0]["bindings"][0]["schema"], "CreateUser");
    assert_eq!(plan["routes"][0]["response"]["bodySchema"], "User");
    assert_eq!(plan["routes"][0]["jsonResponse"]["mode"], "fallback");
    assert_eq!(plan["routes"][0]["jsonResponse"]["writer"], "none");
    assert_eq!(
        plan["routes"][0]["jsonResponse"]["fallbackReason"],
        "native-schema-response-writer-unsupported:literal-union-schema-unsupported"
    );
    let user_schema = plan["schemas"]
        .as_array()
        .and_then(|schemas| schemas.iter().find(|schema| schema["name"] == "User"))
        .expect("User schema should exist");
    assert_eq!(
        user_schema["definition"]["properties"]["role"]["kind"],
        "literalUnion"
    );
    assert_eq!(
        user_schema["definition"]["properties"]["role"]["variants"][0]["value"],
        "admin"
    );
    assert_eq!(
        user_schema["definition"]["properties"]["status"]["kind"],
        "literal"
    );
    assert_eq!(
        user_schema["definition"]["properties"]["status"]["value"],
        "active"
    );
}

#[test]
fn extracts_openapi_route_contract_metadata() {
    let source = r#"import { Sloppy, Results, Schema } from "sloppy";
const CreateUser = Schema.object({ name: Schema.string().min(1) });
const User = Schema.object({ id: Schema.integer(), name: Schema.string() });
const Query = Schema.object({ expand: Schema.string().optional() });
const Params = Schema.object({ id: Schema.integer() });
const Header = Schema.string();
const app = Sloppy.create();
app.post("/users/{id:int}", () => Results.created("/users/1", { id: 1, name: "Ada" }))
  .name("Users.Create")
  .summary("Create user")
  .description("Creates a user account.")
  .tags("Users", "Admin")
  .accepts(CreateUser)
  .returns(201, User, { description: "User created" })
  .produces("application/json")
  .consumes("application/json")
  .header("x-request-id", Header, { required: true, description: "Request id" })
  .query(Query)
  .params(Params)
  .authorize("Users.Write")
  .openapi({ "x-audit": "enabled" });
app.docs({ title: "Users API", requireAuth: { policy: "Docs.Read" } });
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("OpenAPI route metadata should extract");
    let route = &app.routes[0];
    assert_eq!(route.name.as_deref(), Some("Users.Create"));
    assert_eq!(route.summary.as_deref(), Some("Create user"));
    assert_eq!(
        route.description.as_deref(),
        Some("Creates a user account.")
    );
    assert_eq!(route.tags, vec!["Users".to_string(), "Admin".to_string()]);
    assert_eq!(route.consumes, vec!["application/json".to_string()]);
    assert_eq!(route.produces, vec!["application/json".to_string()]);
    assert_eq!(route.headers[0].name, "x-request-id");
    assert_eq!(route.headers[0].schema, "Header");
    assert_eq!(route.query_schema.as_deref(), Some("Query"));
    assert_eq!(route.params_schema.as_deref(), Some("Params"));
    assert_eq!(
        route.auth.as_ref().and_then(|auth| auth.policy.as_deref()),
        Some("Users.Write")
    );
    assert_eq!(
        route
            .openapi_override
            .as_ref()
            .and_then(|value| value.get("x-audit"))
            .and_then(|value| value.as_str()),
        Some("enabled")
    );
    assert!(app
        .routes
        .iter()
        .any(|route| route.name.as_deref() == Some("Docs.Ui")));
    assert!(app
        .routes
        .iter()
        .any(|route| route.name.as_deref() == Some("Docs.OpenApi")));
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    let route = &plan["routes"][0];
    assert_eq!(route["summary"], "Create user");
    assert_eq!(route["querySchema"], "Query");
    assert_eq!(route["paramsSchema"], "Params");
    assert_eq!(route["headers"][0]["name"], "x-request-id");
    assert_eq!(route["openapi"]["x-audit"], "enabled");
}

#[test]
fn app_docs_preserves_full_require_auth_metadata() {
    for (label, require_auth, roles, claims, policy) in [
        ("required", "true", vec![], vec![], None),
        ("role", r#"{ role: "admin" }"#, vec!["admin"], vec![], None),
        (
            "roles",
            r#"{ roles: ["admin", "ops"] }"#,
            vec!["admin", "ops"],
            vec![],
            None,
        ),
        (
            "claim",
            r#"{ claim: "tenant.write" }"#,
            vec![],
            vec!["tenant.write"],
            None,
        ),
        (
            "policy",
            r#"{ policy: "Docs.Admin" }"#,
            vec![],
            vec![],
            Some("Docs.Admin"),
        ),
    ] {
        let source = format!(
            r#"import {{ Sloppy, Results }} from "sloppy";
const app = Sloppy.create();
app.get("/users", () => Results.ok([])).name("Users.List");
app.docs({{ requireAuth: {require_auth} }});
export default app;
"#
        );
        let app = extract(std::path::Path::new("app.ts"), &source)
            .unwrap_or_else(|error| panic!("{label}: {error:?}"));
        let docs_routes = app
            .routes
            .iter()
            .filter(|route| {
                route
                    .name
                    .as_deref()
                    .is_some_and(|name| name.starts_with("Docs."))
            })
            .collect::<Vec<_>>();
        assert_eq!(docs_routes.len(), 2, "{label}: docs routes");
        for route in docs_routes {
            let auth = route
                .auth
                .as_ref()
                .unwrap_or_else(|| panic!("{label}: auth should exist"));
            assert!(auth.required, "{label}: required");
            assert_eq!(auth.roles, roles, "{label}: roles");
            assert_eq!(auth.claims, claims, "{label}: claims");
            assert_eq!(auth.policy.as_deref(), policy, "{label}: policy");
        }
    }
}

#[test]
fn app_docs_enabled_false_is_noop_and_strict_is_plan_visible() {
    let disabled = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/users", () => Results.ok([]));
app.docs({ enabled: false });
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), disabled).expect("disabled docs extracts");
    assert!(!app.routes.iter().any(|route| route
        .name
        .as_deref()
        .is_some_and(|name| name.starts_with("Docs."))));

    let strict = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/users", () => Results.ok([]));
app.docs({ strict: true });
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), strict).expect("strict docs extracts");
    let openapi = app
        .routes
        .iter()
        .find(|route| route.name.as_deref() == Some("Docs.OpenApi"))
        .expect("OpenAPI docs route");
    let docs = openapi.docs.as_ref().expect("docs metadata");
    assert_eq!(docs.kind, "openapi");
    assert!(docs.strict);
}

#[test]
fn typed_framework_handler_sanitizes_context_schema_reference() {
    let source = r#"import { Sloppy, Results, Schema, RequestContext } from "sloppy";
import { Postgres } from "sloppy/providers/postgres";
const CreateUser = Schema.object({ name: Schema.string() });
const User = Schema.object({ id: Schema.integer(), name: Schema.string() });
const app = Sloppy.create();
app.post("/users", async (db: Postgres<"main">, ctx: RequestContext) => {
  const input = await ctx.body.validate(CreateUser);
  return Results.created("/users/1", { id: 1, name: input.name });
}).accepts(CreateUser).returns(User);
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed handler schema reference should extract");
    let body_binding = app.routes[0]
        .handler
        .bindings
        .iter()
        .find(|binding| binding.kind == "body.json")
        .expect("body schema binding should exist");
    assert_eq!(body_binding.schema.as_deref(), Some("CreateUser"));
    assert!(app.routes[0]
        .handler
        .bindings
        .iter()
        .any(|binding| binding.kind == "context"));
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("ctx.body.validate(undefined)"));
    assert!(!emitted_js.source.contains("ctx.body.validate(CreateUser)"));
}

#[test]
fn handler_emits_referenced_top_level_literal_helpers() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const columns = `id, name`;
const app = Sloppy.create();
app.get("/users", () => Results.ok({ sql: `select ${columns} from users` }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("top-level literal helper should extract");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("const columns = `id, name`;"));
    assert!(emitted_js.source.contains("`select ${columns} from users`"));
}

#[test]
fn extracts_named_schema_references_inside_schema_dsl() {
    let source = r#"import { Sloppy, Results, Schema, RequestContext, Route } from "sloppy";
const User = Schema.object({
  id: Schema.integer(),
  name: Schema.string()
});
const Users = Schema.array(User);
const Project = Schema.object({
  owner: User,
  members: Users
});
const app = Sloppy.create();
app.get("/users", (ctx: RequestContext) => Results.ok(ctx.request.method ? [{ id: 1, name: "Ada" }] : [])).returns(Users);
app.get("/projects/{id:int}", (id: Route<number>, ctx: RequestContext) => Results.ok(ctx.request.method && id ? { owner: { id: 1, name: "Ada" }, members: [] } : { owner: { id: 2, name: "Grace" }, members: [] })).returns(Project);
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("named schema references should extract");
    assert_eq!(app.schemas.len(), 3);

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    let users_schema = plan["schemas"]
        .as_array()
        .and_then(|schemas| schemas.iter().find(|schema| schema["name"] == "Users"))
        .expect("Users schema should exist");
    assert_eq!(users_schema["definition"]["items"]["kind"], "object");
    assert_eq!(
        users_schema["definition"]["items"]["properties"]["name"]["kind"],
        "string"
    );
    let project_schema = plan["schemas"]
        .as_array()
        .and_then(|schemas| schemas.iter().find(|schema| schema["name"] == "Project"))
        .expect("Project schema should exist");
    assert_eq!(
        project_schema["definition"]["properties"]["owner"]["properties"]["id"]["kind"],
        "int"
    );
    assert_eq!(
        project_schema["definition"]["properties"]["members"]["items"]["properties"]["name"]
            ["kind"],
        "string"
    );
    assert_eq!(plan["routes"][0]["response"]["bodySchema"], "Users");
    assert_eq!(plan["routes"][1]["response"]["bodySchema"], "Project");
    assert_eq!(plan["routes"][0]["jsonResponse"]["mode"], "native-schema");
    assert_eq!(plan["routes"][0]["jsonResponse"]["writer"], "bounded");
    assert_eq!(plan["routes"][0]["jsonResponse"]["schema"], "Users");
    assert_eq!(plan["routes"][1]["jsonResponse"]["mode"], "native-schema");
    assert_eq!(plan["routes"][1]["jsonResponse"]["writer"], "bounded");
    assert_eq!(plan["routes"][1]["jsonResponse"]["schema"], "Project");
}

#[test]
fn cyclic_typescript_schema_references_emit_partial_doctor_metadata() {
    let source = r#"import { Sloppy, Results, Body } from "sloppy";
type NodeA = { child: NodeB };
type NodeB = { parent: NodeA };
const app = Sloppy.create();
app.post("/nodes", (input: Body<NodeA>) => Results.ok(input));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("cyclic TypeScript-only schema references should not block runnable source");
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("partial schema plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    let node_schema = plan["schemas"]
        .as_array()
        .and_then(|schemas| schemas.iter().find(|schema| schema["name"] == "NodeA"))
        .expect("NodeA schema should exist");
    assert_eq!(
        node_schema["definition"]["properties"]["child"]["properties"]["parent"]["partial"],
        true
    );
    assert!(plan["doctorChecks"]
        .as_array()
        .is_some_and(|checks| checks
            .iter()
            .any(|check| check["id"] == "schema.reference.partial"
                && check["schema"] == "NodeA"
                && check["reason"] == "cycle")));
}

#[test]
fn returns_schema_metadata_only_applies_to_json_responses() {
    let source = r#"import { Sloppy, Results, Schema } from "sloppy";
const User = Schema.object({ id: Schema.integer() });
const app = Sloppy.create();
app.get("/users", () => Results.text("ok")).returns(User);
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("returns metadata should extract for non-json handlers");
    let response = app.routes[0]
        .handler
        .response
        .as_ref()
        .expect("text response should remain primary metadata");
    assert_eq!(response.kind, "text");
    assert_eq!(response.body_schema, None);
    assert!(app.routes[0].handler.responses.iter().any(|response| {
        response.kind == "json" && response.body_schema.as_deref() == Some("User")
    }));
}

#[test]
fn rejects_unresolved_fluent_route_schema_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.post("/users", () => Results.created("/users/1", {})).accepts(CreateUser);
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.ts"), source)
        .expect_err("unresolved fluent schema metadata should fail extraction");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNRESOLVED_SCHEMA");
}

#[test]
fn rejects_unresolved_body_validate_schema_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.post("/users", async (ctx) => Results.created("/users/1", await ctx.body.validate(CreateUser)));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.ts"), source)
        .expect_err("unresolved body validation schema should fail extraction");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNRESOLVED_SCHEMA");
}

#[test]
fn rejects_unresolved_body_validate_schema_metadata_in_control_flow() {
    let fixtures = [
        (
            "while body",
            r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.post("/users", (ctx) => {
  while (ctx.request.method) {
    return Results.ok({ body: ctx.body.validate(CreateUser) });
  }
  return Results.ok({});
});
export default app;
"#,
        ),
        (
            "switch body",
            r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.post("/users", (ctx) => {
  switch (ctx.request.method) {
    case "POST":
      return Results.ok({ body: ctx.body.validate(CreateUser) });
    default:
      return Results.ok({});
  }
});
export default app;
"#,
        ),
        (
            "try body",
            r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.post("/users", (ctx) => {
  try {
    return Results.ok({ body: ctx.body.validate(CreateUser) });
  } catch {
    return Results.ok({});
  }
});
export default app;
"#,
        ),
        (
            "nested call argument",
            r#"import { Sloppy, Results } from "sloppy";
function wrap(value) { return value; }
const app = Sloppy.create();
app.post("/users", (ctx) => Results.ok({ body: wrap(ctx.body.validate(CreateUser)) }));
export default app;
"#,
        ),
    ];

    for (name, source) in fixtures {
        let diagnostic = match extract(std::path::Path::new("app.ts"), source) {
            Ok(_) => panic!("{name} should fail on unresolved body validation schema"),
            Err(diagnostic) => diagnostic,
        };
        assert_eq!(diagnostic.code, "SLOPPYC_E_UNRESOLVED_SCHEMA", "{name}");
    }
}

#[test]
fn conflicting_fluent_route_schema_metadata_marks_partial() {
    let source = r#"import { Sloppy, Results, Schema } from "sloppy";
const CreateUser = Schema.object({ name: Schema.string() });
const UpdateUser = Schema.object({ name: Schema.string() });
const app = Sloppy.create();
app.post("/users", async (ctx) =>
  Results.created("/users/1", await ctx.body.validate(CreateUser))
).accepts(UpdateUser);
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("conflicting fluent schema metadata should compile as partial metadata");
    assert!(app.routes[0].handler.schema_metadata_conflict);
    assert_eq!(app.routes[0].handler.bindings[0].schema.as_deref(), None);
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("partial plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["routes"][0]["completeness"]["status"], "partial");
    assert!(plan["routes"][0]["completeness"]["reasons"]
        .as_array()
        .is_some_and(|reasons| reasons
            .iter()
            .any(|reason| reason["code"] == "schema-metadata-conflict")));
}

#[test]
fn conflicting_response_schema_metadata_marks_response_partial() {
    let source = r#"import { Sloppy, Results, Route, Schema } from "sloppy";
type UserDto = { id: number, name: string };
const PublicUser = Schema.object({ id: Schema.integer() });
const app = Sloppy.create();
app.get("/users/:id", (id: Route<number>) =>
  Results.ok<UserDto>({ id, name: "Ada" })
).returns(PublicUser);
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("conflicting response schema metadata should compile as partial metadata");
    let response = app.routes[0]
        .handler
        .response
        .as_ref()
        .expect("typed response metadata should be present");
    assert_eq!(response.body_schema.as_deref(), Some("UserDto"));
    assert!(response.partial);
    assert!(app.routes[0].handler.schema_metadata_conflict);

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("partial plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["routes"][0]["response"]["partial"], true);
    assert!(plan["routes"][0]["completeness"]["reasons"]
        .as_array()
        .is_some_and(|reasons| reasons
            .iter()
            .any(|reason| reason["code"] == "response-metadata-partial")));
}

#[test]
fn extracts_default_and_pattern_schema_modifiers() {
    let source = r#"import { Sloppy, Results, Schema } from "sloppy";
const User = Schema.object({
  name: Schema.string().pattern(/^[a-z]+$/u).default("guest")
});
const app = Sloppy.create();
app.post("/users", async (ctx) =>
  Results.created("/users/1", await ctx.body.validate(User))
).accepts(User);
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("default and pattern modifiers should extract");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("ctx.body.validate(undefined)"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    let user_schema = plan["schemas"]
        .as_array()
        .and_then(|schemas| schemas.iter().find(|schema| schema["name"] == "User"))
        .expect("User schema should exist");
    let name_property = &user_schema["definition"]["properties"]["name"];
    assert_eq!(name_property["pattern"], "^[a-z]+$");
    assert_eq!(name_property["patternFlags"], "u");
    assert_eq!(name_property["optional"], true);
    assert_eq!(name_property["default"], "guest");
}

#[test]
fn body_validate_undefined_keeps_validate_facade() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.post("/users", (ctx) => Results.json({ body: ctx.body.validate(undefined) }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("validate(undefined) should remain a supported sanitizer path");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("ctx.body.validate(undefined)"));
    assert!(!emitted_js.source.contains("ctx.body.json(undefined)"));
}

#[test]
fn computed_header_facade_access_materializes_headers_conservatively() {
    let source = r#"const handler = (ctx) => {
  const userAgent = ctx.header["userAgent"];
  return "ok";
};
"#;
    let allocator = oxc_allocator::Allocator::default();
    let source_type = oxc_span::SourceType::from_path(std::path::Path::new("app.js"))
        .expect("fixture source type should be supported");
    let parsed = oxc_parser::Parser::new(&allocator, source, source_type).parse();
    assert!(
        parsed.errors.is_empty(),
        "fixture should parse without errors: {:?}",
        parsed.errors
    );

    let function = parsed
        .program
        .body
        .iter()
        .find_map(|statement| {
            let Statement::VariableDeclaration(declaration) = statement else {
                return None;
            };
            let init = declaration.declarations.first()?.init.as_ref()?;
            let Expression::ArrowFunctionExpression(function) = init else {
                return None;
            };
            Some(function)
        })
        .expect("fixture should contain an arrow handler");
    let bindings = super::request_bindings_from_arrow(function, &BTreeSet::new());

    assert_eq!(bindings.len(), 1);
    assert_eq!(bindings[0].kind, "context");
    assert_eq!(bindings[0].name.as_deref(), Some("RequestContext"));
}

#[test]
fn extracts_bindings_for_named_context_parameter() {
    let source = r#"import { Sloppy, Results, schema } from "sloppy";
const UserCreate = schema.object({ name: schema.string() });
const app = Sloppy.create();
app.post("/users/{id:int}", (request) => Results.json({
  id: request.route.id,
  body: request.body.json(UserCreate)
}));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("named context fixture should extract");
    assert_eq!(app.routes[0].handler.bindings.len(), 2);
    assert_eq!(app.routes[0].handler.bindings[0].kind, "route");
    assert_eq!(
        app.routes[0].handler.bindings[0].name.as_deref(),
        Some("id")
    );
    assert_eq!(app.routes[0].handler.bindings[1].kind, "body.json");
    assert_eq!(
        app.routes[0].handler.bindings[1].schema.as_deref(),
        Some("UserCreate")
    );

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("request.body.json(undefined)"));
    assert!(!emitted_js.source.contains("request.body.json(UserCreate)"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["features"]["metadataInference"], true);
    assert_eq!(plan["routes"][0]["response"]["helper"], "json");
}

#[test]
fn extracts_body_schema_declared_after_route() {
    let source = r#"import { Sloppy, Results, schema } from "sloppy";
const app = Sloppy.create();
app.post("/users", (ctx) => Results.json({ body: ctx.body.json(UserCreate) }));
const UserCreate = schema.object({ name: schema.string() });
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("schema name prepass should make route order independent");
    assert_eq!(
        app.routes[0].handler.bindings[0].schema.as_deref(),
        Some("UserCreate")
    );
}

#[test]
fn emits_response_metadata_for_response_only_routes() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("response-only fixture should extract");
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["features"]["metadataInference"], true);
    assert_eq!(plan["features"]["strongPlanMetadata"], true);
    assert_eq!(plan["completeness"]["status"], "complete");
    assert_eq!(plan["routes"][0]["completeness"]["status"], "complete");
    assert_eq!(plan["routes"][0]["response"]["helper"], "text");
    assert_eq!(plan["sourceFiles"][0]["path"], "app.js");
    assert_eq!(plan["strongPlan"]["profile"], "compiler-30-strong-plan");
}

#[test]
fn ordinary_handlers_collect_multiple_return_response_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/users/{id:int}", async (ctx) => {
  const user = await loadUser(ctx.route.id);
  if (user === null) {
    return Results.notFound();
  }
  return Results.ok(user);
});
function loadUser(id) {
  return id === 1 ? { id } : null;
}
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("runnable branched handler should extract response metadata");
    let statuses = app.routes[0]
        .handler
        .responses
        .iter()
        .map(|response| response.status)
        .collect::<Vec<_>>();
    assert_eq!(statuses, vec![404, 200]);
}

#[test]
fn dynamic_status_result_does_not_emit_response_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/status", (ctx) => Results.status(ctx.route.code, { ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("dynamic status route should extract");
    assert!(app.routes[0].handler.response.is_none());
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("partial plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["completeness"]["status"], "partial");
    assert_eq!(
        plan["routes"][0]["completeness"]["reasons"][0]["code"],
        "response-metadata-missing"
    );
}

#[test]
fn body_json_without_schema_marks_route_partial() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.post("/users", (ctx) => Results.json({ body: ctx.body.json() }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("body json without schema should extract as partial metadata");
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("partial body plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["routes"][0]["completeness"]["status"], "partial");
    assert_eq!(
        plan["routes"][0]["completeness"]["reasons"][0]["code"],
        "body-schema-missing"
    );
}

#[test]
fn does_not_extract_schema_without_schema_import() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const UserCreate = schema.object({ name: schema.string() });
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("unbound local schema expression should stay outside Sloppy DSL");
    assert!(app.schemas.is_empty());
}

#[test]
fn rejects_invalid_schema_and_config_metadata() {
    let invalid_schema = r#"import { Sloppy, Results, schema } from "sloppy";
const UserCreate = schema.object(UserShape);
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), invalid_schema)
        .expect_err("invalid schema should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_SCHEMA");

    let invalid_config = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const key = "Sloppy:Server:Host";
const host = app.config.getString(key);
app.get("/", () => Results.text(host));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), invalid_config)
        .expect_err("invalid config key should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_CONFIG_KEY");

    let invalid_body_helper = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.post("/", (ctx) => Results.json({ form: ctx.body.formData() }));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), invalid_body_helper)
        .expect_err("invalid body helper should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE");

    let unknown_body_schema = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const options = {};
app.post("/", (ctx) => Results.json({ body: ctx.body.json(options) }));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), unknown_body_schema)
        .expect_err("unknown body schema identifier should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE");

    let invalid_schema_modifier = r#"import { Sloppy, Results, schema } from "sloppy";
const UserCreate = schema.object({ email: schema.string().email("strict") });
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), invalid_schema_modifier)
        .expect_err("schema modifier arity should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_SCHEMA");

    let conditional_schema = r#"import { Sloppy, Results, schema } from "sloppy";
const flag = true;
const UserCreate = flag ? schema.string() : schema.number();
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), conditional_schema)
        .expect_err("conditional schema should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_SCHEMA");

    let wrapped_schema = r#"import { Sloppy, Results, schema } from "sloppy";
const UserCreate = wrap(schema.string());
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), wrapped_schema)
        .expect_err("schema hidden in call arguments should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_SCHEMA");

    let object_schema = r#"import { Sloppy, Results, schema } from "sloppy";
const UserCreate = { value: schema.string() };
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), object_schema)
        .expect_err("schema hidden in object values should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_SCHEMA");

    let array_schema = r#"import { Sloppy, Results, schema } from "sloppy";
const UserCreate = [schema.string()][0];
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), array_schema)
        .expect_err("schema hidden in array elements should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_SCHEMA");
}

#[test]
fn extracts_route_metadata_without_runtime_execution() {
    let source = r#"import { Sloppy, Results, data } from "sloppy";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("users.db", { provider: "sqlite", access: "read" });
const app = builder.build();
app.mapPost("/users", async () => Results.json({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].method, "POST");
    assert!(app.routes[0].handler.is_async);
    assert_eq!(app.capabilities.len(), 1);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("const { Results, data }"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    assert!(emitted_source_map.contains("\"sourcesContent\""));
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    assert!(plan.contains("\"asyncHandlers\": true"));
    assert!(plan.contains("\"method\": \"POST\""));
    assert!(plan.contains("\"provider\": \"sqlite\""));
}

#[test]
fn data_backed_handlers_may_preserve_runtime_body_shape() {
    let source = r#"import { Sloppy, Results, data } from "sloppy";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.main", {
  provider: "sqlite",
  access: "readwrite",
  database: "users-api-sqlite-runtime.db",
});
const app = builder.build();
app.mapPost("/users", (ctx) => {
  const body = ctx.request.json();
  const db = data.sqlite("main");
  try {
    db.exec("create table if not exists users (id integer primary key, name text not null)", []);
    db.exec("insert into users (name) values (?)", [body.name]);
    return Results.created("/users/1", db.queryOne("select id, name from users where id = last_insert_rowid()", []));
  } finally {
    db.close();
  }
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("data-backed handler body should be preserved for runtime execution");
    assert_eq!(app.routes.len(), 1);
    assert!(app.routes[0]
        .handler
        .source
        .contains("data.sqlite(\"main\")"));
    assert!(app.routes[0].handler.source.contains("ctx.request.json()"));
    assert_eq!(app.capabilities.len(), 1);
}

#[test]
fn data_backed_body_json_with_extra_arguments_is_not_sanitized() {
    let source = r#"import { Sloppy, Results, data, schema } from "sloppy";
const UserCreate = schema.object({ name: schema.string() });
const opts = {};
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.main", {
  provider: "sqlite",
  access: "readwrite",
  database: "users-api-sqlite-runtime.db",
});
const app = builder.build();
app.mapPost("/users", (ctx) => Results.json({ body: ctx.body.json(UserCreate, opts) }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("data-backed handler body should be preserved for runtime execution");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("ctx.body.json(UserCreate, opts)"));
    assert!(!emitted_js.source.contains("ctx.body.json(undefined, opts)"));
}

#[test]
fn data_backed_body_schema_references_are_sanitized_in_control_flow() {
    let source = r#"import { Sloppy, Results, data, schema } from "sloppy";
const UserCreate = schema.object({ name: schema.string() });
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.main", {
  provider: "sqlite",
  access: "readwrite",
  database: "users-api-sqlite-runtime.db",
});
const app = builder.build();
app.mapPost("/users", async (ctx) => {
  const first = await ctx.body.json(UserCreate);
  if ((await ctx.body.json(UserCreate)).name) {
    for (
      let current = await ctx.body.json(UserCreate);
      current.name;
      current = await ctx.body.json(UserCreate)
    ) {
      break;
    }
  }
  try {
    return Results.json({ body: await ctx.body.json(UserCreate), first });
  } catch (error) {
    throw await ctx.body.json(UserCreate);
  }
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("data-backed handler body should extract");
    assert_eq!(app.routes[0].handler.bindings.len(), 1);
    assert_eq!(app.routes[0].handler.bindings[0].kind, "body.json");
    assert_eq!(
        app.routes[0].handler.bindings[0].schema.as_deref(),
        Some("UserCreate")
    );

    let emitted_js = super::emit_app_js(&app);
    assert_eq!(
        emitted_js
            .source
            .matches("ctx.body.json(undefined)")
            .count(),
        6
    );
    assert!(!emitted_js.source.contains("ctx.body.json(UserCreate)"));
}

#[test]
fn infers_direct_provider_read_effect_without_manual_uses() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", () => Results.json(db.query("select id, name from users", [])));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("direct provider read should infer effects");
    assert_eq!(app.capabilities[0].access, "read");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert_eq!(app.routes[0].handler.effects[0].provider, "data.main");
    assert_eq!(app.routes[0].handler.effects[0].capability_kind, "database");
    assert_eq!(app.routes[0].handler.effects[0].provider_kind, "sqlite");
    assert_eq!(app.routes[0].handler.effects[0].access, "read");
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["capabilities"][0]["access"], "read");
    assert_eq!(plan["capabilities"][0]["kind"], "database");
    assert_eq!(plan["dataProviders"][0]["capabilityKind"], "database");
    assert_eq!(plan["dataProviders"][0]["providerKind"], "sqlite");
    assert_eq!(
        plan["routes"][0]["effects"][0]["capabilityKind"],
        "database"
    );
    assert_eq!(plan["routes"][0]["effects"][0]["providerKind"], "sqlite");
    assert_eq!(plan["routes"][0]["effects"][0]["operation"], "query");
}

#[test]
fn infers_provider_write_and_readwrite_effects() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.post("/users", () => {
  db.exec("insert into users (name) values (?)", ["Ada"]);
  return Results.json(db.queryOne("select id, name from users where name = ?", ["Ada"]));
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("mixed provider usage should infer readwrite");
    assert_eq!(app.capabilities[0].access, "readwrite");
    assert_eq!(app.routes[0].handler.effects.len(), 2);
    assert!(app.routes[0]
        .handler
        .effects
        .iter()
        .any(|effect| effect.access == "write"));
    assert!(app.routes[0]
        .handler
        .effects
        .iter()
        .any(|effect| effect.access == "read"));
}

#[test]
fn provider_effect_model_is_database_provider_generic() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.analytics", { provider: "postgres", access: "readwrite" });
builder.capabilities.addDatabase("data.reporting", { provider: "sqlserver", access: "read" });
const app = builder.build();
const analytics = app.provider("postgres:analytics");
app.get("/analytics", () => Results.json({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("database provider metadata should not be sqlite-only");
    assert_eq!(app.capabilities.len(), 2);
    assert_eq!(app.capabilities[0].provider, "postgres");
    assert_eq!(app.capabilities[1].provider, "sqlserver");
    assert!(app.routes[0].handler.effects.is_empty());
    let binding = super::database_provider_binding_from_token("postgres:analytics")
        .expect("postgres binding should be recognized");
    assert_eq!(binding.capability_kind, "database");
    assert_eq!(binding.provider, "postgres");
    assert_eq!(binding.token, "data.analytics");
}

#[test]
fn rejects_non_sqlite_generated_provider_bridge_until_runtime_exists() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.analytics", { provider: "postgres", access: "readwrite" });
const app = builder.build();
const analytics = app.provider("postgres:analytics");
app.get("/analytics", () => Results.json(analytics.query("select id from events", [])));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("non-sqlite generated bridge should be rejected honestly");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE");
}

#[test]
fn rejects_provider_effect_without_registered_provider() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const db = app.provider("sqlite:main");
app.get("/users", () => Results.json(db.query("select id from users", [])));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("provider effects require a registered provider");
    assert_eq!(diagnostic.code, "SLOPPYC_E_MISSING_PROVIDER");
    assert!(diagnostic.message.contains("database provider"));
}

#[test]
fn infers_same_file_helper_effects_without_manual_uses() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
function listUsers() {
  return db.query("select id, name from users", []);
}
app.get("/users", () => Results.json(listUsers()));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("same-file helper should infer provider effects");
    assert_eq!(app.capabilities[0].access, "read");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function listUsers()"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
    assert!(!app.routes[0].handler.source.contains("uses"));
}

#[test]
fn helper_identifier_scanner_ignores_strings_comments_and_object_keys() {
    assert!(super::source_contains_identifier(
        "return Results.json(auth());",
        "auth"
    ));
    assert!(!super::source_contains_identifier(
        "return Results.json({ auth: true });",
        "auth"
    ));
    assert!(!super::source_contains_identifier(
        "return Results.text('auth'); // auth\n",
        "auth"
    ));
    assert!(!super::source_contains_identifier(
        "/* auth */ return Results.text(`auth`);",
        "auth"
    ));
    assert!(!super::source_contains_identifier(
        "return Results.json({ auth  : true });",
        "auth"
    ));
}

#[test]
fn same_file_helper_selection_ignores_non_code_identifier_matches() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
function auth() {
  throw new Error("auth helper should not be emitted");
}
app.get("/users", () => {
  // auth is mentioned in a comment only.
  return Results.json({ auth: true, label: "auth", note: `auth` });
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("unreferenced helper names in metadata text should not poison extraction");
    assert!(!app.routes[0]
        .handler
        .emitted_source
        .contains("function auth"));
}

#[test]
fn helper_dependency_selection_ignores_local_bindings_and_parameters() {
    let helper_sources = BTreeMap::from([
        (
            "buildResponse".to_string(),
            "function buildResponse(formatValue) { const normalizeUser = formatValue; return normalizeUser({ ok: true }); }".to_string(),
        ),
        (
            "formatValue".to_string(),
            "function formatValue(value) { return value; }".to_string(),
        ),
        (
            "normalizeUser".to_string(),
            "function normalizeUser(value) { return value; }".to_string(),
        ),
    ]);

    let selected = super::helper_sources_referenced_by_handler(
        "() => Results.json(buildResponse((value) => value))",
        &helper_sources,
    );

    assert_eq!(selected.len(), 1);
    assert!(selected[0].contains("function buildResponse"));
    assert!(!selected[0].contains("function formatValue"));
    assert!(!selected
        .iter()
        .any(|source| source.contains("function normalizeUser")));
}

#[test]
fn helper_dependency_selection_orders_referenced_initializer_helpers() {
    let helper_sources = BTreeMap::from([
        (
            "OrderCreated".to_string(),
            "const OrderCreated = Webhooks.event(\"order.created\", { version: 1, schema: OrderSchema });".to_string(),
        ),
        (
            "OrderSchema".to_string(),
            "const OrderSchema = schema.object({ id: schema.string() });".to_string(),
        ),
    ]);

    let selected = super::helper_sources_in_dependency_order(&helper_sources);

    assert_eq!(selected.len(), 2);
    assert!(selected[0].contains("const OrderSchema"));
    assert!(selected[1].contains("const OrderCreated"));
}

#[test]
fn helper_dependency_selection_follows_calls_in_variable_initializers() {
    let helper_sources = BTreeMap::from([
        (
            "findTodo".to_string(),
            "async function findTodo(db, id) { return db.queryOne(\"select id from todos where id = ?\", [id]); }".to_string(),
        ),
        (
            "updateTodo".to_string(),
            "async function updateTodo(db, id) { const existing = await findTodo(db, id); return existing; }".to_string(),
        ),
    ]);

    let selected =
        super::helper_sources_referenced_by_handler("() => updateTodo(db, 1)", &helper_sources);

    assert_eq!(selected.len(), 2);
    assert!(selected[0].contains("async function findTodo"));
    assert!(selected[1].contains("async function updateTodo"));
}

#[test]
fn infers_provider_effects_inside_control_flow() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", (ctx) => {
  if (ctx.query.write) {
    db.exec("insert into users (name) values (?)", ["Ada"]);
  }
  return Results.json(db.query("select id from users", []));
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("control-flow provider calls should infer effects");
    assert_eq!(app.capabilities[0].access, "readwrite");
    assert!(app.routes[0]
        .handler
        .effects
        .iter()
        .any(|effect| effect.access == "write"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
}

#[test]
fn resolves_multi_hop_helper_effects() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
function readUsers() {
  return db.query("select id from users", []);
}
function listUsers() {
  return readUsers();
}
app.get("/users", () => Results.json(listUsers()));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("multi-hop helper effects should be resolved");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert_eq!(app.routes[0].handler.effects[0].access, "read");
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
}

#[test]
fn infers_relative_helper_effects_with_provider_arguments() {
    let root = fixture_temp_dir("relative-helper-provider-effects");
    fs::write(
        root.join("usersRepository.ts"),
        r#"export function listUsers(db) {
  return db.query("select id, name from users", []);
}
"#,
    )
    .expect("helper should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { listUsers } from "./usersRepository";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", () => Results.json(listUsers(db)));
export default app;
"#;
    let app =
        extract_temp_input(&root, source).expect("relative helper should infer provider effects");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert_eq!(app.routes[0].handler.effects[0].access, "read");
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function listUsers(db)"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
}

#[test]
fn function_module_can_use_relative_provider_helpers() {
    let root = fixture_temp_dir("function-module-relative-provider-helpers");
    fs::create_dir_all(root.join("routes")).expect("routes directory should be writable");
    fs::write(
        root.join("usersRepository.ts"),
        r#"export function listUsers(db) {
  return db.query("select id, name from users", []);
}
"#,
    )
    .expect("repository helper should be writable");
    fs::write(
        root.join("routes").join("users.ts"),
        r#"import { Results } from "sloppy";
import { listUsers } from "../usersRepository";

export function usersModule(app) {
  const db = app.provider("sqlite:main");
  app.get("/users", () => Results.json(listUsers(db)));
}
"#,
    )
    .expect("route module should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { usersModule } from "./routes/users";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
app.useModule(usersModule);
export default app;
"#;
    let app = extract_temp_input(&root, source)
        .expect("function modules should use relative provider helpers");
    assert_eq!(app.routes[0].pattern, "/users");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function listUsers(db)"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
}

#[test]
fn function_module_can_use_installed_package_helpers() {
    let root = fixture_temp_dir("function-module-package-helpers");
    fs::create_dir_all(root.join("src").join("routes"))
        .expect("routes directory should be writable");
    fs::create_dir_all(root.join("node_modules").join("validator-lite"))
        .expect("package directory should be writable");
    fs::write(
        root.join("node_modules")
            .join("validator-lite")
            .join("package.json"),
        r#"{"name":"validator-lite","version":"0.0.0","type":"module","exports":"./index.js"}"#,
    )
    .expect("package manifest should be writable");
    fs::write(
        root.join("node_modules")
            .join("validator-lite")
            .join("index.js"),
        r#"export function normalizeName(value) {
  return String(value || "").trim();
}

export function isUserName(value) {
  return typeof value === "string" && value.length >= 2;
}
"#,
    )
    .expect("package entry should be writable");
    fs::write(
        root.join("src").join("routes").join("users.ts"),
        r#"import { Results } from "sloppy";
import { isUserName, normalizeName } from "validator-lite";

export function usersModule(app) {
  app.get("/users/{name}", (ctx) => {
    const name = normalizeName(ctx.route.name);
    return Results.ok({ name, valid: isUserName(name) });
  });
}
"#,
    )
    .expect("route module should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { usersModule } from "./routes/users";
const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let input = root.join("src").join("main.ts");
    fs::write(&input, source).expect("input should be writable");
    let app = extract(&input, source).expect("function module package helpers should extract");
    assert_eq!(app.routes[0].pattern, "/users/{name}");
    assert!(app.dependency_graph.has_entries());
    assert!(app
        .dependency_graph
        .packages
        .iter()
        .any(|package| package.name == "validator-lite"));
    let users_module = app
        .dependency_graph
        .modules
        .iter()
        .find(|module| module.id.replace('\\', "/").ends_with("routes/users.ts"))
        .expect("route module should be recorded in dependency graph");
    assert!(users_module
        .resolved_imports
        .iter()
        .any(|import| { import.specifier == "validator-lite" && import.kind == "package" }));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function normalizeName(value)"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function isUserName(value)"));
}

#[test]
fn function_module_records_cross_file_helper_dependency_graph() {
    let root = fixture_temp_dir("function-module-cross-file-helper-graph");
    fs::create_dir_all(root.join("src").join("routes")).expect("routes directory should exist");
    fs::create_dir_all(root.join("src").join("services")).expect("services directory should exist");
    fs::create_dir_all(root.join("src").join("db")).expect("db directory should exist");
    fs::create_dir_all(root.join("src").join("models")).expect("models directory should exist");
    fs::create_dir_all(root.join("src").join("utils")).expect("utils directory should exist");
    fs::write(
        root.join("src").join("routes").join("users.ts"),
        r#"import { Results } from "sloppy";
import { listUsers } from "../services/usersService";

function describeCount(count) {
  return `users:${count}`;
}

export function usersModule(app) {
  app.get("/users", () => {
    const users = listUsers();
    return Results.ok({ summary: describeCount(users.length), users });
  });
}
"#,
    )
    .expect("route module should be writable");
    fs::write(
        root.join("src").join("services").join("usersService.ts"),
        r#"import { repoListUsers } from "../db/usersRepository";
import { label } from "../utils/text";

export function listUsers() {
  return repoListUsers().map((user) => ({ ...user, label: label(user.name) }));
}
"#,
    )
    .expect("service helper should be writable");
    fs::write(
        root.join("src").join("db").join("usersRepository.ts"),
        r#"import { toUser } from "../models/user";

export function repoListUsers() {
  return [toUser({ id: 1, name: "ada" })];
}
"#,
    )
    .expect("repository helper should be writable");
    fs::write(
        root.join("src").join("models").join("user.ts"),
        r#"import { title } from "../utils/text";

export function toUser(row) {
  return { id: row.id, name: title(row.name) };
}
"#,
    )
    .expect("model helper should be writable");
    fs::write(
        root.join("src").join("utils").join("text.ts"),
        r#"export function title(value) {
  return String(value || "").toUpperCase();
}

export function label(value) {
  return `user:${title(value)}`;
}
"#,
    )
    .expect("utility helper should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { usersModule } from "./routes/users";

const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let input = root.join("src").join("main.ts");
    fs::write(&input, source).expect("input should be writable");
    let app = extract(&input, source)
        .expect("resolved cross-file web app should emit runnable partial metadata");

    assert_eq!(app.routes[0].pattern, "/users");
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function describeCount(count)"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function repoListUsers()"));
    let modules = &app.dependency_graph.modules;
    let has_edge = |from_suffix: &str, specifier: &str, to_suffix: &str| {
        modules.iter().any(|module| {
            module.id.replace('\\', "/").ends_with(from_suffix)
                && module.resolved_imports.iter().any(|import| {
                    import.specifier == specifier
                        && import.kind == "relative"
                        && import.resolved_id.replace('\\', "/").ends_with(to_suffix)
                })
        })
    };
    assert!(has_edge("main.ts", "./routes/users", "routes/users.ts"));
    assert!(has_edge(
        "routes/users.ts",
        "../services/usersService",
        "services/usersService.ts"
    ));
    assert!(has_edge(
        "services/usersService.ts",
        "../db/usersRepository",
        "db/usersRepository.ts"
    ));
    assert!(has_edge(
        "db/usersRepository.ts",
        "../models/user",
        "models/user.ts"
    ));
    assert!(has_edge("models/user.ts", "../utils/text", "utils/text.ts"));
}

#[test]
fn resolves_nested_relative_helpers_before_effect_inference() {
    let root = fixture_temp_dir("nested-relative-helper-effects");
    fs::write(
        root.join("queries.ts"),
        r#"export const selectUsers = "select id, name from users";"#,
    )
    .expect("query helper should be writable");
    fs::write(
        root.join("usersRepository.ts"),
        r#"import { selectUsers } from "./queries";
export function listUsers(db) {
  return db.query(selectUsers, []);
}
"#,
    )
    .expect("repository helper should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { listUsers } from "./usersRepository";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", () => Results.json(listUsers(db)));
export default app;
"#;
    let app = extract_temp_input(&root, source)
        .expect("nested relative helpers should be inlined before effect inference");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("const selectUsers = \"select id, name from users\";"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function listUsers(db)"));
}

#[test]
fn imported_route_helpers_include_private_same_module_dependencies() {
    let root = fixture_temp_dir("imported-helper-private-dependencies");
    fs::create_dir_all(root.join("routes")).expect("routes dir should be writable");
    fs::write(
        root.join("db.js"),
        r#"export function ensureSchema(db) {
  return db.exec("create table if not exists todos (id integer primary key, title text)", []);
}
function toTodo(row) {
  return row === null ? null : { id: Number(row.id), title: String(row.title) };
}
export async function seedTodos(db) {
  await ensureSchema(db);
  await db.exec("insert into todos (id, title) select ?, ? where not exists (select 1 from todos where id = ?)", [1, "seed", 1]);
}
export async function listTodos(db) {
  await seedTodos(db);
  const rows = await db.query("select id, title from todos order by id", []);
  return rows.map(toTodo);
}
export async function createTodo(db, title) {
  await ensureSchema(db);
  await db.exec("insert into todos (title) values (?)", [title]);
  return toTodo(await db.queryOne("select id, title from todos where id = last_insert_rowid()", []));
}
export async function findTodo(db, id) {
  await ensureSchema(db);
  return toTodo(await db.queryOne("select id, title from todos where id = ?", [id]));
}
export async function updateTodo(db, id, title) {
  const existing = await findTodo(db, id);
  if (existing === null) {
    return null;
  }
  await db.exec("update todos set title = ? where id = ?", [title, id]);
  return findTodo(db, id);
}
"#,
    )
    .expect("db helper should be writable");
    fs::write(
        root.join("routes").join("todos.js"),
        r#"import { Results } from "sloppy";
import { createTodo, listTodos, updateTodo } from "../db.js";
export function todosModule(app) {
  const db = app.provider("sqlite:main");
  app.get("/todos", async () => Results.json(await listTodos(db)));
  app.post("/todos", async () => Results.created("/todos/1", await createTodo(db, "alpha")));
  app.put("/todos/{id:int}", async (ctx) => Results.json(await updateTodo(db, ctx.route.id, "updated")));
}
"#,
    )
    .expect("route module should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { todosModule } from "./routes/todos.js";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
app.useModule(todosModule);
export default app;
"#;
    let app = extract_temp_input(&root, source)
        .expect("private helper dependencies should be included in emitted route handlers");
    let emitted = app
        .routes
        .iter()
        .map(|route| route.handler.emitted_source.as_str())
        .collect::<Vec<_>>()
        .join("\n");
    assert!(emitted.contains("function ensureSchema(db)"));
    assert!(emitted.contains("function toTodo(row)"));
    assert!(emitted.contains("async function seedTodos(db)"));
    assert!(emitted.contains("async function listTodos(db)"));
    assert!(emitted.contains("async function createTodo(db, title)"));
    assert!(emitted.contains("async function findTodo(db, id)"));
    assert!(emitted.contains("async function updateTodo(db, id, title)"));
    assert!(emitted.contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
}

#[test]
fn imported_route_helpers_do_not_leak_unused_shadowed_private_helpers() {
    let root = fixture_temp_dir("imported-helper-shadowed-private-helper");
    fs::create_dir_all(root.join("routes")).expect("routes dir should be writable");
    fs::write(
        root.join("db.js"),
        r#"function audit(db) {
  return db.exec("insert into audit_log (message) values (?)", ["unused"]);
}
export function listTodos(db) {
  return db.query("select id, title from todos order by id", []);
}
"#,
    )
    .expect("db helper should be writable");
    fs::write(
        root.join("routes").join("todos.js"),
        r#"import { Results } from "sloppy";
import { listTodos } from "../db.js";
export function todosModule(app) {
  const db = app.provider("sqlite:main");
  app.get("/todos", () => {
    const audit = () => "local";
    audit();
    return Results.json(listTodos(db));
  });
}
"#,
    )
    .expect("route module should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { todosModule } from "./routes/todos.js";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
app.useModule(todosModule);
export default app;
"#;
    let app = extract_temp_input(&root, source)
        .expect("unused private imported helpers should not leak into route extraction");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert_eq!(app.routes[0].handler.effects[0].access, "read");
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function listTodos(db)"));
    assert!(!app.routes[0]
        .handler
        .emitted_source
        .contains("function audit(db)"));
}

#[test]
fn imported_route_helpers_reject_ambiguous_private_dependency_names() {
    let root = fixture_temp_dir("imported-helper-ambiguous-private-dependency");
    fs::create_dir_all(root.join("routes")).expect("routes dir should be writable");
    fs::write(
        root.join("todosRepository.js"),
        r#"function toRow(row) {
  return { id: Number(row.id), title: String(row.title) };
}
export function listTodos(db) {
  return db.query("select id, title from todos order by id", []).map(toRow);
}
"#,
    )
    .expect("todos repository helper should be writable");
    fs::write(
        root.join("auditRepository.js"),
        r#"function toRow(row) {
  return { id: Number(row.id), message: String(row.message) };
}
export function listAudits(db) {
  return db.query("select id, message from audit_log order by id", []).map(toRow);
}
"#,
    )
    .expect("audit repository helper should be writable");
    fs::write(
        root.join("routes").join("todos.js"),
        r#"import { Results } from "sloppy";
import { listAudits } from "../auditRepository.js";
import { listTodos } from "../todosRepository.js";
export function todosModule(app) {
  const db = app.provider("sqlite:main");
  app.get("/todos", () => Results.json(listTodos(db)));
  app.get("/audits", () => Results.json(listAudits(db)));
}
"#,
    )
    .expect("route module should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { todosModule } from "./routes/todos.js";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
app.useModule(todosModule);
export default app;
"#;
    let diagnostic = extract_temp_input(&root, source)
        .expect_err("ambiguous private imported helper names should fail closed");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HELPER");
    assert!(diagnostic
        .message
        .contains("ambiguous imported helper name \"toRow\""));
}

#[test]
fn preindexes_later_function_helpers_before_route_extraction() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", () => Results.json(listUsers()));
function listUsers() {
  return db.query("select id from users", []);
}
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("later helper declaration should be indexed before routes");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
}

#[test]
fn rejects_unrelated_closed_over_values_when_provider_exists_elsewhere() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
const config = { message: "hello" };
app.get("/message", () => Results.text(config.message));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("unrelated closed-over state should not be accepted");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE");
}

#[test]
fn rejects_unknown_provider_handle_usage() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", () => Results.json(db.prepare("select id from users")));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("unknown provider method should fail closed");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE");
}

#[test]
fn infers_provider_effects_inside_expression_wrappers() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", (ctx) => Results.json(ctx.query.all ? db.query("select id from users", []) : []));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("conditional provider calls should infer effects");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert_eq!(app.routes[0].handler.effects[0].access, "read");
}

#[test]
fn manual_database_capability_overrides_provider_use_duplicate() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.main", { provider: "sqlite", access: "readwrite", database: ":memory:" });
const app = builder.build();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", () => Results.json(db.query("select id from users", [])));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("manual capability should override synthetic provider use");
    assert_eq!(app.capabilities.len(), 1);
    assert_eq!(app.capabilities[0].access, "readwrite");
    assert!(!app.capabilities[0].from_provider_use);
}

#[test]
fn detects_dynamic_import_inside_function_helper() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
function loadPlugin() {
  return import("./plugin.js");
}
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("dynamic import in helper should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_DYNAMIC_IMPORT");
}

#[test]
fn classifies_with_sql_as_write_by_default() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.post("/users", () => {
  db.exec("with input(name) as (values ('Ada')) insert into users(name) select name from input", []);
  return Results.noContent();
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("WITH SQL should default to write access");
    assert_eq!(app.routes[0].handler.effects[0].access, "write");
    assert_eq!(app.capabilities[0].access, "write");
}

#[test]
fn database_capability_accepts_matching_path_alias() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.main", {
  provider: "sqlite",
  access: "readwrite",
  database: ":memory:",
  path: ":memory:",
});
const app = builder.build();
app.mapGet("/ok", () => Results.ok({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert_eq!(app.capabilities.len(), 1);
    assert_eq!(app.capabilities[0].database.as_deref(), Some(":memory:"));

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    assert!(plan.contains("\"database\": \":memory:\""));
}

#[test]
fn database_capability_accepts_path_alias_only() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.main", {
  provider: "sqlite",
  access: "readwrite",
  path: ":memory:",
});
const app = builder.build();
app.mapGet("/ok", () => Results.ok({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert_eq!(app.capabilities.len(), 1);
    assert_eq!(app.capabilities[0].database.as_deref(), Some(":memory:"));

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    assert!(plan.contains("\"database\": \":memory:\""));
}

#[test]
fn sloppy_fs_import_emits_plan_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { File, Directory, Path } from "sloppy/fs";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/fs import should be recognized");
    assert!(app.uses_fs_runtime);

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(value["requiredFeatures"], serde_json::json!(["stdlib.fs"]));
    assert_eq!(value["features"]["fileSystem"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["filesystem"],
        serde_json::json!(true)
    );
}

#[test]
fn sloppy_time_import_emits_plan_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Time, Deadline, CancellationController } from "sloppy/time";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/time import should be recognized");
    assert!(app.uses_time_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
            "const { Results, Time, Deadline, CancellationController, TimeoutError, CancelledError, InvalidDeadlineError, TimerDisposedError } = __sloppyRuntime;"
        ));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["requiredFeatures"],
        serde_json::json!(["stdlib.time"])
    );
    assert_eq!(value["features"]["time"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["time"],
        serde_json::json!(true)
    );
}

#[test]
fn sloppy_crypto_import_emits_plan_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Random, Hash, Hmac, Password, ConstantTime, Secret, NonCryptoHash } from "sloppy/crypto";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/crypto import should be recognized");
    assert!(app.uses_crypto_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
            "const { Results, Random, Hash, Hmac, Password, ConstantTime, Secret, NonCryptoHash } = __sloppyRuntime;"
        ));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["requiredFeatures"],
        serde_json::json!(["stdlib.crypto"])
    );
    assert_eq!(value["features"]["crypto"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["crypto"],
        serde_json::json!(true)
    );
}

#[test]
fn sloppy_crypto_noncrypto_hash_security_context_emits_doctor_warning() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { NonCryptoHash } from "sloppy/crypto";
const app = Sloppy.create();
const tokenHash = NonCryptoHash.xxHash64("token");
app.mapGet("/token", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/crypto import should be recognized");
    assert!(app.noncrypto_hash_security_context_visible);

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["strongPlan"]["evidence"]["nonCryptoHashSecurityContext"],
        serde_json::json!(true)
    );
    assert_eq!(
        value["doctorChecks"][0]["id"],
        serde_json::json!("stdlib.crypto.noncrypto_hash.security_context")
    );
    assert_eq!(
        value["doctorChecks"][0]["status"],
        serde_json::json!("warn")
    );
}

#[test]
fn sloppy_crypto_noncrypto_hash_security_context_scans_tokens() {
    assert!(noncrypto_hash_security_context_visible(
        r#"const tokenHash = NonCryptoHash . xxHash64(value);"#
    ));
    assert!(!noncrypto_hash_security_context_visible(
        r#"const machineId = NonCryptoHash.xxHash64(value);"#
    ));
    assert!(!noncrypto_hash_security_context_visible(
        r#"const cacheHash = NonCryptoHash.xxHash64("token");"#
    ));
    assert!(!noncrypto_hash_security_context_visible(
        r#"const tokenHash = NonCryptoHashxxHash64(value);"#
    ));
}

#[test]
fn sloppy_crypto_import_alias_is_rejected() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Random as R } from "sloppy/crypto";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("aliased sloppy/crypto import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic
        .message
        .contains("unsupported sloppy import \"Random\""));
}

#[test]
fn sloppy_net_import_emits_plan_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { TcpClient, TcpListener, TcpConnection, LocalEndpoint, UnixSocket, NamedPipe, NetworkAddress, SloppyNetError } from "sloppy/net";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/net import should be recognized");
    assert!(app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
            "const { Results, TcpClient, TcpListener, TcpConnection, LocalEndpoint, UnixSocket, NamedPipe, NetworkAddress, SloppyNetError } = __sloppyRuntime;"
        ));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(value["requiredFeatures"], serde_json::json!(["stdlib.net"]));
    assert_eq!(value["features"]["network"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["network"],
        serde_json::json!(true)
    );
}

#[test]
fn sloppy_os_import_emits_plan_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { System, Environment, Process, ProcessHandle, Signals, OsError } from "sloppy/os";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/os import should be recognized");
    assert!(app.uses_os_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const { Results, System, Environment, Process, ProcessHandle, Signals, OsError } = __sloppyRuntime;"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(value["requiredFeatures"], serde_json::json!(["stdlib.os"]));
    assert_eq!(value["features"]["os"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["os"],
        serde_json::json!(true)
    );
}

#[test]
fn sloppy_net_http_client_import_emits_http_client_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { HttpClient } from "sloppy/net";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/net HttpClient import should be recognized");
    assert!(app.uses_http_client_runtime);
    assert!(!app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("HttpClient"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["requiredFeatures"],
        serde_json::json!(["stdlib.httpclient"])
    );
    assert_eq!(value["features"]["httpClient"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["httpClient"],
        serde_json::json!(true)
    );
    assert_eq!(
        value["doctorChecks"][0]["id"],
        serde_json::json!("stdlib.httpclient.contract")
    );
}

#[test]
fn sloppy_http_import_emits_http_client_required_feature() {
    let source = r#"import { Sloppy, Results, schema } from "sloppy";
import { Http, HttpClientFactory, TestHttp } from "sloppy/http";
const ignored = "Http.client(\"from-string\", { baseUrl: \"https://string.invalid\" })";
// Http.client("from-line-comment", { baseUrl: "https://line-comment.invalid" });
/* Http.typedClient("from-block-comment", { baseUrl: "https://block-comment.invalid" }); */
function ignoredBoundaryProbe() {
  MyHttp.client("not-sloppy-http", { baseUrl: "https://myhttp.invalid" });
  TestHttp.mock().get("/invoices/inv_1").replyJson(200, { id: "inv_1" });
}
const Invoice = schema.object({ id: schema.string() });
const Billing = Http.typedClient("billing", {
  baseUrl: "https://billing.example.test",
  endpoints: {
    getInvoice: Http.get("/invoices/{id}")
      .returns(200, Invoice),
    invalidStatus: Http.get("/invalid-status")
      .returns(999, Invoice),
  },
});
const factory = HttpClientFactory.create({ clients: [Billing] });
const app = Sloppy.create();
app.mapGet("/", () => Results.text(factory.get("billing").name));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/http import should be recognized");
    assert!(app.uses_http_client_runtime);
    assert!(!app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    for expected_export in [
        "HttpClient",
        "Http",
        "HttpClientFactory",
        "HttpError",
        "SloppyHttpClientError",
        "TestHttp",
    ] {
        assert!(emitted_js.source.contains(expected_export));
    }
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["requiredFeatures"],
        serde_json::json!(["stdlib.httpclient"])
    );
    assert_eq!(value["features"]["httpClient"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["httpClient"],
        serde_json::json!(true)
    );
    assert_eq!(value["httpClients"].as_array().unwrap().len(), 1);
    assert_eq!(
        value["httpClients"][0]["name"],
        serde_json::json!("billing")
    );
    assert_eq!(
        value["httpClients"][0]["target"],
        serde_json::json!("static")
    );
    assert_eq!(
        value["httpClients"][0]["baseUrl"],
        serde_json::json!("https://billing.example.test")
    );
    assert_eq!(
        value["httpClients"][0]["endpoints"][0]["method"],
        serde_json::json!("GET")
    );
    assert_eq!(
        value["httpClients"][0]["endpoints"][0]["path"],
        serde_json::json!("/invoices/{id}")
    );
    assert_eq!(
        value["strongPlan"]["evidence"]["httpClients"],
        serde_json::json!(true)
    );
    assert!(value["httpClients"]
        .as_array()
        .unwrap()
        .iter()
        .all(|client| client["name"] != "not-sloppy-http"));
    assert!(value["httpClients"][0]["endpoints"]
        .as_array()
        .unwrap()
        .iter()
        .all(|endpoint| endpoint["returns"]
            .as_array()
            .unwrap()
            .iter()
            .all(|item| item["status"] != serde_json::json!(0)
                && item["status"] != serde_json::json!(999))));
}

#[test]
fn combined_runtime_features_do_not_emit_webhooks_without_webhooks_import() {
    let source = r#"import { Sloppy, Results, data } from "sloppy";
import { Random } from "sloppy/crypto";
import { Http } from "sloppy/http";
import { WorkerPool } from "sloppy/workers";
const client = Http.client("outbound", { baseUrl: "https://example.test" });
const app = Sloppy.create();
app.mapGet("/", () => Results.text(`${Random.uuid()} ${client.name} ${WorkerPool} ${data}`));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("combined runtime feature imports should extract");
    assert!(app.uses_data_runtime);
    assert!(app.uses_crypto_runtime);
    assert!(app.uses_http_client_runtime);
    assert!(app.uses_workers_runtime);
    assert!(!app.uses_webhooks_runtime);

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");
    assert!(value["features"].get("webhooks").is_none());
    assert!(value.get("webhooks").is_none());
    assert!(!value["requiredFeatures"]
        .as_array()
        .expect("requiredFeatures should exist")
        .iter()
        .any(|entry| entry == "stdlib.webhooks"));
}

#[test]
fn webhooks_import_emits_webhooks_plan_metadata() {
    let source = r#"import { Sloppy, Results, Webhooks, schema } from "sloppy";
const OrderSchema = schema.object({ id: schema.string() });
const OrderCreated = Webhooks.event("order.created", {
  version: 1,
  schema: OrderSchema,
});
const app = Sloppy.create();
app.mapGet("/", () => Results.json({ event: OrderCreated.name }));
export default app;
"#;
    let app =
        extract(std::path::Path::new("app.js"), source).expect("webhooks import should extract");
    assert!(app.uses_webhooks_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("Results"));
    assert!(emitted_js.source.contains("schema"));
    assert!(emitted_js.source.contains("Schema"));
    assert!(emitted_js.source.contains("Webhooks"));
    let schema_offset = emitted_js
        .source
        .find("const OrderSchema = schema.object")
        .expect("schema helper should be emitted");
    let event_offset = emitted_js
        .source
        .find("const OrderCreated = Webhooks.event")
        .expect("webhook event helper should be emitted");
    assert!(
        schema_offset < event_offset,
        "schema helper should be emitted before the webhook event descriptor that references it"
    );
    assert!(emitted_js
        .source
        .contains("Results.json({ event: OrderCreated.name })"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");
    assert_eq!(value["features"]["webhooks"], serde_json::json!(true));
    assert!(value["requiredFeatures"]
        .as_array()
        .expect("requiredFeatures should exist")
        .iter()
        .any(|entry| entry == "stdlib.webhooks"));
    assert_eq!(value["webhooks"]["enabled"], serde_json::json!(true));
}

#[test]
fn dynamic_sloppy_http_client_emits_partial_plan_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Http } from "sloppy/http";
const clientName = "billing";
const baseUrl = "https://billing.example.test";
const billing = Http.client(clientName, { baseUrl });
const app = Sloppy.create();
app.mapGet("/", () => Results.text(billing.name));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("dynamic sloppy/http client should still compile");
    assert!(app.uses_http_client_runtime);

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["httpClients"][0]["target"],
        serde_json::json!("dynamic")
    );
    assert_eq!(value["httpClients"][0]["kind"], serde_json::json!("named"));
    assert_eq!(
        value["strongPlan"]["evidence"]["httpClients"],
        serde_json::json!(false)
    );
    assert!(value["doctorChecks"]
        .as_array()
        .unwrap()
        .iter()
        .any(
            |check| check["id"] == "stdlib.httpclient.dynamic-targets" && check["status"] == "warn"
        ));
}

#[test]
fn sloppy_workers_import_emits_plan_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { BackgroundService, WorkQueue, WorkerPool, Worker } from "sloppy/workers";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/workers import should be recognized");
    assert!(app.uses_workers_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
            "const { Results, BackgroundService, WorkQueue, WorkerPool, Worker, WorkerCancellationController, WorkerCancellationSignal, SloppyWorkerError } = __sloppyRuntime;"
        ));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["requiredFeatures"],
        serde_json::json!(["stdlib.workers"])
    );
    assert_eq!(value["features"]["workers"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["workers"],
        serde_json::json!(true)
    );
    assert!(value.get("workers").is_none());
    assert!(value.get("doctorChecks").is_none());
}

#[test]
fn sloppy_workers_type_only_import_does_not_require_runtime_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import type { WorkerPool } from "sloppy/workers";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("type-only sloppy/workers import should be recognized");
    assert!(!app.uses_workers_runtime);
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");
    assert!(value.get("requiredFeatures").is_none());
}

#[test]
fn typed_framework_queue_injection_infers_capability_and_default_service() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import type { WorkQueue } from "sloppy/workers";
const app = Sloppy.create();
app.post("/emails", async (emails: WorkQueue<"emails">) => Results.ok({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed queue injection should extract");
    assert!(app.uses_workers_runtime);
    assert!(app.capabilities.iter().any(|capability| {
        capability.token == "queue.emails"
            && capability.capability_kind == "queue"
            && capability.access == "enqueue"
    }));

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
            "__sloppy_framework_services.addSingleton(\"queue.emails\", () => WorkQueue.create(\"emails\"));"
        ));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");
    assert_eq!(
        value["requiredFeatures"],
        serde_json::json!(["stdlib.workers"])
    );
    assert_eq!(
        value["capabilities"][0],
        serde_json::json!({
            "access": "enqueue",
            "kind": "queue",
            "source": {
                "column": 28,
                "line": 4,
                "path": "app.ts"
            },
            "token": "queue.emails"
        })
    );
}

#[test]
fn sloppy_codec_import_emits_plan_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Base64, Base64Url, Hex, Text, Binary, Compression, Checksums } from "sloppy/codec";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/codec import should be recognized");
    assert!(app.uses_codec_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
            "const { Results, Base64, Base64Url, Hex, Text, Binary, Compression, Checksums } = __sloppyRuntime;"
        ));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["requiredFeatures"],
        serde_json::json!(["stdlib.codec"])
    );
    assert_eq!(value["features"]["codec"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["codec"],
        serde_json::json!(true)
    );
}

#[test]
fn sloppy_codec_checksum_security_context_emits_doctor_warning() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Checksums } from "sloppy/codec";
const app = Sloppy.create();
const tokenChecksum = Checksums.crc32(Text.utf8.encode("token"));
app.mapGet("/token", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/codec import should be recognized");
    assert!(app.checksum_security_context_visible);

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["strongPlan"]["evidence"]["checksumSecurityContext"],
        serde_json::json!(true)
    );
    assert_eq!(
        value["doctorChecks"][0]["id"],
        serde_json::json!("stdlib.codec.checksum.security_context")
    );
    assert_eq!(
        value["doctorChecks"][0]["status"],
        serde_json::json!("warn")
    );
}

#[test]
fn sloppy_codec_checksum_security_context_flows_from_relative_module() {
    let root = fixture_temp_dir("codec-checksum-module");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("checks.js"),
        r#"import { Results } from "sloppy";
import { Checksums } from "sloppy/codec";

export function tokenChecksumModule(app) {
    const tokenChecksum = Checksums.crc32(new Uint8Array([1, 2, 3]));
    app.get("/token-checksum", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");

    let source = r#"import { Sloppy, Results } from "sloppy";
import { tokenChecksumModule } from "./modules/checks.js";

const app = Sloppy.create();
app.useModule(tokenChecksumModule);
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert!(app.uses_codec_runtime);
    assert!(app.checksum_security_context_visible);

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");
    assert_eq!(
        value["strongPlan"]["evidence"]["checksumSecurityContext"],
        serde_json::json!(true)
    );
    assert!(value["doctorChecks"]
        .as_array()
        .is_some_and(|checks| checks.iter().any(|check| check["id"]
            == serde_json::json!("stdlib.codec.checksum.security_context")
            && check["status"] == serde_json::json!("warn"))));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn sloppy_codec_checksum_security_context_scans_tokens() {
    assert!(checksum_security_context_visible(
        r#"const tokenChecksum = Checksums . crc32(value);"#
    ));
    assert!(!checksum_security_context_visible(
        r#"const tokenChecksum = checksums.crc32(value);"#
    ));
    assert!(!checksum_security_context_visible(
        r#"const cacheChecksum = Checksums.crc32(value);"#
    ));
    assert!(!checksum_security_context_visible(
        r#"const cacheChecksum = Checksums.crc32("token");"#
    ));
    assert!(!checksum_security_context_visible(
        r#"const tokenChecksum = Checksumscrc32(value);"#
    ));
}

#[test]
fn sloppy_codec_import_alias_is_rejected() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Base64 as B64 } from "sloppy/codec";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("aliased sloppy/codec import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic
        .message
        .contains("unsupported sloppy import \"Base64\""));
}

#[test]
fn type_only_sloppy_net_import_does_not_emit_runtime_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import type { TcpClient } from "sloppy/net";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("type-only sloppy/net import should be recognized");
    assert!(!app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const { Results } = __sloppyRuntime;"));
    assert!(!emitted_js.source.contains("TcpClient"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert!(value.get("requiredFeatures").is_none());
    assert!(value["features"].get("network").is_none());
    assert!(value["strongPlan"]["evidence"].get("network").is_none());
}

#[test]
fn type_only_sloppy_data_import_does_not_emit_runtime_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import type { Migrations, ProviderHealth, sql } from "sloppy/data";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("type-only sloppy/data import should be recognized");
    assert!(!app.uses_data_runtime);
    assert!(!app.uses_sql_runtime);
    assert!(!app.uses_migrations_runtime);
    assert!(!app.uses_provider_health_runtime);
    assert!(!app.uses_fs_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const { Results } = __sloppyRuntime;"));
    assert!(!emitted_js.source.contains("Migrations"));
    assert!(!emitted_js.source.contains("ProviderHealth"));
    assert!(!emitted_js.source.contains("sql"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert!(value.get("requiredFeatures").is_none());
    assert!(value["features"].get("filesystem").is_none());
    assert!(value["strongPlan"]["evidence"].get("filesystem").is_none());
}

#[test]
fn sloppy_orm_import_emits_runtime_bindings() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { orm, table, column, relation } from "sloppy/orm";
const Teams = table("teams", { id: column.uuid().primaryKey() });
const Users = table("users", { id: column.uuid().primaryKey(), teamId: column.uuid().references(() => Teams.id) });
relation(Users, ({ one }) => ({ team: one(Teams, { local: Users.teamId, foreign: Teams.id }) }));
const app = Sloppy.create();
app.mapGet("/", () => Results.json({ provider: Boolean(orm.from) }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("sloppy/orm runtime import should be recognized");
    assert!(app.uses_orm_runtime);
    assert!(app.uses_data_runtime);
    assert!(app.uses_sql_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
        "const { Results, data, sql, orm, table, column, relation, SloppyOrmError, SloppyOrmConcurrencyError } = __sloppyRuntime;"
    ));
    assert!(emitted_js.source.contains("const Teams = table(\"teams\""));
    assert!(emitted_js.source.contains("relation(Users"));

    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");
    assert_eq!(value["features"]["orm"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["orm"],
        serde_json::json!(true)
    );
    assert_eq!(value["orm"]["mode"], serde_json::json!("runtime-dynamic"));
    assert_eq!(
        value["orm"]["extraction"]["status"],
        serde_json::json!("static")
    );
    let tables = value["orm"]["tables"]
        .as_array()
        .expect("ORM tables should be an array");
    assert!(tables.iter().any(|table| {
        table["model"] == "Teams"
            && table["name"] == "teams"
            && table["columns"]
                .as_array()
                .expect("columns should be an array")
                .iter()
                .any(|column| column["name"] == "id" && column["primaryKey"] == true)
    }));
    assert!(tables.iter().any(|table| {
        table["model"] == "Users"
            && table["columns"]
                .as_array()
                .expect("columns should be an array")
                .iter()
                .any(|column| {
                    column["name"] == "teamId"
                        && column["reference"]["tableModel"] == "Teams"
                        && column["reference"]["column"] == "id"
                })
    }));
    let relations = value["orm"]["relations"]
        .as_array()
        .expect("ORM relations should be an array");
    assert!(relations.iter().any(|relation| {
        relation["tableModel"] == "Users"
            && relation["name"] == "team"
            && relation["kind"] == "one"
            && relation["targetModel"] == "Teams"
            && relation["local"]["tableModel"] == "Users"
            && relation["local"]["column"] == "teamId"
            && relation["foreign"]["tableModel"] == "Teams"
            && relation["foreign"]["column"] == "id"
    }));
    assert!(value["doctorChecks"]
        .as_array()
        .expect("doctor checks should be an array")
        .iter()
        .any(|check| check["id"] == "stdlib.orm.dynamic_metadata"));
}

#[test]
fn sloppy_orm_table_diagnostics_include_valid_shape_hint() {
    let cases = [
        (
            "missing column object",
            r#"import { Sloppy, Results } from "sloppy";
import { table } from "sloppy/orm";
const Users = table("users");
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#,
        ),
        (
            "empty column object",
            r#"import { Sloppy, Results } from "sloppy";
import { table } from "sloppy/orm";
const Users = table("users", {});
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#,
        ),
    ];
    for (name, source) in cases {
        let diagnostic = extract(std::path::Path::new("app.ts"), source)
            .expect_err(&format!("{name} should fail"));
        assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_ORM_TABLE");
        let hint = diagnostic.hint.as_deref().expect("ORM table hint");
        assert!(
            hint.contains("const Users = table(\"users\""),
            "{name} should include table shape hint"
        );
    }
}

#[test]
fn sloppy_orm_dynamic_table_shapes_compile_with_partial_metadata() {
    let cases = [
        (
            "computed table name",
            r#"import { Sloppy, Results } from "sloppy";
import { table, column } from "sloppy/orm";
const tableName = "users";
const Users = table(tableName, { id: column.uuid().primaryKey() });
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#,
        ),
        (
            "reused column object",
            r#"import { Sloppy, Results } from "sloppy";
import { table, column } from "sloppy/orm";
const Id = column.uuid().primaryKey();
const Users = table("users", { id: Id });
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#,
        ),
        (
            "factory column object",
            r#"import { Sloppy, Results } from "sloppy";
import { table, column } from "sloppy/orm";
function makeColumns() {
  return { id: column.uuid().primaryKey() };
}
const Users = table("users", makeColumns());
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#,
        ),
        (
            "column object variable",
            r#"import { Sloppy, Results } from "sloppy";
import { table, column } from "sloppy/orm";
const columns = { id: column.uuid().primaryKey() };
const Users = table("users", columns);
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#,
        ),
    ];
    for (name, source) in cases {
        let app = extract(std::path::Path::new("app.ts"), source)
            .unwrap_or_else(|_| panic!("{name} should compile"));
        let emitted_js = super::emit_app_js(&app);
        assert!(
            emitted_js.source.contains("const Users = table("),
            "{name} should keep the runtime table declaration"
        );
        let emitted_source_map = super::emit_source_map(&app, &emitted_js);
        let plan = super::emit_plan(
            &app,
            &super::sha256_hex(&emitted_js.source),
            &super::sha256_hex(&emitted_source_map),
        )
        .expect("plan should emit");
        let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");
        assert_eq!(
            value["orm"]["extraction"]["status"],
            serde_json::json!("partial"),
            "{name} should mark ORM metadata partial"
        );
    }
}

#[test]
fn sloppy_orm_relation_diagnostics_include_valid_shape_hint() {
    let cases = [
        (
            "missing callback",
            r#"import { Sloppy, Results } from "sloppy";
import { table, column, relation } from "sloppy/orm";
const Users = table("users", { id: column.uuid().primaryKey() });
relation(Users);
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#,
        ),
        (
            "dynamic relation table",
            r#"import { Sloppy, Results } from "sloppy";
import { table, column, relation } from "sloppy/orm";
const Users = table("users", { id: column.uuid().primaryKey() });
relation(Users.name, ({ one }) => ({}));
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#,
        ),
        (
            "invalid relation table expression",
            r#"import { Sloppy, Results } from "sloppy";
import { table, column, relation } from "sloppy/orm";
const Users = table("users", { id: column.uuid().primaryKey() });
function pickTable() { return Users; }
relation(pickTable(), ({ one }) => ({}));
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#,
        ),
    ];
    for (name, source) in cases {
        let diagnostic = extract(std::path::Path::new("app.ts"), source)
            .expect_err(&format!("{name} should fail"));
        assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_ORM_RELATION");
        let hint = diagnostic.hint.as_deref().expect("ORM relation hint");
        assert!(
            hint.contains("relation(Users, ({ one, many })"),
            "{name} should include relation shape hint"
        );
    }
}

#[test]
fn sloppy_orm_dynamic_relation_callback_compiles_with_partial_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { table, column, relation } from "sloppy/orm";
const Users = table("users", { id: column.uuid().primaryKey() });
const configure = ({ one }) => ({});
relation(Users, configure);
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("runtime-valid relation callback variable should compile");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("relation(Users, configure);"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");
    assert_eq!(
        value["orm"]["extraction"]["status"],
        serde_json::json!("partial")
    );
}

#[test]
fn sloppy_orm_plan_ignores_table_and_relation_text_outside_ast_calls() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { table, column, relation } from "sloppy/orm";
// const Ghost = table("ghosts", { id: column.uuid().primaryKey() });
const template = `relation(Ghost, ({ one }) => ({ team: one(Teams, { local: Ghost.teamId, foreign: Teams.id }) }))`;
const quoted = "table(\"quoted\", { id: column.uuid().primaryKey() })";
const Users = table("users", { id: column.uuid().primaryKey() });
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("AST extraction should ignore inert text");
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");
    let tables = value["orm"]["tables"].as_array().expect("table array");
    assert_eq!(tables.len(), 1);
    assert_eq!(tables[0]["name"], "users");
    assert!(value["orm"]["relations"]
        .as_array()
        .expect("relations")
        .is_empty());
}

#[test]
fn root_sloppy_sql_import_emits_data_runtime() {
    let source = r#"import { Sloppy, Results, sql } from "sloppy";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("root sql import should be recognized");
    assert!(app.uses_data_runtime);
    assert!(app.uses_sql_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("const { Results, data, sql }"));
}

#[test]
fn root_sloppy_import_rejects_net_only_exports() {
    let source = r#"import { Sloppy, Results, TcpClient } from "sloppy";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("root sloppy import should reject net-only exports");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic
        .message
        .contains("unsupported sloppy import \"TcpClient\""));
}

#[test]
fn type_only_sloppy_os_import_does_not_emit_runtime_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import type { Process } from "sloppy/os";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("type-only sloppy/os import should be recognized");
    assert!(!app.uses_os_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const { Results } = __sloppyRuntime;"));
    assert!(!emitted_js.source.contains("Process"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert!(value.get("requiredFeatures").is_none());
    assert!(value["features"].get("os").is_none());
    assert!(value["strongPlan"]["evidence"].get("os").is_none());
}

#[test]
fn sloppy_net_import_alias_is_rejected() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { TcpClient as Client } from "sloppy/net";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("aliased sloppy/net import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic
        .message
        .contains("unsupported sloppy import \"TcpClient\""));
}

#[test]
fn sloppy_os_import_alias_is_rejected() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Process as ChildProcess } from "sloppy/os";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("aliased sloppy/os import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic
        .message
        .contains("unsupported sloppy import \"Process\""));
}

#[test]
fn side_effect_sloppy_fs_import_is_rejected() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import "sloppy/fs";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("side-effect sloppy/fs import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER");
    assert!(diagnostic
        .message
        .contains("unsupported import specifier \"sloppy/fs\""));
}

#[test]
fn side_effect_sloppy_time_import_is_rejected() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import "sloppy/time";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("side-effect sloppy/time import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER");
    assert!(diagnostic
        .message
        .contains("unsupported import specifier \"sloppy/time\""));
}

#[test]
fn empty_named_sloppy_fs_import_is_rejected() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import {} from "sloppy/fs";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("empty named sloppy/fs import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER");
    assert!(diagnostic
        .message
        .contains("unsupported import specifier \"sloppy/fs\""));
}

#[test]
fn database_capability_rejects_mismatched_path_alias() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.main", {
  provider: "sqlite",
  access: "readwrite",
  database: ":memory:",
  path: "app.db",
});
const app = builder.build();
app.mapGet("/ok", () => Results.ok({ ok: true }));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("mismatched database/path alias should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE");
    assert_eq!(
        diagnostic.message,
        "database capability cannot declare different database and path values"
    );
}

#[test]
fn extracts_function_module_routes_and_provider_metadata() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    let input = root.join("tests/fixtures/function-module/input.js");
    let source = fs::read_to_string(&input).expect("fixture input should exist");
    let app = extract(&input, &source).expect("function module fixture should extract");

    assert_eq!(app.routes.len(), 2);
    assert_eq!(app.routes[0].pattern, "/health");
    assert_eq!(app.routes[1].pattern, "/users");
    assert_eq!(app.routes[1].module.as_deref(), Some("usersModule"));
    assert_eq!(app.capabilities.len(), 1);
    assert_eq!(app.capabilities[0].token, "data.main");

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("const { Results, data }"));
    assert!(emitted_js
        .source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));

    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    assert!(emitted_source_map.contains("\"users.js\""));
    assert!(emitted_source_map.contains("\"input.js\""));

    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    assert!(plan.contains("\"module\": \"usersModule\""));
    assert!(plan.contains("\"path\": \"users.js\""));
}

#[test]
fn extracts_multiple_function_modules_from_same_file() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    let input = root.join("tests/fixtures/function-module-same-file/input.js");
    let source = fs::read_to_string(&input).expect("fixture input should exist");
    let app = extract(&input, &source).expect("same-file function modules should extract");

    assert_eq!(app.routes.len(), 2);
    assert_eq!(app.routes[0].pattern, "/health");
    assert_eq!(app.routes[0].module.as_deref(), Some("healthModule"));
    assert_eq!(app.routes[1].pattern, "/users");
    assert_eq!(app.routes[1].module.as_deref(), Some("usersModule"));
}

include!("sloppyc_tests/framework_runtime.rs");
