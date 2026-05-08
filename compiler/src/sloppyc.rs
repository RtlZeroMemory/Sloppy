use std::{
    collections::{BTreeMap, BTreeSet},
    ffi::OsString,
    fs,
    path::{Path, PathBuf},
};

use oxc_allocator::Allocator;
use oxc_ast::ast::{
    Argument, ArrayExpressionElement, BindingPattern, CallExpression, ChainElement, Declaration,
    Expression, ExpressionStatement, ForStatementInit, ImportDeclaration,
    ImportDeclarationSpecifier, ImportOrExportKind, ObjectPropertyKind, PropertyKey, PropertyKind,
    Statement, TSLiteral, TSSignature, TSType, TSTypeName,
};
use oxc_parser::Parser;
use oxc_span::Span;
use serde_json::json;
use serde_json::Value;
use sha2::{Digest, Sha256};

use crate::diagnostic::Diagnostic;
use crate::parser::{source_type_for_path, ParseContext};
use crate::resolver;
use crate::source::{line_column, source_map_source_name};
use crate::validation::{
    plan_completeness, route_completeness, Completeness, RouteCompletenessInput,
};

const COMPILER_VERSION: &str = "sloppyc-0.8.0";
const RUNTIME_MINIMUM_VERSION: &str = "0.1.0";
const STDLIB_VERSION: &str = "0.1.0";

// CODEC_EXPORTS is the public codec contract shared by import validation and runtime
// export emission.
const CODEC_EXPORTS: &[&str] = &[
    "Base64",
    "Base64Url",
    "Hex",
    "Text",
    "Binary",
    "Compression",
    "Checksums",
];

// WORKER_EXPORTS is the public workers contract shared by import validation and runtime
// export emission.
const WORKER_EXPORTS: &[&str] = &[
    "BackgroundService",
    "WorkQueue",
    "WorkerPool",
    "Worker",
    "WorkerCancellationController",
    "WorkerCancellationSignal",
    "SloppyWorkerError",
];

#[derive(Debug, Eq, PartialEq)]
enum CliCommand {
    Help,
    Version,
    Build {
        input: PathBuf,
        out_dir: PathBuf,
        options: CompileOptions,
    },
    Invalid(String),
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct CompileOptions {
    pub environment: Option<String>,
    pub host: Option<String>,
    pub port: Option<u16>,
    pub config_overrides: Vec<(String, String)>,
}

impl CompileOptions {
    pub fn new() -> Self {
        Self {
            environment: None,
            host: None,
            port: None,
            config_overrides: Vec::new(),
        }
    }
}

impl Default for CompileOptions {
    fn default() -> Self {
        Self::new()
    }
}

pub enum CliExit {
    Success,
    Output(String),
    Failure { code: i32, diagnostic: String },
}

#[derive(Debug, Clone)]
pub struct PlanOutput {
    pub path: PathBuf,
    pub contents: String,
}

#[derive(Debug, Clone)]
pub struct BundleOutput {
    pub path: PathBuf,
    pub contents: String,
}

#[derive(Debug, Clone)]
pub struct SourceMapOutput {
    pub path: PathBuf,
    pub contents: String,
}

#[derive(Debug, Clone)]
pub struct CompileOutput {
    pub out_dir: PathBuf,
    pub plan: PlanOutput,
    pub bundle: BundleOutput,
    pub source_map: SourceMapOutput,
}

#[derive(Debug)]
pub struct CompileError {
    pub code: i32,
    pub diagnostic: Diagnostic,
    pub source: Option<String>,
}

#[derive(Debug, Clone)]
struct Route {
    method: &'static str,
    pattern: String,
    framework_path: Option<String>,
    name: Option<String>,
    span: Span,
    source_path: PathBuf,
    source_name: String,
    source: String,
    module: Option<String>,
    handler: Handler,
}

#[derive(Debug, Clone)]
struct Handler {
    source: String,
    span: Span,
    is_async: bool,
    runtime_deferred: bool,
    source_name: String,
    source_text: String,
    bindings: Vec<RequestBinding>,
    response: Option<ResponseMetadata>,
    responses: Vec<ResponseMetadata>,
    effects: Vec<EffectMetadata>,
}

#[derive(Debug, Clone)]
struct DatabaseCapability {
    token: String,
    capability_kind: String,
    provider: String,
    config_name: Option<String>,
    access: String,
    database: Option<String>,
    config_source: Option<String>,
    source_name: String,
    source: String,
    span: Span,
    from_provider_use: bool,
}

#[derive(Debug, Clone)]
struct ServiceRegistration {
    token: String,
    lifetime: &'static str,
    factory_source: String,
}

#[derive(Debug, Clone)]
struct SourceMapMapping {
    generated_line: usize,
    generated_column: usize,
    source_index: usize,
    original_line: usize,
    original_column: usize,
}

#[derive(Debug)]
struct HandlerGeneratedStart {
    handler_id: usize,
    generated_line: usize,
    generated_column: usize,
}

#[derive(Debug)]
struct EmittedAppJs {
    source: String,
    mappings: Vec<SourceMapMapping>,
    handler_generated_starts: Vec<HandlerGeneratedStart>,
}

#[derive(Debug)]
struct ExtractedApp {
    uses_data_runtime: bool,
    uses_sql_runtime: bool,
    source_files: Vec<SourceFile>,
    routes: Vec<Route>,
    service_registrations: Vec<ServiceRegistration>,
    modules: Vec<FunctionModule>,
    helper_sources: Vec<String>,
    capabilities: Vec<DatabaseCapability>,
    configuration: Option<ConfigurationPlan>,
    schemas: Vec<SchemaMetadata>,
    config_reads: Vec<ConfigReadMetadata>,
    uses_time_runtime: bool,
    uses_fs_runtime: bool,
    uses_crypto_runtime: bool,
    noncrypto_hash_security_context_visible: bool,
    uses_codec_runtime: bool,
    checksum_security_context_visible: bool,
    uses_net_runtime: bool,
    uses_os_runtime: bool,
    uses_http_client_runtime: bool,
    uses_workers_runtime: bool,
}

#[derive(Debug, Clone)]
pub(crate) struct RequestBinding {
    pub(crate) kind: String,
    pub(crate) name: Option<String>,
    pub(crate) schema: Option<String>,
    pub(crate) parameter: Option<String>,
    pub(crate) type_name: Option<String>,
    pub(crate) source_name: Option<String>,
    pub(crate) source_text: Option<String>,
    pub(crate) span: Option<Span>,
    pub(crate) wrapper: Option<String>,
    pub(crate) injection_kind: Option<String>,
    pub(crate) provider_kind: Option<String>,
    pub(crate) capability: Option<String>,
    pub(crate) semantic: Option<String>,
    pub(crate) redacted: bool,
}

#[derive(Debug, Clone)]
struct ResponseMetadata {
    helper: String,
    status: u16,
    kind: String,
    body_schema: Option<String>,
    source_name: Option<String>,
    source_text: Option<String>,
    span: Option<Span>,
    partial: bool,
}

#[derive(Debug, Clone)]
struct EffectMetadata {
    provider: String,
    capability_kind: String,
    provider_kind: String,
    access: &'static str,
    operation: String,
    reason: String,
    source_name: String,
    source_text: String,
    span: Span,
}

#[derive(Debug, Clone, Default)]
struct FunctionEffectSummary {
    effects: Vec<EffectMetadata>,
    provider_bindings: BTreeMap<String, ProviderBinding>,
    helper_calls: BTreeSet<String>,
    unknown_provider_usage: bool,
    source_name: String,
    source_text: String,
}

struct HandlerExtractionContext<'a> {
    route_pattern: &'a str,
    source: &'a str,
    source_name: &'a str,
    allow_data_handler_body: bool,
    schema_names: &'a BTreeSet<String>,
    provider_bindings: &'a BTreeMap<String, ProviderBinding>,
    helper_effects: &'a BTreeMap<String, FunctionEffectSummary>,
}

#[derive(Debug, Clone)]
struct ProviderBinding {
    token: String,
    capability_kind: String,
    provider: String,
}

#[derive(Debug, Clone)]
struct SchemaMetadata {
    name: String,
    definition: Value,
    source_name: String,
    source: String,
    span: Span,
}

#[derive(Debug, Clone)]
struct ConfigReadMetadata {
    key: String,
    value_type: String,
    has_default: bool,
    default_value: Option<Value>,
    required: bool,
    sensitive: bool,
    source_name: String,
    source: String,
    span: Span,
}

fn schema_names(state: &AppState) -> BTreeSet<String> {
    state.schema_names.clone()
}

#[derive(Debug, Clone)]
struct ConfigurationPlan {
    environment: String,
    keys: Vec<ConfigurationPlanKey>,
    providers: Vec<ConfigurationProviderPlan>,
    requirements: Vec<ConfigurationRequirementPlan>,
    package_manifest: ConfigurationPackageManifest,
}

#[derive(Debug, Clone)]
struct ConfigurationPlanKey {
    key: String,
    source: String,
    value: Value,
    sensitive: bool,
}

#[derive(Debug, Clone)]
struct ConfigurationProviderPlan {
    provider: String,
    name: String,
    prefix: String,
    source: String,
}

#[derive(Debug, Clone)]
struct ConfigurationRequirementPlan {
    key: String,
    value_type: String,
    required: bool,
    sensitive: bool,
    status: String,
    source: Option<String>,
    required_by: String,
    default_value: Option<Value>,
}

#[derive(Debug, Clone, Default)]
struct ConfigurationPackageManifest {
    required: Vec<ConfigurationPackageEntry>,
    optional: Vec<ConfigurationPackageEntry>,
}

#[derive(Debug, Clone)]
struct ConfigurationPackageEntry {
    key: String,
    env: String,
    value_type: String,
    sensitive: bool,
    default_value: Option<Value>,
}

#[derive(Debug, Clone)]
struct SourceFile {
    name: String,
    source: String,
}

#[derive(Debug, Clone)]
struct ImportedModule {
    local_name: String,
    export_name: String,
    path: PathBuf,
    span: Span,
}

#[derive(Debug, Clone, Eq, PartialEq)]
struct FunctionModule {
    name: String,
    source_name: String,
}

#[derive(Debug)]
struct AppState {
    sloppy_imported: bool,
    results_imported: bool,
    data_imported: bool,
    sql_imported: bool,
    schema_imported: bool,
    time_imported: bool,
    fs_imported: bool,
    crypto_imported: bool,
    noncrypto_hash_security_context_visible: bool,
    codec_imported: bool,
    checksum_security_context_visible: bool,
    net_imported: bool,
    os_imported: bool,
    http_client_imported: bool,
    workers_imported: bool,
    sqlite_imported: bool,
    unsupported_import_alias: bool,
    unsupported_import_name: Option<(String, Span)>,
    unsupported_import_specifier: Option<(String, Span)>,
    dynamic_import: Option<Span>,
    app_vars: BTreeSet<String>,
    builder_vars: BTreeSet<String>,
    group_vars: BTreeMap<String, String>,
    provider_bindings: BTreeMap<String, ProviderBinding>,
    helper_sources: BTreeMap<String, String>,
    helper_effects: BTreeMap<String, FunctionEffectSummary>,
    app_provider_uses: BTreeSet<String>,
    imported_modules: Vec<ImportedModule>,
    used_modules: Vec<(String, Span)>,
    modules: BTreeMap<(String, String), FunctionModule>,
    routes: Vec<Route>,
    service_registrations: Vec<ServiceRegistration>,
    capabilities: Vec<DatabaseCapability>,
    schemas: Vec<SchemaMetadata>,
    schema_names: BTreeSet<String>,
    config_reads: Vec<ConfigReadMetadata>,
    default_export: Option<String>,
}

impl AppState {
    fn new() -> Self {
        Self {
            sloppy_imported: false,
            results_imported: false,
            data_imported: false,
            sql_imported: false,
            schema_imported: false,
            time_imported: false,
            fs_imported: false,
            crypto_imported: false,
            noncrypto_hash_security_context_visible: false,
            codec_imported: false,
            checksum_security_context_visible: false,
            net_imported: false,
            os_imported: false,
            http_client_imported: false,
            workers_imported: false,
            sqlite_imported: false,
            unsupported_import_alias: false,
            unsupported_import_name: None,
            unsupported_import_specifier: None,
            dynamic_import: None,
            app_vars: BTreeSet::new(),
            builder_vars: BTreeSet::new(),
            group_vars: BTreeMap::new(),
            provider_bindings: BTreeMap::new(),
            helper_sources: BTreeMap::new(),
            helper_effects: BTreeMap::new(),
            app_provider_uses: BTreeSet::new(),
            imported_modules: Vec::new(),
            used_modules: Vec::new(),
            modules: BTreeMap::new(),
            routes: Vec::new(),
            service_registrations: Vec::new(),
            capabilities: Vec::new(),
            schemas: Vec::new(),
            schema_names: BTreeSet::new(),
            config_reads: Vec::new(),
            default_export: None,
        }
    }
}

pub fn run(args: impl IntoIterator<Item = OsString>) -> CliExit {
    match command_from_args(args) {
        CliCommand::Version => CliExit::Output(version_text()),
        CliCommand::Help => CliExit::Output(help_text()),
        CliCommand::Invalid(message) => CliExit::Failure {
            code: 2,
            diagnostic: format!("{}sloppyc: {message}", help_text()),
        },
        CliCommand::Build {
            input,
            out_dir,
            options,
        } => match compile_file(&input, &out_dir, &options) {
            Ok(_) => CliExit::Success,
            Err(failure) => CliExit::Failure {
                code: failure.code,
                diagnostic: failure.diagnostic.render(failure.source.as_deref()),
            },
        },
    }
}

fn command_from_args(args: impl IntoIterator<Item = OsString>) -> CliCommand {
    let mut values = args.into_iter().collect::<Vec<_>>();
    if values.is_empty() {
        return CliCommand::Help;
    }

    let first = values.remove(0);
    let Some(first) = first.to_str() else {
        return CliCommand::Invalid("arguments must be valid UTF-8".to_string());
    };

    match first {
        "--version" => {
            if values.is_empty() {
                CliCommand::Version
            } else {
                CliCommand::Invalid("--version does not accept extra arguments".to_string())
            }
        }
        "--help" | "-h" => CliCommand::Help,
        "build" => parse_build_args(values),
        other => CliCommand::Invalid(format!("unsupported command '{other}'")),
    }
}

fn parse_build_args(values: Vec<OsString>) -> CliCommand {
    let mut input = None;
    let mut out_dir = None;
    let mut options = CompileOptions::new();
    let mut index = 0;

    while index < values.len() {
        let Some(arg) = values[index].to_str() else {
            return CliCommand::Invalid("build arguments must be valid UTF-8".to_string());
        };

        if arg == "--out" {
            index += 1;
            if index >= values.len() {
                return CliCommand::Invalid("build requires a directory after --out".to_string());
            }
            out_dir = Some(PathBuf::from(&values[index]));
        } else if arg == "--environment" {
            index += 1;
            if index >= values.len() {
                return CliCommand::Invalid(
                    "build requires an environment name after --environment".to_string(),
                );
            }
            let Some(environment) = values[index].to_str() else {
                return CliCommand::Invalid("build environment must be valid UTF-8".to_string());
            };
            if environment.trim().is_empty() {
                return CliCommand::Invalid("build environment must not be empty".to_string());
            }
            options.environment = Some(environment.to_string());
        } else if arg == "--host" {
            index += 1;
            if index >= values.len() {
                return CliCommand::Invalid("build requires a host after --host".to_string());
            }
            let Some(host) = values[index].to_str() else {
                return CliCommand::Invalid("build host must be valid UTF-8".to_string());
            };
            if host.trim().is_empty() {
                return CliCommand::Invalid("build host must not be empty".to_string());
            }
            options.host = Some(host.to_string());
        } else if arg == "--port" {
            index += 1;
            if index >= values.len() {
                return CliCommand::Invalid("build requires a port after --port".to_string());
            }
            let Some(port_text) = values[index].to_str() else {
                return CliCommand::Invalid("build port must be valid UTF-8".to_string());
            };
            let Ok(port) = port_text.parse::<u16>() else {
                return CliCommand::Invalid(format!(
                    "build --port expects an integer from 0 to 65535, got '{port_text}'"
                ));
            };
            options.port = Some(port);
        } else if arg == "--config" {
            index += 1;
            if index >= values.len() {
                return CliCommand::Invalid("build requires KEY=VALUE after --config".to_string());
            }
            let Some(override_text) = values[index].to_str() else {
                return CliCommand::Invalid("build --config value must be valid UTF-8".to_string());
            };
            let Some((key, value)) = override_text.split_once('=') else {
                return CliCommand::Invalid("build --config expects KEY=VALUE".to_string());
            };
            if key.trim().is_empty() {
                return CliCommand::Invalid("build --config key must not be empty".to_string());
            }
            options
                .config_overrides
                .push((key.to_string(), value.to_string()));
        } else if arg.starts_with('-') {
            return CliCommand::Invalid(format!("unsupported build option '{arg}'"));
        } else if input.is_none() {
            input = Some(PathBuf::from(&values[index]));
        } else {
            return CliCommand::Invalid("build accepts exactly one input file".to_string());
        }
        index += 1;
    }

    match (input, out_dir) {
        (Some(input), Some(out_dir)) => CliCommand::Build {
            input,
            out_dir,
            options,
        },
        (None, _) => CliCommand::Invalid("build requires an input file".to_string()),
        (_, None) => CliCommand::Invalid("build requires --out <directory>".to_string()),
    }
}

fn version_text() -> String {
    format!("{}\n", COMPILER_VERSION.replace("sloppyc-", "sloppyc "))
}

fn help_text() -> String {
    let mut text = version_text();
    text.push_str(
        "Supported app compiler: parses the documented Sloppy app shape and emits deterministic artifacts.\n",
    );
    text.push('\n');
    text.push_str("Usage:\n");
    text.push_str("  sloppyc --help\n");
    text.push_str("  sloppyc --version\n");
    text.push_str(
        "  sloppyc build <input.js> --out <directory> [--environment <name>] [--host <host>] [--port <port>] [--config <key=value>]\n",
    );
    text
}

pub fn compile_project(
    input: &Path,
    out_dir: &Path,
    options: &CompileOptions,
) -> Result<CompileOutput, Box<CompileError>> {
    compile_file(input, out_dir, options)
}

pub fn compile_file(
    input: &Path,
    out_dir: &Path,
    options: &CompileOptions,
) -> Result<CompileOutput, Box<CompileError>> {
    build(input, out_dir, options)?;
    read_compile_output(out_dir)
}

fn build(input: &Path, out_dir: &Path, options: &CompileOptions) -> Result<(), Box<CompileError>> {
    let source = fs::read_to_string(input).map_err(|error| {
        Box::new(CompileError {
            code: 1,
            diagnostic: Diagnostic::new(
                "SLOPPYC_E_INPUT",
                format!("failed to read compiler input: {error}"),
            )
            .with_path(input),
            source: None,
        })
    })?;

    let mut extracted = extract(input, &source).map_err(|diagnostic| {
        let diagnostic_source = diagnostic_render_source(input, &source, &diagnostic);
        Box::new(CompileError {
            code: 1,
            diagnostic,
            source: diagnostic_source,
        })
    })?;
    let configuration = ConfigurationModel::load(input, options, &extracted.config_reads).map_err(
        |diagnostic| {
            let diagnostic_source = diagnostic_render_source(input, &source, &diagnostic);
            Box::new(CompileError {
                code: 1,
                diagnostic,
                source: diagnostic_source,
            })
        },
    )?;
    configuration
        .apply_to_app(&mut extracted)
        .map_err(|diagnostic| {
            let diagnostic_source = diagnostic_render_source(input, &source, &diagnostic);
            Box::new(CompileError {
                code: 1,
                diagnostic,
                source: diagnostic_source,
            })
        })?;
    write_artifacts(out_dir, &extracted).map_err(|diagnostic| {
        let diagnostic_source = diagnostic_render_source(input, &source, &diagnostic);
        Box::new(CompileError {
            code: 1,
            diagnostic,
            source: diagnostic_source,
        })
    })?;
    Ok(())
}

fn diagnostic_render_source(
    input: &Path,
    entry_source: &str,
    diagnostic: &Diagnostic,
) -> Option<String> {
    let Some(path) = diagnostic.path.as_deref() else {
        return Some(entry_source.to_string());
    };
    if path == input {
        return Some(entry_source.to_string());
    }
    fs::read_to_string(path)
        .ok()
        .or_else(|| Some(entry_source.to_string()))
}

fn read_compile_output(out_dir: &Path) -> Result<CompileOutput, Box<CompileError>> {
    let bundle_path = out_dir.join("app.js");
    let source_map_path = out_dir.join("app.js.map");
    let plan_path = out_dir.join("app.plan.json");
    let bundle = read_artifact(&bundle_path, "app.js")?;
    let source_map = read_artifact(&source_map_path, "app.js.map")?;
    let plan = read_artifact(&plan_path, "app.plan.json")?;

    Ok(CompileOutput {
        out_dir: out_dir.to_path_buf(),
        plan: PlanOutput {
            path: plan_path,
            contents: plan,
        },
        bundle: BundleOutput {
            path: bundle_path,
            contents: bundle,
        },
        source_map: SourceMapOutput {
            path: source_map_path,
            contents: source_map,
        },
    })
}

fn read_artifact(path: &Path, name: &str) -> Result<String, Box<CompileError>> {
    fs::read_to_string(path).map_err(|error| {
        Box::new(CompileError {
            code: 1,
            diagnostic: Diagnostic::new(
                "SLOPPYC_E_OUTPUT",
                format!("failed to read emitted {name}: {error}"),
            )
            .with_path(path),
            source: None,
        })
    })
}

#[derive(Debug, Clone)]
struct ConfigEntry {
    key: String,
    value: Value,
    source: String,
}

#[derive(Debug)]
struct ConfigurationModel {
    environment: String,
    values: BTreeMap<String, ConfigEntry>,
}

impl ConfigurationModel {
    fn load(
        input: &Path,
        options: &CompileOptions,
        config_reads: &[ConfigReadMetadata],
    ) -> Result<Self, Diagnostic> {
        let environment = options
            .environment
            .clone()
            .unwrap_or_else(|| "Development".to_string());
        let config_dir = input.parent().unwrap_or_else(|| Path::new(""));
        let mut model = Self {
            environment,
            values: BTreeMap::new(),
        };

        model.add_defaults();
        model.load_optional_json(&config_dir.join("appsettings.json"), "appsettings.json")?;
        let env_file_name = format!("appsettings.{}.json", model.environment);
        model.load_optional_json(&config_dir.join(&env_file_name), &env_file_name)?;
        model.load_optional_json(
            &config_dir.join("appsettings.local.json"),
            "appsettings.local.json",
        )?;
        let env_local_file_name = format!("appsettings.{}.local.json", model.environment);
        model.load_optional_json(&config_dir.join(&env_local_file_name), &env_local_file_name)?;
        model.load_optional_json(
            &config_dir.join(".sloppy").join("secrets.json"),
            "user-secrets:.sloppy/secrets.json",
        )?;
        let env_secret_file_name =
            format!("user-secrets:.sloppy/secrets.{}.json", model.environment);
        model.load_optional_json(
            &config_dir
                .join(".sloppy")
                .join(format!("secrets.{}.json", model.environment)),
            &env_secret_file_name,
        )?;
        model.apply_environment_variables(config_reads)?;
        model.apply_cli_overrides(options);
        Ok(model)
    }

    fn add_defaults(&mut self) {
        for (key, value) in [
            ("Sloppy:Server:Host", json!("127.0.0.1")),
            ("Sloppy:Server:Port", json!(5173)),
            ("Sloppy:Server:MaxConnections", json!(4)),
            ("Sloppy:Server:MaxRequestBodyBytes", json!(8192)),
            ("Sloppy:Server:KeepAliveEnabled", json!(true)),
            ("Sloppy:Server:KeepAliveIdleTimeoutMs", json!(5000)),
            ("Sloppy:Server:MaxRequestsPerConnection", json!(100)),
            ("Sloppy:Server:RequestTimeoutMs", json!(30000)),
            ("Sloppy:Runtime:V8MicrotaskDrainLimit", json!(64)),
        ] {
            self.set(key, value, "built-in defaults");
        }
    }

    fn load_optional_json(&mut self, path: &Path, source: &str) -> Result<(), Diagnostic> {
        let contents = match fs::read_to_string(path) {
            Ok(contents) => contents,
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(()),
            Err(error) => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_CONFIG_READ",
                    format!("failed to read {source}: {error}"),
                )
                .with_path(path));
            }
        };
        let value = serde_json::from_str::<Value>(&contents).map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_CONFIG_MALFORMED",
                format!("malformed {source}: {error}"),
            )
            .with_path(path)
        })?;
        let Value::Object(object) = value else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_CONFIG_MALFORMED",
                format!("{source} must contain a JSON object"),
            )
            .with_path(path));
        };
        self.flatten_json_object(Vec::new(), &object, source)
            .map_err(|diagnostic| diagnostic.with_path(path))?;
        Ok(())
    }

    fn flatten_json_object(
        &mut self,
        prefix: Vec<String>,
        object: &serde_json::Map<String, Value>,
        source: &str,
    ) -> Result<(), Diagnostic> {
        for (key, value) in object {
            if key.is_empty() {
                let path = if prefix.is_empty() {
                    "<root>".to_string()
                } else {
                    prefix.join(":")
                };
                return Err(Diagnostic::new(
                    "SLOPPYC_E_CONFIG_KEY",
                    format!("{source} contains an empty config key segment under {path}"),
                )
                .with_hint("Config keys must not contain empty path segments."));
            }
            let mut next = prefix.clone();
            next.push(key.clone());
            if let Value::Object(child) = value {
                self.flatten_json_object(next, child, source)?;
            } else {
                let key = next.join(":");
                if let Some((resolved, resolved_source)) =
                    resolve_json_config_value(&key, value, source)?
                {
                    self.set(&key, resolved, &resolved_source);
                }
            }
        }
        Ok(())
    }

    fn apply_environment_variables(
        &mut self,
        config_reads: &[ConfigReadMetadata],
    ) -> Result<(), Diagnostic> {
        let mut known_roots = self.known_roots();
        for read in config_reads {
            if let Some(root) = read.key.split(':').next() {
                known_roots.insert(normalize_config_key(root));
            }
        }
        for (name, value) in std::env::vars() {
            let Some(logical) = env_logical_name(&name, &known_roots) else {
                continue;
            };
            if logical.is_empty()
                || logical.contains("___")
                || logical.starts_with('_')
                || logical.split("__").any(|segment| segment.is_empty())
            {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_CONFIG_ENV",
                    format!("invalid Sloppy environment variable name '{name}'"),
                )
                .with_hint(
                    "Use Sloppy__Server__Port or SLOPPY_SLOPPY__SERVER__PORT style names.",
                ));
            }
            let key = logical
                .split("__")
                .map(canonical_config_segment)
                .collect::<Vec<_>>()
                .join(":");
            let parsed = self.parse_env_value(&key, &value)?;
            self.set(&key, parsed, &format!("env:{name}"));
        }
        Ok(())
    }

    fn parse_env_value(&self, key: &str, value: &str) -> Result<Value, Diagnostic> {
        match self.get(key).map(|entry| &entry.value) {
            Some(Value::Number(existing)) if existing.is_i64() || existing.is_u64() => {
                let parsed = value.parse::<i64>().map_err(|_| {
                    Diagnostic::new(
                        "SLOPPYC_E_CONFIG_ENV",
                        format!(
                            "environment override for {key} expects an integer, got {}",
                            redact_config_value(key, value)
                        ),
                    )
                })?;
                Ok(json!(parsed))
            }
            Some(Value::Number(_)) => {
                let parsed = value.parse::<f64>().map_err(|_| {
                    Diagnostic::new(
                        "SLOPPYC_E_CONFIG_ENV",
                        format!(
                            "environment override for {key} expects a number, got {}",
                            redact_config_value(key, value)
                        ),
                    )
                })?;
                Ok(json!(parsed))
            }
            Some(Value::Bool(_)) => match value.to_ascii_lowercase().as_str() {
                "true" => Ok(json!(true)),
                "false" => Ok(json!(false)),
                _ => Err(Diagnostic::new(
                    "SLOPPYC_E_CONFIG_ENV",
                    format!(
                        "environment override for {key} expects true or false, got {}",
                        redact_config_value(key, value)
                    ),
                )),
            },
            _ => Ok(json!(value)),
        }
    }

    fn apply_cli_overrides(&mut self, options: &CompileOptions) {
        if let Some(host) = &options.host {
            self.set("Sloppy:Server:Host", json!(host), "CLI --host");
        }
        if let Some(port) = options.port {
            self.set("Sloppy:Server:Port", json!(port), "CLI --port");
        }
        for (key, value) in &options.config_overrides {
            if normalize_config_key(key).split(':').any(str::is_empty) {
                continue;
            }
            let parsed = self
                .parse_env_value(key, value)
                .unwrap_or_else(|_| json!(value));
            self.set(key, parsed, "CLI --config");
        }
    }

    fn apply_to_app(&self, app: &mut ExtractedApp) -> Result<(), Diagnostic> {
        let mut provider_plans = Vec::new();
        let mut requirements = Vec::new();
        for capability in &mut app.capabilities {
            let provider_name = provider_config_name(capability);
            let prefix = format!("Sloppy:Providers:{}:{provider_name}", capability.provider);
            if capability.provider == "sqlite" && capability.database.is_none() {
                let database_key = format!("{prefix}:database");
                if let Some(database) = self.get_string(&database_key)? {
                    let source = self
                        .get(&database_key)
                        .map(|entry| entry.source.clone())
                        .unwrap_or_else(|| "configuration".to_string());
                    capability.database = Some(database);
                    capability.config_source = Some(source);
                } else if capability.from_provider_use {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_CONFIG_MISSING_PROVIDER",
                        format!(
                            "{} provider '{provider_name}' is missing required config value {database_key}",
                            capability.provider
                        ),
                    )
                    .with_hint(
                        format!(
                            "Add Sloppy:Providers:{}:<name>:database to appsettings.json or pass inline provider options.",
                            capability.provider
                        ),
                    ));
                }
            }
            if let Some(source) = capability.config_source.clone() {
                provider_plans.push(ConfigurationProviderPlan {
                    provider: capability.provider.clone(),
                    name: provider_name,
                    prefix,
                    source,
                });
            }
            requirements.extend(self.provider_requirements(capability)?);
        }
        requirements.extend(self.config_read_requirements(&app.config_reads));
        let package_manifest = package_manifest_for_requirements(&requirements);

        app.configuration = Some(ConfigurationPlan {
            environment: self.environment.clone(),
            keys: self.plan_keys(),
            providers: provider_plans,
            requirements,
            package_manifest,
        });
        Ok(())
    }

    fn provider_requirements(
        &self,
        capability: &DatabaseCapability,
    ) -> Result<Vec<ConfigurationRequirementPlan>, Diagnostic> {
        let provider_name = provider_config_name(capability);
        let prefix = format!("Sloppy:Providers:{}:{provider_name}", capability.provider);
        let contract = match capability.provider.as_str() {
            "sqlite" => Some(("database", "string", false)),
            "postgres" | "sqlserver" => Some(("connectionString", "secret", true)),
            _ => None,
        };
        let Some((field, value_type, sensitive)) = contract else {
            return Ok(Vec::new());
        };
        let key = format!("{prefix}:{field}");
        let entry = self.get(&key);
        let status = if capability.database.is_some() || entry.is_some() {
            "present"
        } else {
            "missing"
        };
        Ok(vec![ConfigurationRequirementPlan {
            key,
            value_type: value_type.to_string(),
            required: true,
            sensitive,
            status: status.to_string(),
            source: capability
                .config_source
                .clone()
                .or_else(|| entry.map(|entry| entry.source.clone()))
                .or_else(|| {
                    capability
                        .database
                        .as_ref()
                        .map(|_| "inline provider options".to_string())
                }),
            required_by: source_location_label(
                &capability.source_name,
                &capability.source,
                capability.span,
            ),
            default_value: None,
        }])
    }

    fn config_read_requirements(
        &self,
        reads: &[ConfigReadMetadata],
    ) -> Vec<ConfigurationRequirementPlan> {
        reads
            .iter()
            .map(|read| {
                let entry = self.get(&read.key);
                let status = if entry.is_some() {
                    "present"
                } else if read.has_default {
                    "defaulted"
                } else {
                    "missing"
                };
                ConfigurationRequirementPlan {
                    key: canonical_config_key(&read.key),
                    value_type: read.value_type.clone(),
                    required: read.required,
                    sensitive: read.sensitive || config_key_is_sensitive(&read.key),
                    status: status.to_string(),
                    source: entry.map(|entry| entry.source.clone()),
                    required_by: source_location_label(&read.source_name, &read.source, read.span),
                    default_value: read.default_value.clone(),
                }
            })
            .collect()
    }

    fn plan_keys(&self) -> Vec<ConfigurationPlanKey> {
        self.values
            .values()
            .map(|entry| {
                let sensitive = config_key_is_sensitive(&entry.key);
                ConfigurationPlanKey {
                    key: entry.key.clone(),
                    source: entry.source.clone(),
                    value: if sensitive {
                        json!("<redacted>")
                    } else {
                        entry.value.clone()
                    },
                    sensitive,
                }
            })
            .collect()
    }

    fn get_string(&self, key: &str) -> Result<Option<String>, Diagnostic> {
        let Some(entry) = self.get(key) else {
            return Ok(None);
        };
        match &entry.value {
            Value::String(value) => Ok(Some(value.clone())),
            other => Err(Diagnostic::new(
                "SLOPPYC_E_CONFIG_TYPE",
                format!(
                    "config key {key} from {} expects a string, got {}",
                    entry.source,
                    json_type_name(other)
                ),
            )),
        }
    }

    fn get(&self, key: &str) -> Option<&ConfigEntry> {
        self.values.get(&normalize_config_key(key))
    }

    fn set(&mut self, key: &str, value: Value, source: &str) {
        self.values.insert(
            normalize_config_key(key),
            ConfigEntry {
                key: canonical_config_key(key),
                value,
                source: source.to_string(),
            },
        );
    }

    fn known_roots(&self) -> BTreeSet<String> {
        self.values
            .values()
            .filter_map(|entry| entry.key.split(':').next().map(normalize_config_key))
            .collect()
    }
}

fn resolve_json_config_value(
    key: &str,
    value: &Value,
    source: &str,
) -> Result<Option<(Value, String)>, Diagnostic> {
    let Value::String(text) = value else {
        return Ok(Some((value.clone(), source.to_string())));
    };
    let Some(name) = env_placeholder_name(text) else {
        return Ok(Some((value.clone(), source.to_string())));
    };
    if name.contains(':') || name.contains("__") || name.trim().is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_CONFIG_ENV",
            format!("{source} contains invalid environment placeholder for {key}"),
        )
        .with_hint("Use ${NAME} placeholders with a single environment variable name."));
    }
    match std::env::var(name) {
        Ok(resolved) => Ok(Some((json!(resolved), format!("{source}:${{{name}}}")))),
        Err(_) => Ok(None),
    }
}

fn env_placeholder_name(value: &str) -> Option<&str> {
    value
        .strip_prefix("${")
        .and_then(|inner| inner.strip_suffix('}'))
}

fn env_logical_name(name: &str, known_roots: &BTreeSet<String>) -> Option<String> {
    if let Some(stripped) = name.strip_prefix("SLOPPY_") {
        if stripped.contains("__") {
            return Some(stripped.to_string());
        }
    }
    if !name.contains("__") {
        return None;
    }
    let root = name.split("__").next().unwrap_or_default();
    if normalize_config_key(root) == "SLOPPY" || known_roots.contains(&normalize_config_key(root)) {
        return Some(name.to_string());
    }
    None
}

fn source_location_label(source_name: &str, source: &str, span: Span) -> String {
    let (line, _) = line_column(source, span.start);
    format!("{source_name}:{line}")
}

fn package_manifest_for_requirements(
    requirements: &[ConfigurationRequirementPlan],
) -> ConfigurationPackageManifest {
    let mut manifest = ConfigurationPackageManifest::default();
    let mut seen_required = BTreeSet::new();
    let mut seen_optional = BTreeSet::new();

    for requirement in requirements {
        let entry = ConfigurationPackageEntry {
            key: requirement.key.clone(),
            env: config_key_to_env_name(&requirement.key),
            value_type: requirement.value_type.clone(),
            sensitive: requirement.sensitive,
            default_value: requirement.default_value.clone(),
        };
        if requirement.required && !requirement.has_default() {
            if seen_required.insert(requirement.key.clone()) {
                manifest.required.push(entry);
            }
        } else if seen_optional.insert(requirement.key.clone()) {
            manifest.optional.push(entry);
        }
    }

    manifest
}

impl ConfigurationRequirementPlan {
    fn has_default(&self) -> bool {
        self.default_value.is_some()
    }
}

fn config_key_to_env_name(key: &str) -> String {
    key.split(':').collect::<Vec<_>>().join("__")
}

fn normalize_config_key(key: &str) -> String {
    key.split(':')
        .map(|segment| segment.to_ascii_uppercase())
        .collect::<Vec<_>>()
        .join(":")
}

fn canonical_config_key(key: &str) -> String {
    key.split(':')
        .map(canonical_config_segment)
        .collect::<Vec<_>>()
        .join(":")
}

fn canonical_config_segment(segment: &str) -> String {
    match segment.to_ascii_uppercase().as_str() {
        "SLOPPY" => "Sloppy".to_string(),
        "SERVER" => "Server".to_string(),
        "TLS" => "Tls".to_string(),
        "RUNTIME" => "Runtime".to_string(),
        "PROVIDERS" => "Providers".to_string(),
        "SQLITE" => "sqlite".to_string(),
        "POSTGRES" => "postgres".to_string(),
        "POSTGRESQL" => "postgres".to_string(),
        "SQLSERVER" => "sqlserver".to_string(),
        "HOST" => "Host".to_string(),
        "PORT" => "Port".to_string(),
        "MAXCONNECTIONS" => "MaxConnections".to_string(),
        "MAXREQUESTBODYBYTES" => "MaxRequestBodyBytes".to_string(),
        "KEEPALIVEENABLED" => "KeepAliveEnabled".to_string(),
        "KEEPALIVEIDLETIMEOUTMS" => "KeepAliveIdleTimeoutMs".to_string(),
        "MAXREQUESTSPERCONNECTION" => "MaxRequestsPerConnection".to_string(),
        "REQUESTTIMEOUTMS" => "RequestTimeoutMs".to_string(),
        "ENABLED" => "Enabled".to_string(),
        "CERTIFICATEPATH" => "CertificatePath".to_string(),
        "PRIVATEKEYPATH" => "PrivateKeyPath".to_string(),
        "PASSPHRASE" => "Passphrase".to_string(),
        "V8MICROTASKDRAINLIMIT" => "V8MicrotaskDrainLimit".to_string(),
        "DATABASE" => "database".to_string(),
        "QUEUECAPACITY" => "queueCapacity".to_string(),
        _ => segment.to_string(),
    }
}

fn provider_config_name(capability: &DatabaseCapability) -> String {
    capability
        .config_name
        .clone()
        .unwrap_or_else(|| provider_name_from_token(&capability.token))
}

fn provider_name_from_token(token: &str) -> String {
    token.strip_prefix("data.").unwrap_or(token).to_string()
}

fn json_type_name(value: &Value) -> &'static str {
    match value {
        Value::Null => "null",
        Value::Bool(_) => "bool",
        Value::Number(_) => "number",
        Value::String(_) => "string",
        Value::Array(_) => "array",
        Value::Object(_) => "object",
    }
}

fn config_key_is_sensitive(key: &str) -> bool {
    key.to_ascii_lowercase().split(':').any(|segment| {
        matches!(
            segment,
            "pwd"
                | "passwd"
                | "secret"
                | "password"
                | "token"
                | "apikey"
                | "api_key"
                | "passphrase"
                | "connectionstring"
                | "connection_string"
        ) || segment.ends_with("secret")
            || segment.ends_with("password")
            || segment.ends_with("token")
            || segment.ends_with("apikey")
            || segment.ends_with("passphrase")
            || segment.ends_with("connectionstring")
    })
}

fn redact_config_value(key: &str, value: &str) -> String {
    if config_key_is_sensitive(key) {
        "<redacted>".to_string()
    } else {
        format!("'{value}'")
    }
}

struct ModuleGraph {
    entry_dir: PathBuf,
    visiting: BTreeSet<PathBuf>,
    modules: BTreeMap<PathBuf, CachedModule>,
    source_files: Vec<SourceFile>,
    uses_time_runtime: bool,
    uses_crypto_runtime: bool,
    noncrypto_hash_security_context_visible: bool,
    uses_codec_runtime: bool,
    checksum_security_context_visible: bool,
    uses_net_runtime: bool,
    uses_os_runtime: bool,
    uses_http_client_runtime: bool,
    uses_workers_runtime: bool,
}

#[derive(Debug, Clone)]
struct CachedModule {
    exports: BTreeMap<String, Vec<Route>>,
    duplicate_exports: BTreeSet<String>,
}

impl ModuleGraph {
    fn new(entry_path: &Path) -> Self {
        let entry_dir = entry_path
            .parent()
            .unwrap_or_else(|| Path::new(""))
            .to_path_buf();
        Self {
            entry_dir: fs::canonicalize(&entry_dir).unwrap_or(entry_dir),
            visiting: BTreeSet::new(),
            modules: BTreeMap::new(),
            source_files: Vec::new(),
            uses_time_runtime: false,
            uses_crypto_runtime: false,
            noncrypto_hash_security_context_visible: false,
            uses_codec_runtime: false,
            checksum_security_context_visible: false,
            uses_net_runtime: false,
            uses_os_runtime: false,
            uses_http_client_runtime: false,
            uses_workers_runtime: false,
        }
    }

    fn record_source(&mut self, path: &Path, source: &str) -> String {
        let name = source_map_source_name(path);
        if !self.source_files.iter().any(|file| file.name == name) {
            self.source_files.push(SourceFile {
                name: name.clone(),
                source: source.to_string(),
            });
        }
        name
    }
}

fn extract(path: &Path, source: &str) -> Result<ExtractedApp, Diagnostic> {
    let mut graph = ModuleGraph::new(path);
    extract_entry(path, source, &mut graph)
}

fn extract_entry(
    path: &Path,
    source: &str,
    graph: &mut ModuleGraph,
) -> Result<ExtractedApp, Diagnostic> {
    let source_type = source_type_for_path(path, ParseContext::Entry)?;
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, source_type).parse();

    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(
            Diagnostic::new("SLOPPYC_E_PARSE", format!("failed to parse input: {error}"))
                .with_path(path),
        );
    }

    let source_name = graph.record_source(path, source);
    let mut state = AppState::new();
    state.noncrypto_hash_security_context_visible = noncrypto_hash_security_context_visible(source);
    state.checksum_security_context_visible = checksum_security_context_visible(source);
    state.schema_names = collect_schema_declaration_names(&parsed.program.body);
    for statement in &parsed.program.body {
        if state.dynamic_import.is_none() {
            state.dynamic_import = statement_dynamic_import_span(statement);
        }
        match statement {
            Statement::ImportDeclaration(import) => {
                extract_import(path, graph, &mut state, import)?
            }
            Statement::VariableDeclaration(declaration) => {
                extract_variable_declaration(path, source, &source_name, &mut state, declaration)?
            }
            Statement::FunctionDeclaration(function) => {
                extract_function_declaration(path, source, &source_name, &mut state, function)?
            }
            Statement::TSTypeAliasDeclaration(alias) => {
                if let Some(schema) = typescript_type_alias_schema(
                    path,
                    source,
                    &source_name,
                    alias,
                    &state.schema_names,
                )? {
                    state.schemas.push(schema);
                }
            }
            Statement::TSInterfaceDeclaration(interface) => {
                if let Some(schema) = typescript_interface_schema(
                    path,
                    source,
                    &source_name,
                    interface,
                    &state.schema_names,
                )? {
                    state.schemas.push(schema);
                }
            }
            Statement::ExpressionStatement(_) => {}
            Statement::ExportDefaultDeclaration(export) => {
                state.default_export = export_default_identifier(&export.declaration);
            }
            _ => return Err(top_level_statement_diagnostic(path, source, statement)),
        }
    }

    for statement in &parsed.program.body {
        if let Statement::ExpressionStatement(statement) = statement {
            extract_expression_statement(path, source, &source_name, &mut state, statement)?;
        }
    }

    if let Some(span) = state.dynamic_import {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_DYNAMIC_IMPORT",
            "dynamic import is not supported by the Sloppy module compiler",
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use static relative imports or documented Sloppy stdlib imports."));
    }

    if let Some((specifier, span)) = &state.unsupported_import_specifier {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
            format!("unsupported import specifier \"{specifier}\""),
        )
        .with_path(path)
        .with_span(*span)
                .with_hint("Use documented named, unaliased imports from \"sloppy\", \"sloppy/time\", \"sloppy/fs\", \"sloppy/crypto\", \"sloppy/codec\", \"sloppy/net\", \"sloppy/os\", or \"sloppy/workers\"; Sloppy does not implement Node or npm resolution."));
    }

    if let Some((specifier, span)) = &state.unsupported_import_name {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT",
            format!("unsupported sloppy import \"{specifier}\""),
        )
        .with_path(path)
        .with_span(*span)
        .with_hint("Use documented unaliased imports from \"sloppy\", \"sloppy/time\", \"sloppy/fs\", \"sloppy/crypto\", \"sloppy/codec\", \"sloppy/net\", \"sloppy/os\", or \"sloppy/workers\"."));
    }

    if !state.sloppy_imported || !state.results_imported {
        let hint = if state.unsupported_import_alias {
            "Import without aliases: import { Sloppy, Results } from \"sloppy\";"
        } else {
            "Use: import { Sloppy, Results } from \"sloppy\";"
        };
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT",
            if state.unsupported_import_alias {
                "input must import Sloppy and Results from \"sloppy\" without aliases"
            } else {
                "input must import Sloppy and Results from \"sloppy\""
            },
        )
        .with_path(path)
        .with_hint(hint));
    }

    for (local_name, span) in state.used_modules.clone() {
        let Some(imported) = state
            .imported_modules
            .iter()
            .find(|module| module.local_name == local_name)
            .cloned()
        else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                "app.useModule requires a function imported from a supported relative module",
            )
            .with_path(path)
            .with_span(span)
            .with_hint(
                "Import a named function module and pass it directly to app.useModule(...).",
            ));
        };
        let module_routes = extract_relative_module(graph, &imported)?;
        let module = FunctionModule {
            name: imported.export_name.clone(),
            source_name: source_map_source_name(&imported.path),
        };
        state
            .modules
            .entry((module.source_name.clone(), module.name.clone()))
            .or_insert(module);
        state.routes.extend(module_routes);
    }

    let Some(default_export) = state.default_export else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_MISSING_APP",
            "input must export one app as default",
        )
        .with_path(path)
        .with_hint("End the file with: export default app;"));
    };

    if !state.app_vars.contains(&default_export) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_MISSING_APP",
            "default export must reference the extracted Sloppy app",
        )
        .with_path(path)
        .with_hint("Export the variable created by Sloppy.create() or builder.build()."));
    }

    if state.app_vars.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_MULTIPLE_APPS",
            "supported app compiler supports exactly one app object",
        )
        .with_path(path));
    }

    if state.routes.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_MISSING_ROUTE",
            "app must register at least one route",
        )
        .with_path(path));
    }

    let mut route_keys = BTreeSet::new();
    let mut route_names = BTreeSet::new();
    for route in &state.routes {
        let key = format!("{} {}", route.method, route.pattern);
        if !route_keys.insert(key) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_DUPLICATE_ROUTE",
                "duplicate route method and pattern are not supported",
            )
            .with_path(&route.source_path)
            .with_span(route.span));
        }
        if let Some(name) = &route.name {
            if !route_names.insert(name.clone()) {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_DUPLICATE_ROUTE_NAME",
                    "duplicate route name",
                )
                .with_path(&route.source_path)
                .with_span(route.span));
            }
        }
    }

    coalesce_manual_capability_overrides(&mut state.capabilities);
    apply_inferred_capability_access(&mut state.capabilities, &state.routes);
    validate_provider_effect_registrations(path, &state.routes, &state.capabilities)?;

    let mut capability_tokens = BTreeSet::new();
    for capability in &state.capabilities {
        if !capability_tokens.insert(capability.token.clone()) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_DUPLICATE_CAPABILITY",
                "duplicate database capability token",
            )
            .with_path(path)
            .with_hint("Declare each database capability token once."));
        }
    }

    let helper_sources = state
        .helper_sources
        .iter()
        .filter(|(name, _)| helper_source_is_safe_for_top_level(state.helper_effects.get(*name)))
        .map(|(_, source)| source.clone())
        .collect();

    Ok(ExtractedApp {
        uses_data_runtime: state.data_imported
            || state.sql_imported
            || state.sqlite_imported
            || !state.app_provider_uses.is_empty()
            || state.routes.iter().any(|route| {
                !route.handler.effects.is_empty()
                    || route
                        .handler
                        .bindings
                        .iter()
                        .any(|binding| binding.injection_kind.as_deref() == Some("provider"))
            }),
        uses_sql_runtime: state.sql_imported,
        source_files: graph.source_files.clone(),
        routes: state.routes,
        service_registrations: state.service_registrations,
        modules: state.modules.into_values().collect(),
        helper_sources,
        capabilities: state.capabilities,
        configuration: None,
        schemas: state.schemas,
        config_reads: state.config_reads,
        uses_time_runtime: state.time_imported || graph.uses_time_runtime,
        uses_fs_runtime: state.fs_imported,
        uses_crypto_runtime: state.crypto_imported || graph.uses_crypto_runtime,
        noncrypto_hash_security_context_visible: state.noncrypto_hash_security_context_visible
            || graph.noncrypto_hash_security_context_visible,
        uses_codec_runtime: state.codec_imported || graph.uses_codec_runtime,
        checksum_security_context_visible: state.checksum_security_context_visible
            || graph.checksum_security_context_visible,
        uses_net_runtime: state.net_imported || graph.uses_net_runtime,
        uses_os_runtime: state.os_imported || graph.uses_os_runtime,
        uses_http_client_runtime: state.http_client_imported || graph.uses_http_client_runtime,
        uses_workers_runtime: state.workers_imported || graph.uses_workers_runtime,
    })
}

fn helper_source_is_safe_for_top_level(summary: Option<&FunctionEffectSummary>) -> bool {
    match summary {
        Some(summary) => {
            summary.effects.is_empty()
                && summary.provider_bindings.is_empty()
                && !summary.unknown_provider_usage
        }
        None => true,
    }
}

fn collect_schema_declaration_names(statements: &[Statement<'_>]) -> BTreeSet<String> {
    let mut names = BTreeSet::new();
    for statement in statements {
        match statement {
            Statement::VariableDeclaration(declaration) => {
                for declarator in &declaration.declarations {
                    let Some(init) = &declarator.init else {
                        continue;
                    };
                    if expression_mentions_schema(init) {
                        if let Some(name) = binding_identifier(&declarator.id) {
                            names.insert(name.to_string());
                        }
                    }
                }
            }
            Statement::TSTypeAliasDeclaration(alias) if alias.type_parameters.is_none() => {
                names.insert(alias.id.name.as_str().to_string());
            }
            Statement::TSInterfaceDeclaration(interface) if interface.type_parameters.is_none() => {
                names.insert(interface.id.name.as_str().to_string());
            }
            _ => {}
        }
    }
    names
}

fn sloppy_time_import_name_supported(name: &str) -> bool {
    matches!(
        name,
        "Time"
            | "Deadline"
            | "CancellationController"
            | "TimeoutError"
            | "CancelledError"
            | "InvalidDeadlineError"
            | "TimerDisposedError"
    )
}

fn sloppy_crypto_import_name_supported(name: &str) -> bool {
    matches!(
        name,
        "Random" | "Hash" | "Hmac" | "Password" | "ConstantTime" | "Secret" | "NonCryptoHash"
    )
}

fn noncrypto_hash_security_context_visible(source: &str) -> bool {
    source_contains_noncrypto_xxhash64_member(source) && source_has_security_identifier(source)
}

fn source_contains_noncrypto_xxhash64_member(source: &str) -> bool {
    source_contains_ascii_member(source, "NonCryptoHash", "xxHash64")
}

fn checksum_security_context_visible(source: &str) -> bool {
    source_contains_checksum_crc32_member(source) && source_has_security_identifier(source)
}

fn source_contains_checksum_crc32_member(source: &str) -> bool {
    source_contains_ascii_member(source, "Checksums", "crc32")
}

fn source_contains_ascii_member(source: &str, object_name: &str, property_name: &str) -> bool {
    let bytes = source.as_bytes();
    let mut index = 0;
    while index < bytes.len() {
        if let Some(next_index) = skip_js_literal_or_comment(bytes, index) {
            index = next_index;
            continue;
        }
        let Some((identifier, next_index)) = read_ascii_js_identifier(source, index) else {
            index += 1;
            continue;
        };
        if identifier == object_name {
            let dot_index = skip_ascii_whitespace(bytes, next_index);
            if bytes.get(dot_index) == Some(&b'.') {
                let property_index = skip_ascii_whitespace(bytes, dot_index + 1);
                if let Some((property, _)) = read_ascii_js_identifier(source, property_index) {
                    if property == property_name {
                        return true;
                    }
                }
            }
        }
        index = next_index;
    }
    false
}

fn source_has_security_identifier(source: &str) -> bool {
    let bytes = source.as_bytes();
    let mut index = 0;
    while index < bytes.len() {
        if let Some(next_index) = skip_js_literal_or_comment(bytes, index) {
            index = next_index;
            continue;
        }
        let Some((identifier, next_index)) = read_ascii_js_identifier(source, index) else {
            index += 1;
            continue;
        };
        if identifier_has_security_part(identifier) {
            return true;
        }
        index = next_index;
    }
    false
}

fn skip_js_literal_or_comment(bytes: &[u8], index: usize) -> Option<usize> {
    match bytes.get(index).copied()? {
        b'\'' | b'"' | b'`' => skip_js_quoted(bytes, index),
        b'/' if bytes.get(index + 1) == Some(&b'/') => {
            let mut end = index + 2;
            while end < bytes.len() && bytes[end] != b'\n' {
                end += 1;
            }
            Some(end)
        }
        b'/' if bytes.get(index + 1) == Some(&b'*') => {
            let mut end = index + 2;
            while end + 1 < bytes.len() {
                if bytes[end] == b'*' && bytes[end + 1] == b'/' {
                    return Some(end + 2);
                }
                end += 1;
            }
            Some(bytes.len())
        }
        _ => None,
    }
}

fn skip_js_quoted(bytes: &[u8], index: usize) -> Option<usize> {
    let quote = bytes.get(index).copied()?;
    let mut end = index + 1;
    while end < bytes.len() {
        if bytes[end] == b'\\' {
            end = (end + 2).min(bytes.len());
        } else if bytes[end] == quote {
            return Some(end + 1);
        } else {
            end += 1;
        }
    }
    Some(bytes.len())
}

fn read_ascii_js_identifier(source: &str, start: usize) -> Option<(&str, usize)> {
    let bytes = source.as_bytes();
    let first = *bytes.get(start)?;
    if !ascii_js_identifier_start(first) {
        return None;
    }
    let mut end = start + 1;
    while end < bytes.len() && ascii_js_identifier_part(bytes[end]) {
        end += 1;
    }
    Some((&source[start..end], end))
}

fn skip_ascii_whitespace(bytes: &[u8], mut index: usize) -> usize {
    while index < bytes.len() && bytes[index].is_ascii_whitespace() {
        index += 1;
    }
    index
}

fn ascii_js_identifier_start(byte: u8) -> bool {
    byte == b'$' || byte == b'_' || byte.is_ascii_alphabetic()
}

fn ascii_js_identifier_part(byte: u8) -> bool {
    ascii_js_identifier_start(byte) || byte.is_ascii_digit()
}

fn identifier_has_security_part(identifier: &str) -> bool {
    let mut part = String::new();
    let mut chars = identifier.chars().peekable();
    let mut previous_was_upper = false;
    while let Some(ch) = chars.next() {
        if ch == '_' || ch == '$' || ch.is_ascii_digit() {
            if security_identifier_part_matches(&part) {
                return true;
            }
            part.clear();
            previous_was_upper = false;
            continue;
        }

        if ch.is_ascii_uppercase() {
            let next_is_lower = chars
                .peek()
                .map(|next| next.is_ascii_lowercase())
                .unwrap_or(false);
            if !part.is_empty() && (!previous_was_upper || next_is_lower) {
                if security_identifier_part_matches(&part) {
                    return true;
                }
                part.clear();
            }
            part.push(ch.to_ascii_lowercase());
            previous_was_upper = true;
        } else {
            part.push(ch.to_ascii_lowercase());
            previous_was_upper = false;
        }
    }
    security_identifier_part_matches(&part)
}

fn security_identifier_part_matches(part: &str) -> bool {
    matches!(
        part,
        "auth"
            | "credential"
            | "hmac"
            | "integrity"
            | "mac"
            | "password"
            | "secret"
            | "signature"
            | "token"
            | "verify"
    )
}

fn sloppy_codec_import_name_supported(name: &str) -> bool {
    CODEC_EXPORTS.contains(&name)
}

fn sloppy_net_import_name_supported(name: &str) -> bool {
    matches!(
        name,
        "TcpClient" | "TcpListener" | "TcpConnection" | "NetworkAddress" | "HttpClient"
    )
}

fn sloppy_os_import_name_supported(name: &str) -> bool {
    matches!(name, "System" | "Environment" | "Process" | "Signals")
}

fn sloppy_workers_import_name_supported(name: &str) -> bool {
    WORKER_EXPORTS.contains(&name)
}

#[derive(Debug, Clone, Copy)]
enum SloppyStdlibImport {
    Fs,
    Time,
    Crypto,
    Codec,
    Net,
    Os,
    Workers,
}

impl SloppyStdlibImport {
    fn from_source(source: &str) -> Option<Self> {
        match source {
            "sloppy/fs" => Some(Self::Fs),
            "sloppy/time" => Some(Self::Time),
            "sloppy/crypto" => Some(Self::Crypto),
            "sloppy/codec" => Some(Self::Codec),
            "sloppy/net" => Some(Self::Net),
            "sloppy/os" => Some(Self::Os),
            "sloppy/workers" => Some(Self::Workers),
            _ => None,
        }
    }

    fn name_supported(self, name: &str) -> bool {
        match self {
            Self::Fs => matches!(
                name,
                "File" | "Directory" | "Path" | "FileHandle" | "FileWatcher"
            ),
            Self::Time => sloppy_time_import_name_supported(name),
            Self::Crypto => sloppy_crypto_import_name_supported(name),
            Self::Codec => sloppy_codec_import_name_supported(name),
            Self::Net => sloppy_net_import_name_supported(name),
            Self::Os => sloppy_os_import_name_supported(name),
            Self::Workers => sloppy_workers_import_name_supported(name),
        }
    }
}

fn validate_module_sloppy_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
    module_name: &str,
    is_supported: fn(&str) -> bool,
) -> Result<(), Diagnostic> {
    let Some(specifiers) = &import.specifiers else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
            format!("unsupported import specifier \"{module_name}\""),
        )
        .with_path(path)
        .with_span(import.source.span));
    };
    if specifiers.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
            format!("unsupported import specifier \"{module_name}\""),
        )
        .with_path(path)
        .with_span(import.source.span));
    }
    for specifier in specifiers {
        let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
                format!("unsupported import specifier \"{module_name}\""),
            )
            .with_path(path)
            .with_span(import.source.span));
        };
        let imported = specifier.imported.name().as_str();
        let local = specifier.local.name.as_str();
        if !is_supported(imported) || imported != local {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_IMPORT",
                format!("unsupported sloppy import \"{imported}\""),
            )
            .with_path(path)
            .with_span(specifier.span));
        }
    }
    Ok(())
}

fn validate_module_sloppy_time_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(
        path,
        import,
        "sloppy/time",
        sloppy_time_import_name_supported,
    )
}

fn validate_module_sloppy_crypto_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(
        path,
        import,
        "sloppy/crypto",
        sloppy_crypto_import_name_supported,
    )
}

fn validate_module_sloppy_codec_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(
        path,
        import,
        "sloppy/codec",
        sloppy_codec_import_name_supported,
    )
}

fn validate_module_sloppy_net_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(path, import, "sloppy/net", sloppy_net_import_name_supported)
}

fn validate_module_sloppy_os_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(path, import, "sloppy/os", sloppy_os_import_name_supported)
}

fn validate_module_sloppy_workers_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(
        path,
        import,
        "sloppy/workers",
        sloppy_workers_import_name_supported,
    )
}

fn import_specifier_is_runtime_value(
    import: &ImportDeclaration<'_>,
    specifier: &oxc_ast::ast::ImportSpecifier<'_>,
) -> bool {
    import.import_kind != ImportOrExportKind::Type
        && specifier.import_kind != ImportOrExportKind::Type
}

fn import_has_runtime_value_specifier(import: &ImportDeclaration<'_>) -> bool {
    let Some(specifiers) = &import.specifiers else {
        return false;
    };
    specifiers.iter().any(|specifier| {
        let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
            return false;
        };
        import_specifier_is_runtime_value(import, specifier)
    })
}

fn mark_sloppy_stdlib_runtime_import(state: &mut AppState, kind: SloppyStdlibImport) {
    match kind {
        SloppyStdlibImport::Fs => state.fs_imported = true,
        SloppyStdlibImport::Time => state.time_imported = true,
        SloppyStdlibImport::Crypto => state.crypto_imported = true,
        SloppyStdlibImport::Codec => state.codec_imported = true,
        SloppyStdlibImport::Net => state.net_imported = true,
        SloppyStdlibImport::Os => state.os_imported = true,
        SloppyStdlibImport::Workers => state.workers_imported = true,
    }
}

fn mark_sloppy_net_runtime_usage(
    net_runtime: &mut bool,
    http_client_runtime: &mut bool,
    imported: &str,
) {
    /* HttpClient is exported from sloppy/net but maps to stdlib.httpclient, not raw TCP stdlib.net. */
    if imported == "HttpClient" {
        *http_client_runtime = true;
    } else {
        *net_runtime = true;
    }
}

fn mark_sloppy_net_runtime_import(state: &mut AppState, imported: &str) {
    mark_sloppy_net_runtime_usage(
        &mut state.net_imported,
        &mut state.http_client_imported,
        imported,
    );
}

fn handle_sloppy_stdlib_import(
    import_source: &str,
    import: &ImportDeclaration<'_>,
    state: &mut AppState,
    kind: SloppyStdlibImport,
) {
    let Some(specifiers) = &import.specifiers else {
        state.unsupported_import_specifier = Some((import_source.to_string(), import.source.span));
        return;
    };
    if specifiers.is_empty() {
        state.unsupported_import_specifier = Some((import_source.to_string(), import.source.span));
        return;
    }
    for specifier in specifiers {
        let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
            state.unsupported_import_specifier =
                Some((import_source.to_string(), import.source.span));
            return;
        };
        let imported = specifier.imported.name().as_str();
        let local = specifier.local.name.as_str();
        if kind.name_supported(imported) && imported == local {
            if import_specifier_is_runtime_value(import, specifier) {
                if matches!(kind, SloppyStdlibImport::Net) {
                    mark_sloppy_net_runtime_import(state, imported);
                } else {
                    mark_sloppy_stdlib_runtime_import(state, kind);
                }
            }
        } else {
            if kind.name_supported(imported) {
                state.unsupported_import_alias = true;
            }
            state.unsupported_import_name = Some((imported.to_string(), specifier.span));
        }
    }
}

fn extract_import(
    path: &Path,
    graph: &ModuleGraph,
    state: &mut AppState,
    import: &oxc_ast::ast::ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    let import_source = import.source.value.as_str();
    if import_source.starts_with("./") || import_source.starts_with("../") {
        let resolved = resolve_relative_import(path, import_source).ok_or_else(|| {
            Diagnostic::new(
                "SLOPPYC_E_MISSING_RELATIVE_IMPORT",
                format!("relative import \"{import_source}\" could not be resolved"),
            )
            .with_path(path)
            .with_span(import.source.span)
            .with_hint("Use a relative .js/.mjs/.ts module inside the source root.")
        })?;
        if !resolver::stays_within_source_root(&resolved, &graph.entry_dir) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_RELATIVE_IMPORT",
                "relative imports must stay within the source root",
            )
            .with_path(path)
            .with_span(import.source.span));
        }
        if let Some(specifiers) = &import.specifiers {
            for specifier in specifiers {
                let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
                    state.unsupported_import_specifier =
                        Some((import_source.to_string(), import.source.span));
                    return Ok(());
                };
                state.imported_modules.push(ImportedModule {
                    local_name: specifier.local.name.as_str().to_string(),
                    export_name: specifier.imported.name().as_str().to_string(),
                    path: resolved.clone(),
                    span: specifier.span,
                });
            }
        }
        return Ok(());
    }

    if matches!(
        import_source,
        "sloppy/providers/sqlite" | "sloppy/providers/postgres" | "sloppy/providers/sqlserver"
    ) {
        let supported_type_name = match import_source {
            "sloppy/providers/sqlite" => "Sqlite",
            "sloppy/providers/postgres" => "Postgres",
            "sloppy/providers/sqlserver" => "SqlServer",
            _ => unreachable!(),
        };
        if let Some(specifiers) = &import.specifiers {
            for specifier in specifiers {
                let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
                    state.unsupported_import_specifier =
                        Some((import_source.to_string(), import.source.span));
                    return Ok(());
                };
                let imported = specifier.imported.name().as_str();
                let local = specifier.local.name.as_str();
                if imported == "sqlite" && local == "sqlite" {
                    state.sqlite_imported = true;
                } else if imported == supported_type_name && local == supported_type_name {
                    /* Provider marker imports are compiler metadata only in this slice. */
                } else {
                    state.unsupported_import_name = Some((imported.to_string(), specifier.span));
                }
            }
        }
        return Ok(());
    }

    if import_source == "sloppy/data" {
        if let Some(specifiers) = &import.specifiers {
            for specifier in specifiers {
                let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
                    state.unsupported_import_specifier =
                        Some((import_source.to_string(), import.source.span));
                    return Ok(());
                };
                let imported = specifier.imported.name().as_str();
                let local = specifier.local.name.as_str();
                if imported == "sql" && local == "sql" {
                    state.sql_imported = true;
                    state.data_imported = true;
                } else {
                    state.unsupported_import_name = Some((imported.to_string(), specifier.span));
                }
            }
        }
        return Ok(());
    }

    if let Some(kind) = SloppyStdlibImport::from_source(import_source) {
        handle_sloppy_stdlib_import(import_source, import, state, kind);
        return Ok(());
    }

    if import_source != "sloppy" {
        state.unsupported_import_specifier =
            Some((import.source.value.as_str().to_string(), import.source.span));
        return Ok(());
    }

    if let Some(specifiers) = &import.specifiers {
        for specifier in specifiers {
            let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
                state.unsupported_import_specifier =
                    Some((import.source.value.as_str().to_string(), import.source.span));
                return Ok(());
            };

            let imported = specifier.imported.name().as_str();
            let local = specifier.local.name.as_str();
            if sloppy_root_import_name_supported(imported) && imported != local {
                state.unsupported_import_alias = true;
                state.unsupported_import_name = Some((imported.to_string(), specifier.span));
            }
            match (imported, local) {
                ("Sloppy", "Sloppy") => state.sloppy_imported = true,
                ("Results", "Results") => state.results_imported = true,
                ("data", "data") => state.data_imported = true,
                ("schema", "schema") => state.schema_imported = true,
                _ if sloppy_root_import_name_supported(imported) && imported == local => {}
                _ if sloppy_root_import_name_supported(imported) => {}
                _ => {
                    state.unsupported_import_name = Some((imported.to_string(), specifier.span));
                }
            }
        }
    }
    Ok(())
}

fn sloppy_root_import_name_supported(name: &str) -> bool {
    matches!(
        name,
        "Sloppy"
            | "Results"
            | "data"
            | "schema"
            | "Email"
            | "NonEmptyString"
            | "PasswordString"
            | "SecretString"
            | "Uuid"
            | "PositiveInt"
            | "DateTime"
            | "Instant"
            | "RequestContext"
            | "SlopRequest"
            | "SlopResponse"
            | "CancellationSignal"
            | "Deadline"
            | "Route"
            | "Query"
            | "Body"
            | "Header"
            | "Service"
            | "Config"
    )
}

fn extract_variable_declaration(
    path: &Path,
    source: &str,
    source_name: &str,
    state: &mut AppState,
    declaration: &oxc_ast::ast::VariableDeclaration<'_>,
) -> Result<(), Diagnostic> {
    for declarator in &declaration.declarations {
        let Some(name) = binding_identifier(&declarator.id) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_BINDING",
                "supported app compiler only supports identifier bindings",
            )
            .with_path(path)
            .with_span(declarator.span));
        };

        let Some(init) = &declarator.init else {
            continue;
        };

        if is_sloppy_factory_call(init, "create") {
            state.app_vars.insert(name.to_string());
        } else if is_sloppy_factory_call(init, "createBuilder") {
            state.builder_vars.insert(name.to_string());
        } else if let Some(builder_name) = builder_build_object(init) {
            if state.builder_vars.contains(builder_name) {
                state.app_vars.insert(name.to_string());
            } else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_APP_FACTORY",
                    "builder.build() must be called on a Sloppy.createBuilder() variable",
                )
                .with_path(path)
                .with_span(init.span()));
            }
        } else if let Some((receiver, prefix)) = app_group_call(init) {
            let full_prefix = if state.app_vars.contains(receiver) {
                prefix.to_string()
            } else if let Some(parent_prefix) = state.group_vars.get(receiver) {
                join_route_patterns(parent_prefix, prefix)
            } else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_ROUTE_GROUP",
                    "route groups must be created from the extracted app object or another extracted route group",
                )
                .with_path(path)
                .with_span(init.span()));
            };
            state.group_vars.insert(name.to_string(), full_prefix);
        } else if let Some(provider) = sqlite_provider_call(init, source, source_name) {
            state.provider_bindings.insert(
                name.to_string(),
                ProviderBinding {
                    token: provider.token,
                    capability_kind: "database".to_string(),
                    provider: provider.provider,
                },
            );
        } else if let Some(binding) = app_provider_lookup(init, state) {
            state.provider_bindings.insert(name.to_string(), binding);
        } else if helper_initializer(init).is_some() {
            let Some(init_source) = source_slice(source, init.span()) else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HELPER",
                    "helper source could not be extracted",
                )
                .with_path(path)
                .with_span(init.span()));
            };
            let helper_source = format!("const {name} = {init_source};");
            let summary = helper_effects_from_initializer(
                init,
                &state.provider_bindings,
                source,
                source_name,
            );
            state.helper_sources.insert(name.to_string(), helper_source);
            state.helper_effects.insert(name.to_string(), summary);
            resolve_helper_effect_callgraph(&mut state.helper_effects);
        } else if state.schema_imported {
            if let Some(schema) = schema_declaration(path, source, source_name, name, init)? {
                state.schemas.push(schema);
            } else if let Some(config_reads) =
                config_read_metadata(path, source, source_name, state, init)?
            {
                state.config_reads.extend(config_reads);
            } else if let Some(diagnostic) = malformed_config_read_diagnostic(path, state, init) {
                return Err(diagnostic);
            } else {
                validate_supported_initializer(path, source, source_name, state, init)?;
            }
        } else if let Some(config_reads) =
            config_read_metadata(path, source, source_name, state, init)?
        {
            state.config_reads.extend(config_reads);
        } else if let Some(diagnostic) = malformed_config_read_diagnostic(path, state, init) {
            return Err(diagnostic);
        } else {
            validate_supported_initializer(path, source, source_name, state, init)?;
        }
    }
    Ok(())
}

fn extract_function_declaration(
    path: &Path,
    source: &str,
    source_name: &str,
    state: &mut AppState,
    function: &oxc_ast::ast::Function<'_>,
) -> Result<(), Diagnostic> {
    let Some(identifier) = &function.id else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HELPER",
            "function helpers must have a name",
        )
        .with_path(path)
        .with_span(function.span));
    };
    let Some(helper_source) = source_slice(source, function.span) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HELPER",
            "helper source could not be extracted",
        )
        .with_path(path)
        .with_span(function.span));
    };
    let name = identifier.name.as_str().to_string();
    let summary = function_effects_from_function(
        function,
        &state.provider_bindings,
        &BTreeMap::new(),
        source,
        source_name,
    );
    state.helper_sources.insert(name.clone(), helper_source);
    state.helper_effects.insert(name, summary);
    resolve_helper_effect_callgraph(&mut state.helper_effects);
    Ok(())
}

fn helper_initializer(expression: &Expression<'_>) -> Option<()> {
    match expression {
        Expression::ArrowFunctionExpression(_) | Expression::FunctionExpression(_) => Some(()),
        _ => None,
    }
}

fn extract_expression_statement(
    path: &Path,
    source: &str,
    source_name: &str,
    state: &mut AppState,
    statement: &ExpressionStatement<'_>,
) -> Result<(), Diagnostic> {
    if let Some(capability) =
        database_capability_call(path, source, source_name, &statement.expression, state)?
    {
        add_manual_database_capability(state, capability);
        return Ok(());
    }

    if let Some(provider) =
        app_use_provider_call(path, source, source_name, &statement.expression, state)?
    {
        add_sqlite_provider_capability(state, provider);
        return Ok(());
    }

    if let Some((module_name, span)) = app_use_module_call(&statement.expression, state) {
        state.used_modules.push((module_name, span));
        return Ok(());
    }

    if let Some(registration) =
        app_service_registration_call(path, source, source_name, &statement.expression, state)?
    {
        state.service_registrations.push(registration);
        return Ok(());
    }

    let (route_expr, name) = match &statement.expression {
        Expression::CallExpression(call) => match with_name_call(call)? {
            Some((inner, name)) => (inner, Some(name)),
            None => (&statement.expression, None),
        },
        _ => (&statement.expression, None),
    };

    let Some((receiver, method, pattern, handler_arg)) = route_call_parts(route_expr) else {
        if let Some(diagnostic) = unsupported_route_call_diagnostic(path, route_expr, source, state)
        {
            return Err(diagnostic);
        }

        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_TOP_LEVEL",
            "unsupported top-level expression in compiler extraction",
        )
        .with_path(path)
        .with_span(statement.span)
        .with_hint(
            "Use literal route declarations or builder.capabilities.addDatabase(...) metadata.",
        ));
    };

    let full_pattern = if state.app_vars.contains(receiver) {
        pattern.to_string()
    } else if let Some(prefix) = state.group_vars.get(receiver) {
        join_route_patterns(prefix, pattern)
    } else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_TARGET",
            "route declarations must be called on the extracted app or a route group variable",
        )
        .with_path(path)
        .with_span(statement.span));
    };

    let normalized_pattern = normalize_framework_route_pattern(&full_pattern);
    if !route_pattern_supported(&normalized_pattern) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_PATTERN",
            "route pattern is outside the Plan v1 alpha route syntax",
        )
        .with_path(path)
        .with_span(statement.span)
        .with_hint("Use '/', static segments, {name}, {name:str}, or {name:int}."));
    }

    let schema_names = schema_names(state);
    let handler_context = HandlerExtractionContext {
        route_pattern: &full_pattern,
        source,
        source_name,
        allow_data_handler_body: state.data_imported,
        schema_names: &schema_names,
        provider_bindings: &state.provider_bindings,
        helper_effects: &state.helper_effects,
    };
    let Some(handler) = handler_from_argument(handler_arg, &handler_context) else {
        return Err(handler_diagnostic(
            path,
            handler_arg,
            &full_pattern,
            &schema_names,
            statement.span,
        ));
    };

    let mut handler = handler;
    if !handler.effects.is_empty() {
        let providers = providers_used_by_effects(&state.provider_bindings, &handler.effects);
        if let Some((name, binding)) = providers
            .iter()
            .find(|(_, binding)| !provider_has_generated_runtime_bridge(binding))
        {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE",
                format!(
                    "{} provider-backed handlers are not executable by the compiler-generated runtime bridge yet",
                    binding.provider
                ),
            )
            .with_path(path)
            .with_span(statement.span)
            .with_hint(format!(
                "Provider handle '{name}' is recognized for Plan metadata, but only the SQLite generated bridge is executable by the current compiler/runtime contract."
            )));
        }
        let helper_sources = state.helper_sources.values().cloned().collect::<Vec<_>>();
        handler.source = wrap_handler_with_providers_and_helpers(
            &handler.source,
            &providers,
            &helper_sources,
            handler.is_async,
        );
    }

    state.routes.push(Route {
        method,
        framework_path: (normalized_pattern != full_pattern).then_some(full_pattern),
        pattern: normalized_pattern,
        name,
        span: statement.span,
        source_path: path.to_path_buf(),
        source_name: source_name.to_string(),
        source: source.to_string(),
        module: None,
        handler,
    });
    Ok(())
}

fn unsupported_route_call_diagnostic(
    path: &Path,
    expression: &Expression<'_>,
    source: &str,
    state: &AppState,
) -> Option<Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };

    if computed_member_receiver(&call.callee).is_some_and(|receiver| {
        state.app_vars.contains(receiver) || state.group_vars.contains_key(receiver)
    }) {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_COMPUTED_ROUTE_METHOD",
                "computed route registration methods are not supported",
            )
            .with_path(path)
            .with_span(call.span)
            .with_hint("Use an explicit call such as app.mapGet(\"/literal\", handler)."),
        );
    }

    let (receiver, property) = static_member_name(&call.callee)?;
    if route_method_from_property(property).is_none() {
        if unsupported_route_method_property(property)
            && (state.app_vars.contains(receiver) || state.group_vars.contains_key(receiver))
        {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HTTP_METHOD",
                    "unsupported route declaration method",
                )
                .with_path(path)
                .with_span(call.span)
                .with_hint("Supported compiler methods are mapGet, mapPost, mapPut, mapPatch, and mapDelete."),
            );
        }
        return None;
    }

    if call.arguments.len() != 2 {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_SHAPE",
                "route declarations require a literal pattern and one handler",
            )
            .with_path(path)
            .with_span(call.span),
        );
    }

    if call.arguments.first().and_then(string_argument).is_none() {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_DYNAMIC_ROUTE_PATTERN",
                "route pattern must be a string literal",
            )
            .with_path(path)
            .with_span(call.span)
            .with_hint("Dynamic route strings are not part of the supported app compiler."),
        );
    }

    if call
        .arguments
        .get(1)
        .and_then(|argument| {
            let context = HandlerExtractionContext {
                route_pattern: call
                    .arguments
                    .first()
                    .and_then(string_argument)
                    .unwrap_or_default(),
                source,
                source_name: "",
                allow_data_handler_body: state.data_imported,
                schema_names: &schema_names(state),
                provider_bindings: &state.provider_bindings,
                helper_effects: &state.helper_effects,
            };
            handler_from_argument(argument, &context)
        })
        .is_none()
    {
        let handler_argument = call.arguments.get(1)?;
        return Some(handler_diagnostic(
            path,
            handler_argument,
            call.arguments
                .first()
                .and_then(string_argument)
                .unwrap_or_default(),
            &schema_names(state),
            call.span,
        ));
    }

    None
}

fn unsupported_route_method_property(property: &str) -> bool {
    property.starts_with("map") || matches!(property, "head" | "options")
}

fn validate_supported_initializer(
    path: &Path,
    source: &str,
    source_name: &str,
    state: &AppState,
    init: &Expression<'_>,
) -> Result<(), Diagnostic> {
    if let Some((_, _, _, _)) = route_call(
        init,
        source,
        source_name,
        state.data_imported,
        &schema_names(state),
        &state.provider_bindings,
        &state.helper_effects,
    ) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_SHAPE",
            "route registration must be a top-level statement, not a variable initializer",
        )
        .with_path(path)
        .with_span(init.span()));
    }
    if let Some(diagnostic) = unsupported_route_call_diagnostic(path, init, source, state) {
        return Err(diagnostic);
    }
    Ok(())
}

fn statement_dynamic_import_span(statement: &Statement<'_>) -> Option<Span> {
    match statement {
        Statement::VariableDeclaration(declaration) => {
            declaration.declarations.iter().find_map(|declarator| {
                declarator
                    .init
                    .as_ref()
                    .and_then(expression_dynamic_import_span)
            })
        }
        Statement::ExpressionStatement(statement) => {
            expression_dynamic_import_span(&statement.expression)
        }
        Statement::ReturnStatement(statement) => statement
            .argument
            .as_ref()
            .and_then(expression_dynamic_import_span),
        Statement::BlockStatement(block) => {
            block.body.iter().find_map(statement_dynamic_import_span)
        }
        Statement::FunctionDeclaration(function) => function.body.as_ref().and_then(|body| {
            body.statements
                .iter()
                .find_map(statement_dynamic_import_span)
        }),
        Statement::IfStatement(statement) => expression_dynamic_import_span(&statement.test)
            .or_else(|| statement_dynamic_import_span(&statement.consequent))
            .or_else(|| {
                statement
                    .alternate
                    .as_ref()
                    .and_then(statement_dynamic_import_span)
            }),
        Statement::DoWhileStatement(statement) => statement_dynamic_import_span(&statement.body)
            .or_else(|| expression_dynamic_import_span(&statement.test)),
        Statement::WhileStatement(statement) => expression_dynamic_import_span(&statement.test)
            .or_else(|| statement_dynamic_import_span(&statement.body)),
        Statement::ForStatement(statement) => statement
            .init
            .as_ref()
            .and_then(for_init_dynamic_import_span)
            .or_else(|| {
                statement
                    .test
                    .as_ref()
                    .and_then(expression_dynamic_import_span)
            })
            .or_else(|| {
                statement
                    .update
                    .as_ref()
                    .and_then(expression_dynamic_import_span)
            })
            .or_else(|| statement_dynamic_import_span(&statement.body)),
        Statement::ForInStatement(statement) => expression_dynamic_import_span(&statement.right)
            .or_else(|| statement_dynamic_import_span(&statement.body)),
        Statement::ForOfStatement(statement) => expression_dynamic_import_span(&statement.right)
            .or_else(|| statement_dynamic_import_span(&statement.body)),
        Statement::SwitchStatement(statement) => {
            expression_dynamic_import_span(&statement.discriminant).or_else(|| {
                statement.cases.iter().find_map(|case| {
                    case.test
                        .as_ref()
                        .and_then(expression_dynamic_import_span)
                        .or_else(|| {
                            case.consequent
                                .iter()
                                .find_map(statement_dynamic_import_span)
                        })
                })
            })
        }
        Statement::TryStatement(statement) => statement
            .block
            .body
            .iter()
            .find_map(statement_dynamic_import_span)
            .or_else(|| {
                statement.handler.as_ref().and_then(|handler| {
                    handler
                        .body
                        .body
                        .iter()
                        .find_map(statement_dynamic_import_span)
                })
            })
            .or_else(|| {
                statement.finalizer.as_ref().and_then(|finalizer| {
                    finalizer
                        .body
                        .iter()
                        .find_map(statement_dynamic_import_span)
                })
            }),
        Statement::ThrowStatement(statement) => expression_dynamic_import_span(&statement.argument),
        _ => None,
    }
}

fn for_init_dynamic_import_span(init: &ForStatementInit<'_>) -> Option<Span> {
    match init {
        ForStatementInit::VariableDeclaration(declaration) => {
            declaration.declarations.iter().find_map(|declarator| {
                declarator
                    .init
                    .as_ref()
                    .and_then(expression_dynamic_import_span)
            })
        }
        ForStatementInit::ImportExpression(node) => Some(node.span),
        ForStatementInit::AwaitExpression(node) => expression_dynamic_import_span(&node.argument),
        ForStatementInit::CallExpression(call) => expression_dynamic_import_span(&call.callee)
            .or_else(|| call.arguments.iter().find_map(argument_dynamic_import_span)),
        ForStatementInit::ParenthesizedExpression(node) => {
            expression_dynamic_import_span(&node.expression)
        }
        _ => None,
    }
}

fn expression_dynamic_import_span(expression: &Expression<'_>) -> Option<Span> {
    match expression {
        Expression::ImportExpression(node) => Some(node.span),
        Expression::AwaitExpression(node) => expression_dynamic_import_span(&node.argument),
        Expression::CallExpression(call) => expression_dynamic_import_span(&call.callee)
            .or_else(|| call.arguments.iter().find_map(argument_dynamic_import_span)),
        Expression::ParenthesizedExpression(node) => {
            expression_dynamic_import_span(&node.expression)
        }
        Expression::ArrowFunctionExpression(function) => function
            .body
            .statements
            .iter()
            .find_map(statement_dynamic_import_span),
        Expression::FunctionExpression(function) => function.body.as_ref().and_then(|body| {
            body.statements
                .iter()
                .find_map(statement_dynamic_import_span)
        }),
        _ => None,
    }
}

fn argument_dynamic_import_span(argument: &Argument<'_>) -> Option<Span> {
    match argument {
        Argument::ImportExpression(node) => Some(node.span),
        Argument::AwaitExpression(node) => expression_dynamic_import_span(&node.argument),
        Argument::CallExpression(call) => expression_dynamic_import_span(&call.callee)
            .or_else(|| call.arguments.iter().find_map(argument_dynamic_import_span)),
        Argument::ArrowFunctionExpression(function) => function
            .body
            .statements
            .iter()
            .find_map(statement_dynamic_import_span),
        Argument::FunctionExpression(function) => function.body.as_ref().and_then(|body| {
            body.statements
                .iter()
                .find_map(statement_dynamic_import_span)
        }),
        Argument::ParenthesizedExpression(node) => expression_dynamic_import_span(&node.expression),
        _ => None,
    }
}

fn database_capability_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<DatabaseCapability>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some(chain) = static_member_chain(&call.callee) else {
        return Ok(None);
    };
    if chain.len() != 3 || chain[1] != "capabilities" || chain[2] != "addDatabase" {
        return Ok(None);
    }
    if !state.builder_vars.contains(chain[0]) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_TARGET",
            "database capabilities must be declared on the extracted builder",
        )
        .with_path(path)
        .with_span(call.span)
        .with_hint("Use builder.capabilities.addDatabase(...) before builder.build()."));
    }
    if call.arguments.len() != 2 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE",
            "addDatabase requires a literal token and an options object",
        )
        .with_path(path)
        .with_span(call.span));
    }

    let token = string_argument(call.arguments.first().ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE",
            "missing database capability token",
        )
        .with_path(path)
        .with_span(call.span)
    })?)
    .ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_TOKEN",
            "database capability token must be a string literal",
        )
        .with_path(path)
        .with_span(argument_span(&call.arguments[0]).unwrap_or(call.span))
    })?;
    if !plan_token_supported(token) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_TOKEN",
            "database capability token uses unsupported characters",
        )
        .with_path(path)
        .with_span(argument_span(&call.arguments[0]).unwrap_or(call.span))
        .with_hint("Use letters, digits, '.', '_', and '-' in capability tokens."));
    }

    let options = object_argument(call.arguments.get(1).ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE",
            "missing database capability options",
        )
        .with_path(path)
        .with_span(call.span)
    })?)
    .ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE",
            "database capability options must be an object literal",
        )
        .with_path(path)
        .with_span(argument_span(&call.arguments[1]).unwrap_or(call.span))
    })?;

    reject_secret_option_fields(path, options)?;

    let provider = required_object_string_property(path, options, "provider")?;
    if !database_provider_supported(provider) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_DATA_PROVIDER",
            "compiler-emitted database provider metadata supports sqlite, postgres, and sqlserver",
        )
        .with_path(path)
        .with_span(options.span)
        .with_hint(
            "Use one of the first-party database provider values supported by Plan metadata.",
        ));
    }

    let access = optional_object_string_property(path, options, "access")?
        .unwrap_or("readwrite")
        .to_string();
    if !matches!(access.as_str(), "read" | "write" | "readwrite") {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_ACCESS",
            "database capability access must be read, write, or readwrite",
        )
        .with_path(path)
        .with_span(options.span));
    }

    // `path` is a transitional alias: output canonicalizes to `database`, and conflicting
    // dual-field values are rejected so generated plans stay unambiguous.
    let database = optional_object_string_property(path, options, "database")?;
    let path_option = optional_object_string_property(path, options, "path")?;
    if let (Some(database), Some(path_option)) = (database, path_option) {
        if database != path_option {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE",
                "database capability cannot declare different database and path values",
            )
            .with_path(path)
            .with_span(options.span)
            .with_hint(
                "Use the canonical database option; path is accepted only as a transitional alias.",
            ));
        }
    }
    let database = database.or(path_option).map(|value| value.to_string());

    Ok(Some(DatabaseCapability {
        token: token.to_string(),
        capability_kind: "database".to_string(),
        provider: provider.to_string(),
        config_name: None,
        access,
        database,
        config_source: None,
        source_name: source_name.to_string(),
        source: source.to_string(),
        span: call.span,
        from_provider_use: false,
    }))
}

fn database_provider_supported(provider: &str) -> bool {
    matches!(provider, "sqlite" | "postgres" | "sqlserver")
}

fn top_level_statement_diagnostic(
    path: &Path,
    source: &str,
    statement: &Statement<'_>,
) -> Diagnostic {
    let span = statement.span();
    let text = source_slice(source, span).unwrap_or_default();
    if top_level_statement_is_conditional(statement) && text.contains(".map") {
        return Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONDITIONAL_ROUTE_REGISTRATION",
            "conditional route registration is not supported",
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Register compiler-extracted routes unconditionally at the top level.");
    }
    if top_level_statement_is_loop(statement) && text.contains(".map") {
        return Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_LOOP_ROUTE_REGISTRATION",
            "loop-based route registration is not supported",
        )
        .with_path(path)
        .with_span(span)
        .with_hint("List compiler-extracted routes as explicit top-level route method calls.");
    }

    Diagnostic::new(
        "SLOPPYC_E_UNSUPPORTED_TOP_LEVEL",
        "unsupported top-level syntax in supported app compiler",
    )
    .with_path(path)
    .with_span(span)
    .with_hint(
        "Use imports, const app/builder/group declarations, literal route calls, and export default app.",
    )
}

fn top_level_statement_is_conditional(statement: &Statement<'_>) -> bool {
    matches!(
        statement,
        Statement::IfStatement(_) | Statement::SwitchStatement(_)
    )
}

fn top_level_statement_is_loop(statement: &Statement<'_>) -> bool {
    matches!(
        statement,
        Statement::ForStatement(_)
            | Statement::ForInStatement(_)
            | Statement::ForOfStatement(_)
            | Statement::WhileStatement(_)
            | Statement::DoWhileStatement(_)
    )
}

fn export_default_identifier(
    declaration: &oxc_ast::ast::ExportDefaultDeclarationKind<'_>,
) -> Option<String> {
    match declaration {
        oxc_ast::ast::ExportDefaultDeclarationKind::Identifier(identifier) => {
            Some(identifier.name.as_str().to_string())
        }
        _ => None,
    }
}

fn binding_identifier<'a>(binding: &'a BindingPattern<'a>) -> Option<&'a str> {
    match binding {
        BindingPattern::BindingIdentifier(identifier) => Some(identifier.name.as_str()),
        _ => None,
    }
}

fn is_sloppy_factory_call(expression: &Expression<'_>, method: &str) -> bool {
    let Expression::CallExpression(call) = expression else {
        return false;
    };
    static_member_name(&call.callee)
        .is_some_and(|(object, property)| object == "Sloppy" && property == method)
        && call.arguments.is_empty()
}

fn builder_build_object<'a>(expression: &'a Expression<'a>) -> Option<&'a str> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    if !call.arguments.is_empty() {
        return None;
    }
    static_member_name(&call.callee).and_then(|(object, property)| {
        if property == "build" {
            Some(object)
        } else {
            None
        }
    })
}

fn app_group_call<'a>(expression: &'a Expression<'a>) -> Option<(&'a str, &'a str)> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (object, property) = static_member_name(&call.callee)?;
    if !matches!(property, "mapGroup" | "group") || call.arguments.len() != 1 {
        return None;
    }
    let prefix = string_argument(call.arguments.first()?)?;
    Some((object, prefix))
}

fn app_provider_lookup(expression: &Expression<'_>, state: &AppState) -> Option<ProviderBinding> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if property != "provider" || !state.app_vars.contains(receiver) || call.arguments.len() != 1 {
        return None;
    }
    let token = string_argument(call.arguments.first()?)?;
    database_provider_binding_from_token(token)
}

fn config_read_metadata(
    _path: &Path,
    source: &str,
    source_name: &str,
    state: &AppState,
    expression: &Expression<'_>,
) -> Result<Option<Vec<ConfigReadMetadata>>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    if let Some(read) = config_call_metadata(call, source, source_name, state) {
        return Ok(Some(vec![read]));
    }
    let Some(reads) = config_bind_metadata(call, source, source_name, state) else {
        return Ok(None);
    };
    Ok(Some(reads))
}

fn config_call_metadata(
    call: &CallExpression<'_>,
    source: &str,
    source_name: &str,
    state: &AppState,
) -> Option<ConfigReadMetadata> {
    let Expression::StaticMemberExpression(method_member) = &call.callee else {
        return None;
    };
    let method = method_member.property.name.as_str();
    let Expression::StaticMemberExpression(config_member) = &method_member.object else {
        return None;
    };
    if config_member.property.name.as_str() != "config" {
        return None;
    }
    let Expression::Identifier(app) = &config_member.object else {
        return None;
    };
    if !state.app_vars.contains(app.name.as_str()) || call.arguments.is_empty() {
        return None;
    }
    let key = string_argument(call.arguments.first()?)?.to_string();
    let value_type = match method {
        "getString" => "string",
        "getInt" => "int",
        "getNumber" => "number",
        "getBoolean" => "bool",
        "getBool" => "bool",
        "getDuration" => "duration",
        "getSize" | "getBytes" => "size",
        "getArray" => "array",
        "getObject" => "object",
        "getSecret" => "secret",
        _ => return None,
    };
    let default_value = call.arguments.get(1).and_then(argument_json_value);
    let has_default = call.arguments.len() > 1;
    Some(ConfigReadMetadata {
        sensitive: method == "getSecret" || config_key_is_sensitive(&key),
        required: !has_default,
        key,
        value_type: value_type.to_string(),
        has_default,
        default_value,
        source_name: source_name.to_string(),
        source: source.to_string(),
        span: call.span,
    })
}

fn config_bind_metadata(
    call: &CallExpression<'_>,
    source: &str,
    source_name: &str,
    state: &AppState,
) -> Option<Vec<ConfigReadMetadata>> {
    let Expression::StaticMemberExpression(method_member) = &call.callee else {
        return None;
    };
    if method_member.property.name.as_str() != "bind" {
        return None;
    }
    let Expression::StaticMemberExpression(config_member) = &method_member.object else {
        return None;
    };
    if config_member.property.name.as_str() != "config" {
        return None;
    }
    let Expression::Identifier(app) = &config_member.object else {
        return None;
    };
    if !state.app_vars.contains(app.name.as_str()) || call.arguments.is_empty() {
        return None;
    }
    let prefix = string_argument(call.arguments.first()?)?;
    let Some(schema) = call.arguments.get(1).and_then(object_argument) else {
        return Some(vec![ConfigReadMetadata {
            key: provider_config_prefix(prefix),
            value_type: "object".to_string(),
            has_default: false,
            default_value: None,
            required: false,
            sensitive: false,
            source_name: source_name.to_string(),
            source: source.to_string(),
            span: call.span,
        }]);
    };
    let mut reads = Vec::new();
    for property in &schema.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            continue;
        };
        if property.computed {
            continue;
        }
        let name = property_key_name(&property.key)?;
        let Some((key_segment, value_type, has_default, default_value, required, sensitive)) =
            bind_descriptor_metadata(name, &property.value)
        else {
            continue;
        };
        let key = format!("{}:{}", provider_config_prefix(prefix), key_segment);
        reads.push(ConfigReadMetadata {
            key,
            value_type,
            has_default,
            default_value,
            required,
            sensitive,
            source_name: source_name.to_string(),
            source: source.to_string(),
            span: property.span,
        });
    }
    Some(reads)
}

fn bind_descriptor_metadata(
    name: &str,
    expression: &Expression<'_>,
) -> Option<(String, String, bool, Option<Value>, bool, bool)> {
    match expression {
        Expression::StringLiteral(literal) => {
            let value_type = literal.value.as_str();
            if !config_value_type_supported(value_type) {
                return None;
            }
            Some((
                config_bind_descriptor_segment(name),
                value_type.to_string(),
                false,
                None,
                true,
                value_type == "secret",
            ))
        }
        Expression::ObjectExpression(object) => {
            let value_type = object_string_property_value(object, "type")
                .or_else(|| {
                    object_bool_property_value(object, "secret")
                        .filter(|secret| *secret)
                        .map(|_| "secret")
                })
                .unwrap_or("string");
            if !config_value_type_supported(value_type) {
                return None;
            }
            let default_value = object_json_property_value(object, "default");
            let has_default = default_value.is_some();
            let required = object_bool_property_value(object, "required").unwrap_or(!has_default);
            let key_segment = object_string_property_value(object, "key")
                .map(str::to_string)
                .unwrap_or_else(|| config_bind_descriptor_segment(name));
            Some((
                key_segment,
                value_type.to_string(),
                has_default,
                default_value,
                required,
                value_type == "secret"
                    || object_bool_property_value(object, "secret").unwrap_or(false),
            ))
        }
        _ => None,
    }
}

fn config_value_type_supported(value_type: &str) -> bool {
    matches!(
        value_type,
        "array"
            | "bool"
            | "boolean"
            | "bytes"
            | "duration"
            | "int"
            | "integer"
            | "number"
            | "object"
            | "secret"
            | "size"
            | "string"
    )
}

fn provider_config_prefix(prefix: &str) -> String {
    if prefix.contains(':') && !prefix.starts_with("Sloppy:") && prefix.split(':').count() == 2 {
        let mut segments = prefix.split(':');
        let provider = segments.next().unwrap_or_default();
        let name = segments.next().unwrap_or_default();
        return format!("Sloppy:Providers:{provider}:{name}");
    }
    prefix.to_string()
}

fn config_bind_descriptor_segment(name: &str) -> String {
    if name.contains(':') {
        return name.to_string();
    }
    let mut chars = name.chars();
    let Some(first) = chars.next() else {
        return name.to_string();
    };
    first.to_ascii_uppercase().to_string() + chars.as_str()
}

fn malformed_config_read_diagnostic(
    path: &Path,
    state: &AppState,
    expression: &Expression<'_>,
) -> Option<Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let Expression::StaticMemberExpression(method_member) = &call.callee else {
        return None;
    };
    let method = method_member.property.name.as_str();
    if !matches!(
        method,
        "bind"
            | "getArray"
            | "getBool"
            | "getBoolean"
            | "getBytes"
            | "getDuration"
            | "getInt"
            | "getNumber"
            | "getObject"
            | "getSecret"
            | "getSize"
            | "getString"
    ) {
        return None;
    }
    let Expression::StaticMemberExpression(config_member) = &method_member.object else {
        return None;
    };
    if config_member.property.name.as_str() != "config" {
        return None;
    }
    let Expression::Identifier(app) = &config_member.object else {
        return None;
    };
    if !state.app_vars.contains(app.name.as_str()) {
        return None;
    }
    Some(
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONFIG_KEY",
            "config helper keys must be string literals",
        )
        .with_path(path)
        .with_span(call.span),
    )
}

fn schema_declaration(
    path: &Path,
    source: &str,
    source_name: &str,
    name: &str,
    expression: &Expression<'_>,
) -> Result<Option<SchemaMetadata>, Diagnostic> {
    if !expression_mentions_schema(expression) {
        return Ok(None);
    }
    let definition = schema_definition(expression).ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_SCHEMA",
            "schema declarations must use the supported schema DSL",
        )
        .with_path(path)
        .with_span(expression.span())
        .with_hint("Use schema.object/string/int/number/bool/array with literal object fields.")
    })?;
    Ok(Some(SchemaMetadata {
        name: name.to_string(),
        definition,
        source_name: source_name.to_string(),
        source: source.to_string(),
        span: expression.span(),
    }))
}

fn typescript_type_alias_schema(
    path: &Path,
    source: &str,
    source_name: &str,
    alias: &oxc_ast::ast::TSTypeAliasDeclaration<'_>,
    known_schema_names: &BTreeSet<String>,
) -> Result<Option<SchemaMetadata>, Diagnostic> {
    if alias.type_parameters.is_some() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_SCHEMA",
            "generic type aliases are not supported by Framework v2 schema inference",
        )
        .with_path(path)
        .with_span(alias.span)
        .with_hint(
            "Use concrete object aliases or interfaces for compiler-emitted schema metadata.",
        ));
    }
    let name = alias.id.name.as_str();
    let definition = typescript_schema_definition(
        path,
        source,
        &alias.type_annotation,
        known_schema_names,
        Some(name),
    )?;
    Ok(Some(SchemaMetadata {
        name: name.to_string(),
        definition,
        source_name: source_name.to_string(),
        source: source.to_string(),
        span: alias.span,
    }))
}

fn typescript_interface_schema(
    path: &Path,
    source: &str,
    source_name: &str,
    interface: &oxc_ast::ast::TSInterfaceDeclaration<'_>,
    known_schema_names: &BTreeSet<String>,
) -> Result<Option<SchemaMetadata>, Diagnostic> {
    if interface.type_parameters.is_some() || !interface.extends.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_SCHEMA",
            "generic or inherited interfaces are not supported by Framework v2 schema inference",
        )
        .with_path(path)
        .with_span(interface.span)
        .with_hint(
            "Use a concrete interface without extends for compiler-emitted schema metadata.",
        ));
    }
    let name = interface.id.name.as_str();
    let definition = typescript_object_schema_from_signatures(
        path,
        source,
        &interface.body.body,
        known_schema_names,
        Some(name),
    )?;
    Ok(Some(SchemaMetadata {
        name: name.to_string(),
        definition,
        source_name: source_name.to_string(),
        source: source.to_string(),
        span: interface.span,
    }))
}

fn typescript_schema_definition(
    path: &Path,
    source: &str,
    ty: &TSType<'_>,
    known_schema_names: &BTreeSet<String>,
    current_schema_name: Option<&str>,
) -> Result<Value, Diagnostic> {
    match ty {
        TSType::TSStringKeyword(_) => Ok(json!({ "kind": "string" })),
        TSType::TSNumberKeyword(_) => Ok(json!({ "kind": "number" })),
        TSType::TSBooleanKeyword(_) => Ok(json!({ "kind": "bool" })),
        TSType::TSNullKeyword(_) => Ok(json!({ "kind": "null" })),
        TSType::TSArrayType(array) => {
            let items = typescript_schema_definition(
                path,
                source,
                &array.element_type,
                known_schema_names,
                current_schema_name,
            )?;
            Ok(json!({ "kind": "array", "items": items }))
        }
        TSType::TSTypeLiteral(literal) => typescript_object_schema_from_signatures(
            path,
            source,
            &literal.members,
            known_schema_names,
            current_schema_name,
        ),
        TSType::TSTypeReference(reference) => {
            let Some(name) = typescript_type_name(&reference.type_name) else {
                return Err(unsupported_typescript_schema_diagnostic(
                    path,
                    reference.span,
                    "qualified or computed type references are not supported by Framework v2 schema inference",
                ));
            };
            semantic_or_reference_schema(
                path,
                reference.span,
                name,
                reference.type_arguments.as_deref(),
                known_schema_names,
                current_schema_name,
            )
        }
        TSType::TSLiteralType(literal) => literal_type_schema(path, literal),
        TSType::TSUnionType(union) => union_type_schema(
            path,
            source,
            &union.types,
            union.span,
            known_schema_names,
            current_schema_name,
        ),
        TSType::TSParenthesizedType(parenthesized) => typescript_schema_definition(
            path,
            source,
            &parenthesized.type_annotation,
            known_schema_names,
            current_schema_name,
        ),
        TSType::TSAnyKeyword(_)
        | TSType::TSUnknownKeyword(_)
        | TSType::TSConditionalType(_)
        | TSType::TSMappedType(_)
        | TSType::TSFunctionType(_)
        | TSType::TSImportType(_)
        | TSType::TSIndexedAccessType(_)
        | TSType::TSInferType(_)
        | TSType::TSIntersectionType(_)
        | TSType::TSTemplateLiteralType(_)
        | TSType::TSTupleType(_)
        | TSType::TSTypeOperatorType(_)
        | TSType::TSTypeQuery(_) => Err(unsupported_typescript_schema_diagnostic(
            path,
            ts_type_span(ty),
            "unsupported TypeScript type shape in Framework v2 schema inference",
        )),
        _ => Err(unsupported_typescript_schema_diagnostic(
            path,
            ts_type_span(ty),
            "unsupported TypeScript type keyword in Framework v2 schema inference",
        )),
    }
}

fn typescript_object_schema_from_signatures(
    path: &Path,
    source: &str,
    signatures: &[TSSignature<'_>],
    known_schema_names: &BTreeSet<String>,
    current_schema_name: Option<&str>,
) -> Result<Value, Diagnostic> {
    let mut properties = serde_json::Map::new();
    for signature in signatures {
        let TSSignature::TSPropertySignature(property) = signature else {
            return Err(unsupported_typescript_schema_diagnostic(
                path,
                ts_signature_span(signature),
                "only object properties are supported in Framework v2 schema metadata",
            ));
        };
        if property.computed {
            return Err(unsupported_typescript_schema_diagnostic(
                path,
                property.span,
                "computed TypeScript property names are not supported in Framework v2 schema metadata",
            ));
        }
        let Some(annotation) = &property.type_annotation else {
            return Err(unsupported_typescript_schema_diagnostic(
                path,
                property.span,
                "schema properties must include TypeScript type annotations",
            ));
        };
        let Some(key) = property_key_string(&property.key) else {
            return Err(unsupported_typescript_schema_diagnostic(
                path,
                property.span,
                "schema property names must be static strings or identifiers",
            ));
        };
        let mut value = typescript_schema_definition(
            path,
            source,
            &annotation.type_annotation,
            known_schema_names,
            current_schema_name,
        )?;
        if property.optional {
            value["optional"] = json!(true);
        }
        if schema_value_is_secret(&value) || key_is_secret_like(&key) {
            value["secret"] = json!(true);
            value["redaction"] = json!("secret");
        }
        properties.insert(key, value);
    }
    Ok(json!({ "kind": "object", "properties": properties }))
}

fn semantic_or_reference_schema(
    path: &Path,
    span: Span,
    name: &str,
    type_arguments: Option<&oxc_ast::ast::TSTypeParameterInstantiation<'_>>,
    known_schema_names: &BTreeSet<String>,
    current_schema_name: Option<&str>,
) -> Result<Value, Diagnostic> {
    match name {
        "Email" if type_arguments.is_none() => Ok(json!({
            "kind": "string",
            "semantic": "Email",
            "validation": "email"
        })),
        "NonEmptyString" if type_arguments.is_none() => Ok(json!({
            "kind": "string",
            "semantic": "NonEmptyString",
            "min": 1
        })),
        "SecretString" if type_arguments.is_none() => Ok(json!({
            "kind": "string",
            "semantic": "SecretString",
            "secret": true,
            "redaction": "secret"
        })),
        "PasswordString" => {
            let min = type_arguments
                .and_then(|arguments| arguments.params.first())
                .and_then(type_numeric_literal_value)
                .ok_or_else(|| {
                    Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_SCHEMA",
                        "PasswordString requires a numeric literal minimum length",
                    )
                    .with_path(path)
                    .with_span(span)
                    .with_hint("Use PasswordString<8> or another numeric literal.")
                })?;
            Ok(json!({
                "kind": "string",
                "semantic": "PasswordString",
                "min": min,
                "secret": true,
                "redaction": "secret"
            }))
        }
        "Uuid" if type_arguments.is_none() => Ok(json!({
            "kind": "string",
            "semantic": "Uuid",
            "validation": "uuid"
        })),
        "DateTime" | "Instant" if type_arguments.is_none() => Ok(json!({
            "kind": "string",
            "semantic": name,
            "validation": "datetime"
        })),
        "PositiveInt" if type_arguments.is_none() => Ok(json!({
            "kind": "int",
            "semantic": "PositiveInt",
            "min": 1
        })),
        _ if type_arguments.is_none() && current_schema_name == Some(name) => Err(
            unsupported_typescript_schema_diagnostic(
                path,
                span,
                "recursive TypeScript schema references are not supported by Framework v2 schema inference",
            ),
        ),
        _ if type_arguments.is_none() && known_schema_names.contains(name) => {
            Ok(json!({ "kind": "ref", "name": name }))
        }
        _ if type_arguments.is_none() => Err(Diagnostic::new(
            "SLOPPYC_E_UNRESOLVED_TYPE",
            "unresolved TypeScript type reference in Framework v2 schema inference",
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use a local type alias/interface or a supported Sloppy semantic built-in.")),
        _ => Err(unsupported_typescript_schema_diagnostic(
            path,
            span,
            "generic type references are not supported by Framework v2 schema inference",
        )),
    }
}

fn literal_type_schema(
    path: &Path,
    literal: &oxc_ast::ast::TSLiteralType<'_>,
) -> Result<Value, Diagnostic> {
    match &literal.literal {
        TSLiteral::StringLiteral(value) => {
            Ok(json!({ "kind": "literal", "value": value.value.as_str() }))
        }
        TSLiteral::NumericLiteral(value) => Ok(json!({ "kind": "literal", "value": value.value })),
        TSLiteral::BooleanLiteral(value) => Ok(json!({ "kind": "literal", "value": value.value })),
        _ => Err(unsupported_typescript_schema_diagnostic(
            path,
            literal.span,
            "only string, number, and boolean literal types are supported",
        )),
    }
}

fn union_type_schema(
    path: &Path,
    source: &str,
    types: &[TSType<'_>],
    span: Span,
    known_schema_names: &BTreeSet<String>,
    current_schema_name: Option<&str>,
) -> Result<Value, Diagnostic> {
    let mut nullable = false;
    let mut variants = Vec::new();
    for ty in types {
        if matches!(ty, TSType::TSNullKeyword(_)) {
            nullable = true;
            continue;
        }
        variants.push(typescript_schema_definition(
            path,
            source,
            ty,
            known_schema_names,
            current_schema_name,
        )?);
    }
    if nullable && variants.len() == 1 {
        let mut value = variants.remove(0);
        value["nullable"] = json!(true);
        return Ok(value);
    }
    if variants.is_empty() {
        return Err(unsupported_typescript_schema_diagnostic(
            path,
            span,
            "union types must contain at least one non-null variant",
        ));
    }
    if variants.iter().all(|value| value["kind"] == "literal") {
        return Ok(json!({ "kind": "literalUnion", "variants": variants }));
    }
    Err(unsupported_typescript_schema_diagnostic(
        path,
        span,
        "only nullable unions and literal unions are supported by Framework v2 schema inference",
    ))
}

fn unsupported_typescript_schema_diagnostic(path: &Path, span: Span, message: &str) -> Diagnostic {
    Diagnostic::new("SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_SCHEMA", message)
        .with_path(path)
        .with_span(span)
        .with_hint("Use concrete aliases, interfaces, object literals, arrays, primitives, semantic built-ins, nullable unions, or literal unions.")
}

fn type_numeric_literal_value(ty: &TSType<'_>) -> Option<u64> {
    let TSType::TSLiteralType(literal) = ty else {
        return None;
    };
    let TSLiteral::NumericLiteral(value) = &literal.literal else {
        return None;
    };
    if value.value.fract() == 0.0 && value.value >= 0.0 {
        Some(value.value as u64)
    } else {
        None
    }
}

fn typescript_type_name<'a>(name: &'a TSTypeName<'a>) -> Option<&'a str> {
    match name {
        TSTypeName::IdentifierReference(identifier) => Some(identifier.name.as_str()),
        _ => None,
    }
}

fn schema_value_is_secret(value: &Value) -> bool {
    value
        .get("secret")
        .and_then(Value::as_bool)
        .unwrap_or(false)
}

fn key_is_secret_like(key: &str) -> bool {
    let lower = key.to_ascii_lowercase();
    lower.contains("password") || lower.contains("secret") || lower.contains("token")
}

pub(crate) fn ts_type_span(ty: &TSType<'_>) -> Span {
    match ty {
        TSType::TSAnyKeyword(node) => node.span,
        TSType::TSBigIntKeyword(node) => node.span,
        TSType::TSBooleanKeyword(node) => node.span,
        TSType::TSIntrinsicKeyword(node) => node.span,
        TSType::TSNeverKeyword(node) => node.span,
        TSType::TSNullKeyword(node) => node.span,
        TSType::TSNumberKeyword(node) => node.span,
        TSType::TSObjectKeyword(node) => node.span,
        TSType::TSStringKeyword(node) => node.span,
        TSType::TSSymbolKeyword(node) => node.span,
        TSType::TSUndefinedKeyword(node) => node.span,
        TSType::TSUnknownKeyword(node) => node.span,
        TSType::TSVoidKeyword(node) => node.span,
        TSType::TSArrayType(node) => node.span,
        TSType::TSConditionalType(node) => node.span,
        TSType::TSConstructorType(node) => node.span,
        TSType::TSFunctionType(node) => node.span,
        TSType::TSImportType(node) => node.span,
        TSType::TSIndexedAccessType(node) => node.span,
        TSType::TSInferType(node) => node.span,
        TSType::TSIntersectionType(node) => node.span,
        TSType::TSLiteralType(node) => node.span,
        TSType::TSMappedType(node) => node.span,
        TSType::TSNamedTupleMember(node) => node.span,
        TSType::TSTemplateLiteralType(node) => node.span,
        TSType::TSThisType(node) => node.span,
        TSType::TSTupleType(node) => node.span,
        TSType::TSTypeLiteral(node) => node.span,
        TSType::TSTypeOperatorType(node) => node.span,
        TSType::TSTypePredicate(node) => node.span,
        TSType::TSTypeQuery(node) => node.span,
        TSType::TSTypeReference(node) => node.span,
        TSType::TSUnionType(node) => node.span,
        TSType::TSParenthesizedType(node) => node.span,
        TSType::JSDocNullableType(node) => node.span,
        TSType::JSDocNonNullableType(node) => node.span,
        TSType::JSDocUnknownType(node) => node.span,
    }
}

fn ts_signature_span(signature: &TSSignature<'_>) -> Span {
    match signature {
        TSSignature::TSIndexSignature(node) => node.span,
        TSSignature::TSPropertySignature(node) => node.span,
        TSSignature::TSCallSignatureDeclaration(node) => node.span,
        TSSignature::TSConstructSignatureDeclaration(node) => node.span,
        TSSignature::TSMethodSignature(node) => node.span,
    }
}

fn schema_definition(expression: &Expression<'_>) -> Option<Value> {
    match expression {
        Expression::CallExpression(call) => schema_definition_call(call),
        Expression::ParenthesizedExpression(parenthesized) => {
            schema_definition(&parenthesized.expression)
        }
        _ => None,
    }
}

fn schema_definition_call(call: &CallExpression<'_>) -> Option<Value> {
    if let Expression::StaticMemberExpression(member) = &call.callee {
        let property = member.property.name.as_str();
        if matches!(property, "optional" | "min" | "max" | "email") {
            let mut base = schema_definition(&member.object)?;
            if property == "optional" {
                if !call.arguments.is_empty() {
                    return None;
                }
                base["optional"] = json!(true);
            } else if property == "email" {
                if !call.arguments.is_empty() {
                    return None;
                }
                base["format"] = json!("email");
            } else {
                if call.arguments.len() != 1 {
                    return None;
                }
                let number = call.arguments.first().and_then(numeric_argument_value)?;
                base[property] = json!(number);
            }
            return Some(base);
        }
    }

    let (object, method) = static_member_name(&call.callee)?;
    if object != "schema" {
        return None;
    }
    match method {
        "string" | "int" | "number" | "bool" if call.arguments.is_empty() => {
            Some(json!({ "kind": method }))
        }
        "array" if call.arguments.len() == 1 => {
            let inner = call
                .arguments
                .first()
                .and_then(argument_schema_definition)?;
            Some(json!({ "kind": "array", "items": inner }))
        }
        "object" if call.arguments.len() == 1 => {
            let Argument::ObjectExpression(object) = call.arguments.first()? else {
                return None;
            };
            let mut properties = serde_json::Map::new();
            for property in &object.properties {
                let ObjectPropertyKind::ObjectProperty(property) = property else {
                    return None;
                };
                if property.kind != PropertyKind::Init
                    || property.method
                    || property.shorthand
                    || property.computed
                {
                    return None;
                }
                let key = property_key_string(&property.key)?;
                let value = schema_definition(&property.value)?;
                properties.insert(key, value);
            }
            Some(json!({ "kind": "object", "properties": properties }))
        }
        _ => None,
    }
}

fn argument_schema_definition(argument: &Argument<'_>) -> Option<Value> {
    match argument {
        Argument::CallExpression(call) => schema_definition_call(call),
        Argument::ParenthesizedExpression(parenthesized) => {
            schema_definition(&parenthesized.expression)
        }
        _ => None,
    }
}

fn expression_mentions_schema(expression: &Expression<'_>) -> bool {
    match expression {
        Expression::CallExpression(call) => call_mentions_schema(call),
        Expression::ObjectExpression(object) => object
            .properties
            .iter()
            .any(object_property_mentions_schema),
        Expression::ArrayExpression(array) => {
            array.elements.iter().any(array_element_mentions_schema)
        }
        Expression::ComputedMemberExpression(member) => {
            expression_mentions_schema(&member.object)
                || expression_mentions_schema(&member.expression)
        }
        Expression::StaticMemberExpression(member) => expression_mentions_schema(&member.object),
        Expression::ParenthesizedExpression(node) => expression_mentions_schema(&node.expression),
        Expression::ConditionalExpression(node) => {
            expression_mentions_schema(&node.test)
                || expression_mentions_schema(&node.consequent)
                || expression_mentions_schema(&node.alternate)
        }
        Expression::UnaryExpression(node) => expression_mentions_schema(&node.argument),
        Expression::BinaryExpression(node) => {
            expression_mentions_schema(&node.left) || expression_mentions_schema(&node.right)
        }
        Expression::LogicalExpression(node) => {
            expression_mentions_schema(&node.left) || expression_mentions_schema(&node.right)
        }
        Expression::SequenceExpression(node) => {
            node.expressions.iter().any(expression_mentions_schema)
        }
        Expression::TSAsExpression(node) => expression_mentions_schema(&node.expression),
        Expression::TSSatisfiesExpression(node) => expression_mentions_schema(&node.expression),
        Expression::TSTypeAssertion(node) => expression_mentions_schema(&node.expression),
        Expression::TSNonNullExpression(node) => expression_mentions_schema(&node.expression),
        Expression::TSInstantiationExpression(node) => expression_mentions_schema(&node.expression),
        _ => false,
    }
}

fn call_mentions_schema(call: &CallExpression<'_>) -> bool {
    static_member_name(&call.callee).is_some_and(|(object, _)| object == "schema")
        || match &call.callee {
            Expression::StaticMemberExpression(member) => {
                expression_mentions_schema(&member.object)
            }
            _ => false,
        }
        || call.arguments.iter().any(argument_mentions_schema)
}

fn argument_mentions_schema(argument: &Argument<'_>) -> bool {
    match argument {
        Argument::CallExpression(call) => call_mentions_schema(call),
        Argument::ObjectExpression(object) => object
            .properties
            .iter()
            .any(object_property_mentions_schema),
        Argument::ArrayExpression(array) => {
            array.elements.iter().any(array_element_mentions_schema)
        }
        Argument::ComputedMemberExpression(member) => {
            expression_mentions_schema(&member.object)
                || expression_mentions_schema(&member.expression)
        }
        Argument::ParenthesizedExpression(parenthesized) => {
            expression_mentions_schema(&parenthesized.expression)
        }
        Argument::StaticMemberExpression(member) => expression_mentions_schema(&member.object),
        _ => false,
    }
}

fn object_property_mentions_schema(property: &ObjectPropertyKind<'_>) -> bool {
    match property {
        ObjectPropertyKind::ObjectProperty(property) => expression_mentions_schema(&property.value),
        _ => false,
    }
}

fn array_element_mentions_schema(element: &ArrayExpressionElement<'_>) -> bool {
    match element {
        ArrayExpressionElement::CallExpression(call) => call_mentions_schema(call),
        ArrayExpressionElement::ObjectExpression(object) => object
            .properties
            .iter()
            .any(object_property_mentions_schema),
        ArrayExpressionElement::ArrayExpression(array) => {
            array.elements.iter().any(array_element_mentions_schema)
        }
        _ => false,
    }
}

fn property_key_string(key: &PropertyKey<'_>) -> Option<String> {
    match key {
        PropertyKey::StaticIdentifier(identifier) => Some(identifier.name.as_str().to_string()),
        PropertyKey::StringLiteral(literal) => Some(literal.value.as_str().to_string()),
        PropertyKey::NumericLiteral(literal) => Some(literal.value.to_string()),
        _ => None,
    }
}

fn numeric_argument_value(argument: &Argument<'_>) -> Option<f64> {
    match argument {
        Argument::NumericLiteral(literal) => Some(literal.value),
        _ => None,
    }
}

fn sqlite_provider_call(
    expression: &Expression<'_>,
    source: &str,
    source_name: &str,
) -> Option<DatabaseCapability> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    sqlite_provider_call_expression(call, source, source_name)
}

fn sqlite_provider_call_expression(
    call: &CallExpression<'_>,
    source: &str,
    source_name: &str,
) -> Option<DatabaseCapability> {
    let Expression::Identifier(callee) = &call.callee else {
        return None;
    };
    if callee.name.as_str() != "sqlite" {
        return None;
    };
    if call.arguments.is_empty() || call.arguments.len() > 2 {
        return None;
    }
    let name = string_argument(call.arguments.first()?)?;
    Some(DatabaseCapability {
        token: normalize_sqlite_provider_token(name),
        capability_kind: "database".to_string(),
        provider: "sqlite".to_string(),
        config_name: Some(name.to_string()),
        access: "readwrite".to_string(),
        database: None,
        config_source: None,
        source_name: source_name.to_string(),
        source: source.to_string(),
        span: call.span,
        from_provider_use: true,
    })
}

fn app_use_provider_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<DatabaseCapability>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if property != "use" || !state.app_vars.contains(receiver) || call.arguments.len() != 1 {
        return Ok(None);
    }
    let Some(Argument::CallExpression(provider_call)) = call.arguments.first() else {
        return Ok(None);
    };
    let Some(mut provider) = sqlite_provider_call_expression(provider_call, source, source_name)
    else {
        return Ok(None);
    };
    if let Some(options_argument) = provider_call.arguments.get(1) {
        let options = object_argument(options_argument).ok_or_else(|| {
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_PROVIDER_IMPORT",
                "sqlite provider import options must be an object literal",
            )
            .with_path(path)
            .with_span(argument_span(options_argument).unwrap_or(provider_call.span))
        })?;
        provider.database =
            optional_object_string_property(path, options, "database")?.map(ToOwned::to_owned);
        if provider.database.is_some() {
            provider.config_source = Some("inline".to_string());
        }
    }
    Ok(Some(provider))
}

fn app_use_module_call(expression: &Expression<'_>, state: &AppState) -> Option<(String, Span)> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if property != "useModule" || !state.app_vars.contains(receiver) || call.arguments.len() != 1 {
        return None;
    }
    let Argument::Identifier(identifier) = call.arguments.first()? else {
        return None;
    };
    Some((identifier.name.as_str().to_string(), identifier.span))
}

fn app_service_registration_call(
    path: &Path,
    source: &str,
    _source_name: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<ServiceRegistration>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some(chain) = static_member_chain(&call.callee) else {
        return Ok(None);
    };
    if chain.len() != 3 || chain[1] != "services" || !state.app_vars.contains(chain[0]) {
        return Ok(None);
    }
    let lifetime = match chain[2] {
        "addSingleton" => "singleton",
        "addScoped" => "scoped",
        "addTransient" => "transient",
        _ => return Ok(None),
    };
    if call.arguments.len() != 2 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_SERVICE_REGISTRATION",
            "service registrations require a string token and one factory",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let Some(token_argument) = call.arguments.first() else {
        return Ok(None);
    };
    let Some(factory_argument) = call.arguments.get(1) else {
        return Ok(None);
    };
    let token = string_argument(token_argument).ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_SERVICE_REGISTRATION",
            "service registration token must be a string literal",
        )
        .with_path(path)
        .with_span(argument_span(token_argument).unwrap_or(call.span))
    })?;
    if !matches!(
        factory_argument,
        Argument::ArrowFunctionExpression(_) | Argument::FunctionExpression(_)
    ) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_SERVICE_REGISTRATION",
            "service registration factory must be an inline function",
        )
        .with_path(path)
        .with_span(argument_span(factory_argument).unwrap_or(call.span)));
    }
    let Some(factory_source) =
        argument_span(factory_argument).and_then(|span| source_slice(source, span))
    else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_SERVICE_REGISTRATION",
            "service registration factory source could not be extracted",
        )
        .with_path(path)
        .with_span(call.span));
    };
    Ok(Some(ServiceRegistration {
        token: token.to_string(),
        lifetime,
        factory_source,
    }))
}

fn add_sqlite_provider_capability(state: &mut AppState, provider: DatabaseCapability) {
    if state
        .capabilities
        .iter()
        .any(|capability| !capability.from_provider_use && capability.token == provider.token)
    {
        state.app_provider_uses.insert(provider.token);
        return;
    }

    if state.app_provider_uses.insert(provider.token.clone()) {
        state.capabilities.push(provider);
        return;
    }

    if let Some(existing) = state
        .capabilities
        .iter_mut()
        .find(|capability| capability.from_provider_use && capability.token == provider.token)
    {
        *existing = provider;
    }
}

fn add_manual_database_capability(state: &mut AppState, capability: DatabaseCapability) {
    state
        .capabilities
        .retain(|existing| !(existing.from_provider_use && existing.token == capability.token));
    state.capabilities.push(capability);
}

fn normalize_sqlite_provider_token(name: &str) -> String {
    normalize_database_provider_token(name)
}

fn normalize_database_provider_token(name: &str) -> String {
    if name.starts_with("data.") {
        name.to_string()
    } else {
        format!("data.{name}")
    }
}

fn database_provider_binding_from_token(token: &str) -> Option<ProviderBinding> {
    let (provider, name) = token.split_once(':')?;
    if !database_provider_supported(provider) {
        return None;
    }
    Some(ProviderBinding {
        token: normalize_database_provider_token(name),
        capability_kind: "database".to_string(),
        provider: provider.to_string(),
    })
}

fn resolve_relative_import(from_path: &Path, specifier: &str) -> Option<PathBuf> {
    resolver::resolve_relative_import(from_path, specifier)
}

fn extract_relative_module(
    graph: &mut ModuleGraph,
    imported: &ImportedModule,
) -> Result<Vec<Route>, Diagnostic> {
    if graph.visiting.contains(&imported.path) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_CIRCULAR_IMPORT",
            "circular relative imports are not supported",
        )
        .with_path(&imported.path)
        .with_span(imported.span)
        .with_hint(
            "Keep function modules acyclic for the current function-module compiler contract.",
        ));
    }
    if let Some(module) = graph.modules.get(&imported.path) {
        return cached_module_routes(module, imported);
    }
    graph.visiting.insert(imported.path.clone());

    let source = fs::read_to_string(&imported.path).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_INPUT",
            format!("failed to read imported module: {error}"),
        )
        .with_path(&imported.path)
    })?;
    let source_name = graph.record_source(&imported.path, &source);
    graph.noncrypto_hash_security_context_visible |=
        noncrypto_hash_security_context_visible(&source);
    graph.checksum_security_context_visible |= checksum_security_context_visible(&source);
    let source_type = source_type_for_path(&imported.path, ParseContext::Module)?;
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, &source, source_type).parse();
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_PARSE",
            format!("failed to parse module: {error}"),
        )
        .with_path(&imported.path));
    }

    let mut exports = BTreeMap::<String, Vec<Route>>::new();
    let mut duplicate_exports = BTreeSet::<String>::new();
    for statement in &parsed.program.body {
        match statement {
            Statement::ImportDeclaration(import) => {
                let import_source = import.source.value.as_str();
                if import_source == "sloppy" {
                    continue;
                }
                if import_source == "sloppy/time" {
                    validate_module_sloppy_time_import(&imported.path, import)?;
                    if import_has_runtime_value_specifier(import) {
                        graph.uses_time_runtime = true;
                    }
                    continue;
                }
                if import_source == "sloppy/crypto" {
                    validate_module_sloppy_crypto_import(&imported.path, import)?;
                    if import_has_runtime_value_specifier(import) {
                        graph.uses_crypto_runtime = true;
                    }
                    continue;
                }
                if import_source == "sloppy/codec" {
                    validate_module_sloppy_codec_import(&imported.path, import)?;
                    if import_has_runtime_value_specifier(import) {
                        graph.uses_codec_runtime = true;
                    }
                    continue;
                }
                if import_source == "sloppy/net" {
                    validate_module_sloppy_net_import(&imported.path, import)?;
                    if let Some(specifiers) = &import.specifiers {
                        for specifier in specifiers {
                            if let ImportDeclarationSpecifier::ImportSpecifier(specifier) =
                                specifier
                            {
                                if import_specifier_is_runtime_value(import, specifier) {
                                    let imported_name = specifier.imported.name().as_str();
                                    mark_sloppy_net_runtime_usage(
                                        &mut graph.uses_net_runtime,
                                        &mut graph.uses_http_client_runtime,
                                        imported_name,
                                    );
                                }
                            }
                        }
                    }
                    continue;
                }
                if import_source == "sloppy/os" {
                    validate_module_sloppy_os_import(&imported.path, import)?;
                    if import_has_runtime_value_specifier(import) {
                        graph.uses_os_runtime = true;
                    }
                    continue;
                }
                if import_source == "sloppy/workers" {
                    validate_module_sloppy_workers_import(&imported.path, import)?;
                    if import_has_runtime_value_specifier(import) {
                        graph.uses_workers_runtime = true;
                    }
                    continue;
                }
                if import_source.starts_with("./") || import_source.starts_with("../") {
                    let nested = resolve_relative_import(&imported.path, import_source)
                        .ok_or_else(|| {
                            Diagnostic::new(
                                "SLOPPYC_E_MISSING_RELATIVE_IMPORT",
                                format!(
                                    "relative import \"{import_source}\" could not be resolved"
                                ),
                            )
                            .with_path(&imported.path)
                            .with_span(import.source.span)
                        })?;
                    if graph.visiting.contains(&nested) {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_CIRCULAR_IMPORT",
                            "circular relative imports are not supported",
                        )
                        .with_path(&imported.path)
                        .with_span(import.source.span));
                    }
                    continue;
                }
                if import_source != "sloppy/providers/sqlite" {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
                        format!("unsupported import specifier \"{import_source}\""),
                    )
                    .with_path(&imported.path)
                    .with_span(import.source.span));
                }
            }
            Statement::ExportNamedDeclaration(export) => {
                let Some(Declaration::FunctionDeclaration(function)) = &export.declaration else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                        "function modules must export a named function declaration",
                    )
                    .with_path(&imported.path)
                    .with_span(export.span));
                };
                let Some(identifier) = &function.id else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                        "function module export must be named",
                    )
                    .with_path(&imported.path)
                    .with_span(function.span));
                };
                let export_name = identifier.name.as_str();
                let routes = extract_module_function_routes(
                    &imported.path,
                    &source,
                    &source_name,
                    export_name,
                    function,
                )?;
                if exports.insert(export_name.to_string(), routes).is_some() {
                    duplicate_exports.insert(export_name.to_string());
                }
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                    "function module files support imports and named function exports",
                )
                .with_path(&imported.path)
                .with_span(statement.span()));
            }
        }
    }

    graph.visiting.remove(&imported.path);
    let module = CachedModule {
        exports,
        duplicate_exports,
    };
    let routes = cached_module_routes(&module, imported)?;
    graph.modules.insert(imported.path.clone(), module);
    Ok(routes)
}

fn cached_module_routes(
    module: &CachedModule,
    imported: &ImportedModule,
) -> Result<Vec<Route>, Diagnostic> {
    if module.duplicate_exports.contains(&imported.export_name) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_DUPLICATE_EXPORT",
            format!("module exports \"{}\" more than once", imported.export_name),
        )
        .with_path(&imported.path)
        .with_span(imported.span));
    }
    let Some(routes) = module.exports.get(&imported.export_name) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_MISSING_EXPORT",
            format!(
                "imported module does not export \"{}\"",
                imported.export_name
            ),
        )
        .with_path(&imported.path)
        .with_span(imported.span));
    };
    Ok(routes.clone())
}

fn extract_module_function_routes(
    path: &Path,
    source: &str,
    source_name: &str,
    module_name: &str,
    function: &oxc_ast::ast::Function<'_>,
) -> Result<Vec<Route>, Diagnostic> {
    if function.params.items.len() != 1 || function.params.rest.is_some() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
            "function modules must declare exactly one app parameter",
        )
        .with_path(path)
        .with_span(function.span));
    }
    let Some(app_name) = function
        .params
        .items
        .first()
        .and_then(|parameter| binding_identifier(&parameter.pattern))
    else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
            "function module app parameter must be a simple identifier",
        )
        .with_path(path)
        .with_span(function.span));
    };
    let Some(body) = &function.body else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
            "function module must have a body",
        )
        .with_path(path)
        .with_span(function.span));
    };

    let mut groups = BTreeMap::<String, String>::new();
    let mut providers = BTreeMap::<String, ProviderBinding>::new();
    let mut routes = Vec::new();

    for statement in &body.statements {
        match statement {
            Statement::VariableDeclaration(declaration) => {
                for declarator in &declaration.declarations {
                    let Some(name) = binding_identifier(&declarator.id) else {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                            "function module declarations must use simple identifiers",
                        )
                        .with_path(path)
                        .with_span(declarator.span));
                    };
                    let Some(init) = &declarator.init else {
                        continue;
                    };
                    if let Some(binding) = app_provider_call(init, app_name) {
                        providers.insert(name.to_string(), binding);
                    } else if let Some((receiver, prefix)) = app_group_call(init) {
                        let full_prefix = if receiver == app_name {
                            prefix.to_string()
                        } else if let Some(parent_prefix) = groups.get(receiver) {
                            join_route_patterns(parent_prefix, prefix)
                        } else {
                            return Err(Diagnostic::new(
                                "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                                "function module route groups must be created from the module app parameter or another module route group",
                            )
                            .with_path(path)
                            .with_span(init.span()));
                        };
                        groups.insert(name.to_string(), full_prefix);
                    }
                }
            }
            Statement::ExpressionStatement(statement) => {
                let (route_expr, name) = match &statement.expression {
                    Expression::CallExpression(call) => match with_name_call(call)? {
                        Some((inner, name)) => (inner, Some(name)),
                        None => (&statement.expression, None),
                    },
                    _ => (&statement.expression, None),
                };
                let Some((receiver, method, pattern, handler_arg)) = route_call_parts(route_expr)
                else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                        "function modules support provider lookup, route groups, and literal routes only",
                    )
                    .with_path(path)
                    .with_span(statement.span));
                };
                let full_pattern = if receiver == app_name {
                    pattern.to_string()
                } else if let Some(prefix) = groups.get(receiver) {
                    join_route_patterns(prefix, pattern)
                } else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                        "module route must be registered on the module app parameter or a group created from it",
                    )
                    .with_path(path)
                    .with_span(statement.span));
                };
                let normalized_pattern = normalize_framework_route_pattern(&full_pattern);
                if !route_pattern_supported(&normalized_pattern) {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_ROUTE_PATTERN",
                        "route pattern is outside the Plan v1 alpha route syntax",
                    )
                    .with_path(path)
                    .with_span(statement.span)
                    .with_hint("Use '/', static segments, {name}, {name:str}, or {name:int}."));
                }

                let schema_names = BTreeSet::new();
                let helper_effects = BTreeMap::new();
                let handler_context = HandlerExtractionContext {
                    route_pattern: &full_pattern,
                    source,
                    source_name,
                    allow_data_handler_body: false,
                    schema_names: &schema_names,
                    provider_bindings: &providers,
                    helper_effects: &helper_effects,
                };
                let Some(mut handler) = handler_from_argument(handler_arg, &handler_context) else {
                    return Err(handler_diagnostic(
                        path,
                        handler_arg,
                        &full_pattern,
                        &schema_names,
                        statement.span,
                    ));
                };

                if !handler.effects.is_empty() {
                    let used_providers = providers_used_by_effects(&providers, &handler.effects);
                    if let Some((name, binding)) = used_providers
                        .iter()
                        .find(|(_, binding)| !provider_has_generated_runtime_bridge(binding))
                    {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE",
                            format!(
                                "{} provider-backed handlers are not executable by the compiler-generated runtime bridge yet",
                                binding.provider
                            ),
                        )
                        .with_path(path)
                        .with_span(statement.span)
                        .with_hint(format!(
                            "Provider handle '{name}' is recognized for Plan metadata, but only the SQLite generated bridge is executable by the current compiler/runtime contract."
                        )));
                    }
                    handler.source = wrap_module_handler_with_providers(
                        &handler.source,
                        &used_providers,
                        handler.is_async,
                    );
                }
                routes.push(Route {
                    method,
                    framework_path: (normalized_pattern != full_pattern).then_some(full_pattern),
                    pattern: normalized_pattern,
                    name,
                    span: statement.span,
                    source_path: path.to_path_buf(),
                    source_name: source_name.to_string(),
                    source: source.to_string(),
                    module: Some(module_name.to_string()),
                    handler,
                });
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                    "unsupported statement in function module",
                )
                .with_path(path)
                .with_span(statement.span()));
            }
        }
    }
    Ok(routes)
}

fn app_provider_call(expression: &Expression<'_>, app_name: &str) -> Option<ProviderBinding> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if receiver != app_name || property != "provider" || call.arguments.len() != 1 {
        return None;
    }
    let token = string_argument(call.arguments.first()?)?;
    database_provider_binding_from_token(token)
}

fn wrap_module_handler_with_providers(
    handler_source: &str,
    providers: &BTreeMap<String, ProviderBinding>,
    is_async: bool,
) -> String {
    wrap_handler_with_providers_and_helpers(handler_source, providers, &[], is_async)
}

fn provider_has_generated_runtime_bridge(binding: &ProviderBinding) -> bool {
    binding.capability_kind == "database" && binding.provider == "sqlite"
}

fn wrap_handler_with_providers_and_helpers(
    handler_source: &str,
    providers: &BTreeMap<String, ProviderBinding>,
    helper_sources: &[String],
    is_async: bool,
) -> String {
    if providers.is_empty() && helper_sources.is_empty() {
        return handler_source.to_string();
    }
    let provider_names = providers
        .keys()
        .map(|name| format!("let {name};"))
        .collect::<Vec<_>>()
        .join(" ");
    let provider_prefix = providers
        .iter()
        .map(|(name, binding)| {
            format!(
                "{name} = __sloppy_open_data_provider({}, {}); __sloppy_opened_providers.push({name});",
                serde_json::to_string(&binding.provider)
                    .unwrap_or_else(|_| "\"sqlite\"".to_string()),
                serde_json::to_string(&binding.token)
                    .unwrap_or_else(|_| "\"data.main\"".to_string())
            )
        })
        .collect::<Vec<_>>()
        .join(" ");
    let helper_prefix = if helper_sources.is_empty() {
        String::new()
    } else {
        format!("{} ", helper_sources.join(" "))
    };
    let close_loop =
        "while (__sloppy_opened_providers.length > 0) { const __sloppy_provider = __sloppy_opened_providers.pop(); try { __sloppy_provider.close(); } catch (_) {} }";
    if is_async {
        return format!(
            "async function(ctx) {{ const __sloppy_opened_providers = []; {provider_names} try {{ {provider_prefix} {helper_prefix}return await ({handler_source})(ctx); }} finally {{ {close_loop} }} }}"
        );
    }

    format!(
        "function(ctx) {{ const __sloppy_opened_providers = []; {provider_names} try {{ {provider_prefix} {helper_prefix}return ({handler_source})(ctx); }} finally {{ {close_loop} }} }}"
    )
}

fn providers_used_by_effects(
    provider_bindings: &BTreeMap<String, ProviderBinding>,
    effects: &[EffectMetadata],
) -> BTreeMap<String, ProviderBinding> {
    let tokens = effects
        .iter()
        .map(|effect| effect.provider.as_str())
        .collect::<BTreeSet<_>>();
    provider_bindings
        .iter()
        .filter(|(_, binding)| tokens.contains(binding.token.as_str()))
        .map(|(name, binding)| (name.clone(), binding.clone()))
        .collect()
}

fn route_call<'a>(
    expression: &'a Expression<'a>,
    source: &str,
    source_name: &str,
    allow_data_handler_body: bool,
    schema_names: &BTreeSet<String>,
    provider_bindings: &BTreeMap<String, ProviderBinding>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
) -> Option<(&'a str, &'static str, &'a str, Handler)> {
    let (receiver, method, pattern, handler_arg) = route_call_parts(expression)?;
    let context = HandlerExtractionContext {
        route_pattern: pattern,
        source,
        source_name,
        allow_data_handler_body,
        schema_names,
        provider_bindings,
        helper_effects,
    };
    let handler = handler_from_argument(handler_arg, &context)?;
    Some((receiver, method, pattern, handler))
}

fn route_call_parts<'a>(
    expression: &'a Expression<'a>,
) -> Option<(&'a str, &'static str, &'a str, &'a Argument<'a>)> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    let method = route_method_from_property(property)?;
    if call.arguments.len() != 2 {
        return None;
    }

    let pattern = string_argument(call.arguments.first()?)?;
    let handler_arg = call.arguments.get(1)?;
    Some((receiver, method, pattern, handler_arg))
}

fn route_method_from_property(property: &str) -> Option<&'static str> {
    crate::slop_dsl::route_method_from_property(property)
}

fn with_name_call<'a>(
    call: &'a CallExpression<'a>,
) -> Result<Option<(&'a Expression<'a>, String)>, Diagnostic> {
    let Expression::StaticMemberExpression(member) = &call.callee else {
        return Ok(None);
    };

    if member.property.name.as_str() != "withName" {
        return Ok(None);
    }

    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_NAME",
            "withName requires exactly one literal name",
        )
        .with_span(call.span));
    }

    let Some(name) = string_argument(call.arguments.first().ok_or_else(|| {
        Diagnostic::new("SLOPPYC_E_UNSUPPORTED_ROUTE_NAME", "missing route name")
            .with_span(call.span)
    })?) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_NAME",
            "route names must be string literals",
        )
        .with_span(call.span));
    };

    Ok(Some((&member.object, name.to_string())))
}

fn static_member_name<'a>(expression: &'a Expression<'a>) -> Option<(&'a str, &'a str)> {
    crate::slop_dsl::static_member_name(expression)
}

fn static_member_chain<'a>(expression: &'a Expression<'a>) -> Option<Vec<&'a str>> {
    crate::slop_dsl::static_member_chain(expression)
}

fn computed_member_receiver<'a>(expression: &'a Expression<'a>) -> Option<&'a str> {
    crate::slop_dsl::computed_member_receiver(expression)
}

fn string_argument<'a>(argument: &'a Argument<'a>) -> Option<&'a str> {
    crate::slop_dsl::string_argument(argument)
}

fn object_argument<'a>(
    argument: &'a Argument<'a>,
) -> Option<&'a oxc_ast::ast::ObjectExpression<'a>> {
    match argument {
        Argument::ObjectExpression(object) => Some(object),
        _ => None,
    }
}

fn property_key_name<'a>(key: &'a PropertyKey<'a>) -> Option<&'a str> {
    match key {
        PropertyKey::StaticIdentifier(identifier) => Some(identifier.name.as_str()),
        PropertyKey::StringLiteral(literal) => Some(literal.value.as_str()),
        _ => None,
    }
}

fn object_string_property_value<'a>(
    object: &'a oxc_ast::ast::ObjectExpression<'a>,
    name: &str,
) -> Option<&'a str> {
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            continue;
        };
        if property.computed || property_key_name(&property.key) != Some(name) {
            continue;
        }
        let Expression::StringLiteral(value) = &property.value else {
            return None;
        };
        return Some(value.value.as_str());
    }
    None
}

fn object_bool_property_value(
    object: &oxc_ast::ast::ObjectExpression<'_>,
    name: &str,
) -> Option<bool> {
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            continue;
        };
        if property.computed || property_key_name(&property.key) != Some(name) {
            continue;
        }
        let Expression::BooleanLiteral(value) = &property.value else {
            return None;
        };
        return Some(value.value);
    }
    None
}

fn object_json_property_value(
    object: &oxc_ast::ast::ObjectExpression<'_>,
    name: &str,
) -> Option<Value> {
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            continue;
        };
        if property.computed || property_key_name(&property.key) != Some(name) {
            continue;
        }
        return expression_json_value(&property.value);
    }
    None
}

fn argument_json_value(argument: &Argument<'_>) -> Option<Value> {
    match argument {
        Argument::StringLiteral(value) => Some(json!(value.value.as_str())),
        Argument::NumericLiteral(value) => Some(json!(value.value)),
        Argument::BooleanLiteral(value) => Some(json!(value.value)),
        Argument::NullLiteral(_) => Some(Value::Null),
        Argument::ArrayExpression(array) => array_json_value(array),
        Argument::ObjectExpression(object) => object_json_value(object),
        _ => None,
    }
}

fn expression_json_value(expression: &Expression<'_>) -> Option<Value> {
    match expression {
        Expression::StringLiteral(value) => Some(json!(value.value.as_str())),
        Expression::NumericLiteral(value) => Some(json!(value.value)),
        Expression::BooleanLiteral(value) => Some(json!(value.value)),
        Expression::NullLiteral(_) => Some(Value::Null),
        Expression::ArrayExpression(array) => array_json_value(array),
        Expression::ObjectExpression(object) => object_json_value(object),
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_json_value(&parenthesized.expression)
        }
        _ => None,
    }
}

fn array_json_value(array: &oxc_ast::ast::ArrayExpression<'_>) -> Option<Value> {
    let mut values = Vec::new();
    for element in &array.elements {
        let value = match element {
            ArrayExpressionElement::StringLiteral(value) => json!(value.value.as_str()),
            ArrayExpressionElement::NumericLiteral(value) => json!(value.value),
            ArrayExpressionElement::BooleanLiteral(value) => json!(value.value),
            ArrayExpressionElement::NullLiteral(_) => Value::Null,
            ArrayExpressionElement::ArrayExpression(array) => array_json_value(array)?,
            ArrayExpressionElement::ObjectExpression(object) => object_json_value(object)?,
            _ => return None,
        };
        values.push(value);
    }
    Some(Value::Array(values))
}

fn object_json_value(object: &oxc_ast::ast::ObjectExpression<'_>) -> Option<Value> {
    let mut values = serde_json::Map::new();
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return None;
        };
        if property.computed {
            return None;
        }
        let key = property_key_name(&property.key)?.to_string();
        values.insert(key, expression_json_value(&property.value)?);
    }
    Some(Value::Object(values))
}

fn plan_token_supported(token: &str) -> bool {
    !token.is_empty()
        && token
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'_' | b'-'))
}

fn reject_secret_option_fields(
    path: &Path,
    object: &oxc_ast::ast::ObjectExpression<'_>,
) -> Result<(), Diagnostic> {
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            continue;
        };
        let Some(name) = property_key_name(&property.key) else {
            continue;
        };
        let normalized = name
            .chars()
            .filter(|character| character.is_ascii_alphanumeric())
            .map(|character| character.to_ascii_lowercase())
            .collect::<String>();
        if matches!(
            normalized.as_str(),
            "connectionstring" | "password" | "pwd" | "secret" | "apikey" | "accesstoken"
        ) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_SECRET_PLAN_METADATA",
                "provider and capability metadata must not contain secret-bearing fields",
            )
            .with_path(path)
            .with_span(property.span)
            .with_hint(
                "Reference config keys in future provider metadata instead of embedding secrets.",
            ));
        }
    }
    Ok(())
}

fn optional_object_string_property<'a>(
    path: &Path,
    object: &'a oxc_ast::ast::ObjectExpression<'a>,
    property_name: &str,
) -> Result<Option<&'a str>, Diagnostic> {
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE",
                "database capability options do not support spread properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        if property.kind != PropertyKind::Init || property.method || property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE",
                "database capability options must use simple literal properties",
            )
            .with_path(path)
            .with_span(property.span));
        }
        if property_key_name(&property.key) != Some(property_name) {
            continue;
        }
        let Expression::StringLiteral(value) = &property.value else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE",
                format!("database capability option '{property_name}' must be a string literal"),
            )
            .with_path(path)
            .with_span(property.span));
        };
        return Ok(Some(value.value.as_str()));
    }
    Ok(None)
}

fn required_object_string_property<'a>(
    path: &Path,
    object: &'a oxc_ast::ast::ObjectExpression<'a>,
    property_name: &str,
) -> Result<&'a str, Diagnostic> {
    optional_object_string_property(path, object, property_name)?.ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE",
            format!("database capability options must include '{property_name}'"),
        )
        .with_path(path)
        .with_span(object.span)
    })
}

fn route_pattern_supported(pattern: &str) -> bool {
    if pattern == "/" {
        return true;
    }
    if !pattern.starts_with('/') || pattern.ends_with('/') || pattern.contains("//") {
        return false;
    }
    pattern.split('/').skip(1).all(route_segment_supported)
}

fn normalize_framework_route_pattern(pattern: &str) -> String {
    if !pattern.contains("/:") {
        return pattern.to_string();
    }
    let mut normalized = Vec::new();
    for segment in pattern.split('/') {
        if let Some(name) = segment.strip_prefix(':') {
            normalized.push(format!("{{{name}}}"));
        } else {
            normalized.push(segment.to_string());
        }
    }
    normalized.join("/")
}

fn route_segment_supported(segment: &str) -> bool {
    if segment.is_empty() {
        return false;
    }
    if !(segment.starts_with('{') || segment.ends_with('}')) {
        return !segment.contains('{') && !segment.contains('}');
    }
    if !segment.starts_with('{') || !segment.ends_with('}') {
        return false;
    }
    let inner = &segment[1..segment.len() - 1];
    if inner.contains('{') || inner.contains('}') {
        return false;
    }
    let (name, kind) = inner.split_once(':').unwrap_or((inner, "str"));
    !name.is_empty()
        && name
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || byte == b'_')
        && matches!(kind, "str" | "int")
}

fn handler_from_argument(
    argument: &Argument<'_>,
    context: &HandlerExtractionContext<'_>,
) -> Option<Handler> {
    match argument {
        Argument::ArrowFunctionExpression(function) => {
            if typed_framework_handler_parameters(&function.params) {
                return typed_framework_handler_from_arrow(
                    function,
                    context.route_pattern,
                    context.source,
                    context.source_name,
                    context.schema_names,
                );
            }
            let effects = function_effects_from_arrow(
                function,
                context.provider_bindings,
                context.helper_effects,
                context.source,
                context.source_name,
            );
            if handler_parameters_are_unsupported(&function.params)
                || arrow_has_typescript_syntax(function)
                || effects.unknown_provider_usage
                || (!context.allow_data_handler_body
                    && effects.effects.is_empty()
                    && !handler_body_is_supported_arrow(function, context.schema_names))
            {
                return None;
            }
            let handler_source = source_slice(context.source, function.span)?;
            let schema_spans = handler_context_parameter_name(&function.params)
                .map(|ctx_name| {
                    body_json_schema_argument_spans_arrow(function, &ctx_name, context.schema_names)
                })
                .unwrap_or_default();
            Some(Handler {
                source: sanitize_handler_schema_references(
                    handler_source,
                    function.span.start,
                    &schema_spans,
                ),
                span: function.span,
                is_async: function.r#async,
                runtime_deferred: false,
                source_name: context.source_name.to_string(),
                source_text: context.source.to_string(),
                bindings: request_bindings_from_arrow(function, context.schema_names),
                response: response_metadata_from_arrow(function),
                responses: response_metadata_from_arrow(function).into_iter().collect(),
                effects: effects.effects,
            })
        }
        Argument::FunctionExpression(function) => {
            if typed_framework_handler_parameters(&function.params) {
                return typed_framework_handler_from_function(
                    function,
                    context.route_pattern,
                    context.source,
                    context.source_name,
                    context.schema_names,
                );
            }
            let effects = function_effects_from_function(
                function,
                context.provider_bindings,
                context.helper_effects,
                context.source,
                context.source_name,
            );
            if handler_parameters_are_unsupported(&function.params)
                || function_has_typescript_syntax(function)
                || effects.unknown_provider_usage
                || (!context.allow_data_handler_body
                    && effects.effects.is_empty()
                    && !handler_body_is_supported_function(function, context.schema_names))
            {
                return None;
            }
            let handler_source = source_slice(context.source, function.span)?;
            let schema_spans = handler_context_parameter_name(&function.params)
                .map(|ctx_name| {
                    body_json_schema_argument_spans_function(
                        function,
                        &ctx_name,
                        context.schema_names,
                    )
                })
                .unwrap_or_default();
            Some(Handler {
                source: sanitize_handler_schema_references(
                    handler_source,
                    function.span.start,
                    &schema_spans,
                ),
                span: function.span,
                is_async: function.r#async,
                runtime_deferred: false,
                source_name: context.source_name.to_string(),
                source_text: context.source.to_string(),
                bindings: request_bindings_from_function(function, context.schema_names),
                response: response_metadata_from_function(function),
                responses: response_metadata_from_function(function)
                    .into_iter()
                    .collect(),
                effects: effects.effects,
            })
        }
        _ => None,
    }
}

fn typed_framework_handler_parameters(parameters: &oxc_ast::ast::FormalParameters<'_>) -> bool {
    parameters
        .items
        .iter()
        .any(|parameter| parameter.type_annotation.is_some())
}

fn typed_framework_handler_from_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    route_pattern: &str,
    source: &str,
    source_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<Handler> {
    let bindings =
        typed_framework_bindings_from_parameters(&function.params, route_pattern, schema_names)?;
    let responses = response_metadata_many_from_arrow(function, source_name, source, schema_names);
    let handler_source =
        crate::framework_runtime::typed_arrow_handler_source(function, source, &bindings)?;
    Some(Handler {
        source: handler_source,
        span: function.span,
        is_async: function.r#async,
        runtime_deferred: false,
        source_name: source_name.to_string(),
        source_text: source.to_string(),
        bindings,
        response: responses.first().cloned(),
        responses,
        effects: Vec::new(),
    })
}

fn typed_framework_handler_from_function(
    function: &oxc_ast::ast::Function<'_>,
    route_pattern: &str,
    source: &str,
    source_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<Handler> {
    let bindings =
        typed_framework_bindings_from_parameters(&function.params, route_pattern, schema_names)?;
    let responses =
        response_metadata_many_from_function(function, source_name, source, schema_names);
    let handler_source =
        crate::framework_runtime::typed_function_handler_source(function, source, &bindings)?;
    Some(Handler {
        source: handler_source,
        span: function.span,
        is_async: function.r#async,
        runtime_deferred: false,
        source_name: source_name.to_string(),
        source_text: source.to_string(),
        bindings,
        response: responses.first().cloned(),
        responses,
        effects: Vec::new(),
    })
}

fn typed_framework_bindings_from_parameters(
    parameters: &oxc_ast::ast::FormalParameters<'_>,
    route_pattern: &str,
    schema_names: &BTreeSet<String>,
) -> Option<Vec<RequestBinding>> {
    if parameters.rest.is_some() {
        return None;
    }
    let route_names = route_parameter_names(route_pattern);
    let mut bound_route_names = BTreeSet::new();
    let mut implicit_body_parameter = None::<String>;
    let mut bindings = Vec::new();
    for parameter in &parameters.items {
        if parameter.initializer.is_some()
            || parameter.optional
            || parameter.accessibility.is_some()
            || parameter.readonly
            || parameter.r#override
        {
            return None;
        }
        let BindingPattern::BindingIdentifier(identifier) = &parameter.pattern else {
            return None;
        };
        let parameter_name = identifier.name.as_str();
        let annotation = parameter.type_annotation.as_ref()?;
        let type_name = type_display_name(&annotation.type_annotation);
        let span = parameter.span;
        let binding = binding_from_typed_parameter(
            parameter_name,
            &annotation.type_annotation,
            &route_names,
            &mut bound_route_names,
            &mut implicit_body_parameter,
            schema_names,
            span,
        )?;
        let mut binding = binding;
        binding.type_name = Some(type_name);
        bindings.push(binding);
    }
    if route_names
        .iter()
        .any(|name| !bound_route_names.contains(name))
    {
        return None;
    }
    Some(dedupe_request_bindings(bindings))
}

fn binding_from_typed_parameter(
    parameter_name: &str,
    ty: &TSType<'_>,
    route_names: &BTreeSet<String>,
    bound_route_names: &mut BTreeSet<String>,
    body_parameter: &mut Option<String>,
    schema_names: &BTreeSet<String>,
    span: Span,
) -> Option<RequestBinding> {
    if let Some(wrapper) = wrapper_type_reference(ty) {
        if matches!(wrapper.0, "Body" | "Query" | "Route" | "Header")
            && unresolved_body_type(wrapper.1, schema_names)
        {
            return None;
        }
        if wrapper.0 == "Route" && !route_names.contains(parameter_name) {
            return None;
        }
        if wrapper.0 == "Route" {
            bound_route_names.insert(parameter_name.to_string());
        }
        if wrapper.0 == "Body" {
            if body_parameter
                .as_ref()
                .is_some_and(|existing| existing != parameter_name)
            {
                return None;
            }
            *body_parameter = Some(parameter_name.to_string());
        }
        return explicit_wrapper_binding(parameter_name, wrapper, schema_names, span);
    }
    if let Some(context_kind) = framework_context_type(ty) {
        return Some(framework_binding(
            "context",
            Some(context_kind),
            None,
            Some(parameter_name),
            span,
        ));
    }
    if let Some((provider_kind, name)) = provider_type_reference(ty) {
        let capability = provider_capability_token(provider_kind, &name);
        let mut binding =
            framework_binding("injection", Some(&name), None, Some(parameter_name), span);
        binding.injection_kind = Some("provider".to_string());
        binding.provider_kind = Some(provider_kind.to_string());
        binding.capability = Some(capability);
        return Some(binding);
    }
    if let Some(name) = work_queue_type_reference(ty) {
        let mut binding =
            framework_binding("injection", Some(&name), None, Some(parameter_name), span);
        binding.injection_kind = Some("queue".to_string());
        binding.provider_kind = Some("workqueue".to_string());
        binding.capability = Some(format!("queue.{name}"));
        return Some(binding);
    }
    if route_names.contains(parameter_name) && primitive_type_schema(ty).is_some() {
        bound_route_names.insert(parameter_name.to_string());
        let schema = primitive_type_schema(ty);
        let mut binding = framework_binding(
            "route",
            Some(parameter_name),
            schema.as_deref(),
            Some(parameter_name),
            span,
        );
        binding.semantic = semantic_name_from_type(ty).map(ToOwned::to_owned);
        return Some(binding);
    }
    if primitive_type_schema(ty).is_some() {
        return None;
    }
    let schema = schema_name_from_type(ty, schema_names)?;
    if body_parameter
        .as_ref()
        .is_some_and(|existing| existing != parameter_name)
    {
        return None;
    }
    *body_parameter = Some(parameter_name.to_string());
    Some(framework_binding(
        "body.json",
        None,
        Some(&schema),
        Some(parameter_name),
        span,
    ))
}

fn explicit_wrapper_binding(
    parameter_name: &str,
    wrapper: (&str, &TSType<'_>, Option<String>),
    schema_names: &BTreeSet<String>,
    span: Span,
) -> Option<RequestBinding> {
    let (wrapper_name, target_type, key) = wrapper;
    let schema = match wrapper_name {
        "Body" => schema_name_from_type(target_type, schema_names),
        _ => primitive_type_schema(target_type)
            .or_else(|| schema_name_from_type(target_type, schema_names))
            .or_else(|| semantic_name_from_type(target_type).map(ToOwned::to_owned)),
    };
    let kind = match wrapper_name {
        "Route" => "route",
        "Query" => "query",
        "Body" => "body.json",
        "Header" => "header",
        "Service" => "injection",
        "Config" => "config",
        _ => return None,
    };
    let name = if wrapper_name == "Service" {
        Some(type_display_name(target_type))
    } else {
        key.as_deref()
            .or(Some(parameter_name))
            .map(ToOwned::to_owned)
    };
    let mut binding = framework_binding(
        kind,
        name.as_deref(),
        schema.as_deref(),
        Some(parameter_name),
        span,
    );
    binding.wrapper = Some(wrapper_name.to_string());
    if wrapper_name == "Service" {
        binding.injection_kind = Some("service".to_string());
    }
    binding.semantic = semantic_name_from_type(target_type).map(ToOwned::to_owned);
    binding.redacted = semantic_name_from_type(target_type)
        .is_some_and(|name| matches!(name, "PasswordString" | "SecretString"));
    Some(binding)
}

fn framework_binding(
    kind: &str,
    name: Option<&str>,
    schema: Option<&str>,
    parameter: Option<&str>,
    span: Span,
) -> RequestBinding {
    RequestBinding {
        kind: kind.to_string(),
        name: name.map(ToOwned::to_owned),
        schema: schema.map(ToOwned::to_owned),
        parameter: parameter.map(ToOwned::to_owned),
        type_name: None,
        source_name: None,
        source_text: None,
        span: Some(span),
        wrapper: None,
        injection_kind: None,
        provider_kind: None,
        capability: None,
        semantic: None,
        redacted: false,
    }
}

fn route_parameter_names(pattern: &str) -> BTreeSet<String> {
    let mut names = BTreeSet::new();
    for segment in pattern.split('/').skip(1) {
        if let Some(name) = segment.strip_prefix(':') {
            let name = name.split([':', '.', '?']).next().unwrap_or(name);
            if !name.is_empty() {
                names.insert(name.to_string());
            }
        } else if segment.starts_with('{') && segment.ends_with('}') {
            let inner = &segment[1..segment.len() - 1];
            let name = inner.split_once(':').map(|(name, _)| name).unwrap_or(inner);
            if !name.is_empty() {
                names.insert(name.to_string());
            }
        }
    }
    names
}

fn primitive_type_schema(ty: &TSType<'_>) -> Option<String> {
    match ty {
        TSType::TSStringKeyword(_) => Some("string".to_string()),
        TSType::TSNumberKeyword(_) => Some("number".to_string()),
        TSType::TSBooleanKeyword(_) => Some("bool".to_string()),
        TSType::TSTypeReference(reference) => {
            let name = typescript_type_name(&reference.type_name)?;
            match name {
                "Email" | "NonEmptyString" | "PasswordString" | "SecretString" | "Uuid"
                | "DateTime" | "Instant" => Some("string".to_string()),
                "PositiveInt" => Some("int".to_string()),
                _ => None,
            }
        }
        _ => None,
    }
}

fn schema_name_from_type(ty: &TSType<'_>, schema_names: &BTreeSet<String>) -> Option<String> {
    match ty {
        TSType::TSTypeReference(reference) => {
            let name = typescript_type_name(&reference.type_name)?;
            if schema_names.contains(name) {
                Some(name.to_string())
            } else {
                None
            }
        }
        TSType::TSTypeLiteral(_) => Some("<inline>".to_string()),
        _ => None,
    }
}

fn semantic_name_from_type(ty: &TSType<'_>) -> Option<&'static str> {
    let TSType::TSTypeReference(reference) = ty else {
        return None;
    };
    match typescript_type_name(&reference.type_name)? {
        "Email" => Some("Email"),
        "NonEmptyString" => Some("NonEmptyString"),
        "PasswordString" => Some("PasswordString"),
        "SecretString" => Some("SecretString"),
        "Uuid" => Some("Uuid"),
        "PositiveInt" => Some("PositiveInt"),
        "DateTime" => Some("DateTime"),
        "Instant" => Some("Instant"),
        _ => None,
    }
}

fn wrapper_type_reference<'a>(
    ty: &'a TSType<'a>,
) -> Option<(&'a str, &'a TSType<'a>, Option<String>)> {
    let TSType::TSTypeReference(reference) = ty else {
        return None;
    };
    let wrapper = typescript_type_name(&reference.type_name)?;
    if !matches!(
        wrapper,
        "Route" | "Query" | "Body" | "Header" | "Service" | "Config"
    ) {
        return None;
    }
    let arguments = reference.type_arguments.as_ref()?;
    if wrapper == "Header" {
        let key = arguments
            .params
            .first()
            .and_then(type_string_literal_value)?;
        let target = arguments.params.get(1).unwrap_or(arguments.params.first()?);
        return Some((wrapper, target, Some(key)));
    }
    let target = arguments.params.first()?;
    Some((wrapper, target, None))
}

fn framework_context_type(ty: &TSType<'_>) -> Option<&'static str> {
    let TSType::TSTypeReference(reference) = ty else {
        return None;
    };
    match typescript_type_name(&reference.type_name)? {
        "RequestContext" => Some("RequestContext"),
        "SlopRequest" => Some("SlopRequest"),
        "SlopResponse" => Some("SlopResponse"),
        "CancellationSignal" => Some("CancellationSignal"),
        "Deadline" => Some("Deadline"),
        _ => None,
    }
}

fn provider_type_reference(ty: &TSType<'_>) -> Option<(&'static str, String)> {
    let TSType::TSTypeReference(reference) = ty else {
        return None;
    };
    let provider_kind = match typescript_type_name(&reference.type_name)? {
        "Postgres" => "postgres",
        "Sqlite" => "sqlite",
        "SqlServer" => "sqlserver",
        _ => return None,
    };
    let name = reference
        .type_arguments
        .as_ref()
        .and_then(|arguments| arguments.params.first())
        .and_then(type_string_literal_value)?;
    Some((provider_kind, name))
}

fn work_queue_type_reference(ty: &TSType<'_>) -> Option<String> {
    let TSType::TSTypeReference(reference) = ty else {
        return None;
    };
    if typescript_type_name(&reference.type_name)? != "WorkQueue" {
        return None;
    }
    reference
        .type_arguments
        .as_ref()
        .and_then(|arguments| arguments.params.first())
        .and_then(type_string_literal_value)
}

fn provider_capability_token(_provider_kind: &str, name: &str) -> String {
    normalize_database_provider_token(name)
}

fn type_string_literal_value(ty: &TSType<'_>) -> Option<String> {
    let TSType::TSLiteralType(literal) = ty else {
        return None;
    };
    let TSLiteral::StringLiteral(value) = &literal.literal else {
        return None;
    };
    Some(value.value.as_str().to_string())
}

fn type_display_name(ty: &TSType<'_>) -> String {
    match ty {
        TSType::TSStringKeyword(_) => "string".to_string(),
        TSType::TSNumberKeyword(_) => "number".to_string(),
        TSType::TSBooleanKeyword(_) => "boolean".to_string(),
        TSType::TSNullKeyword(_) => "null".to_string(),
        TSType::TSArrayType(array) => format!("{}[]", type_display_name(&array.element_type)),
        TSType::TSTypeReference(reference) => {
            let name = typescript_type_name(&reference.type_name).unwrap_or("<qualified>");
            if let Some(arguments) = &reference.type_arguments {
                let values = arguments
                    .params
                    .iter()
                    .map(type_display_name)
                    .collect::<Vec<_>>();
                format!("{name}<{}>", values.join(", "))
            } else {
                name.to_string()
            }
        }
        TSType::TSLiteralType(literal) => match &literal.literal {
            TSLiteral::StringLiteral(value) => format!("\"{}\"", value.value.as_str()),
            TSLiteral::NumericLiteral(value) => value.value.to_string(),
            TSLiteral::BooleanLiteral(value) => value.value.to_string(),
            _ => "<literal>".to_string(),
        },
        _ => "<unsupported>".to_string(),
    }
}

fn framework_binding_diagnostic(
    path: &Path,
    argument: &Argument<'_>,
    route_pattern: &str,
    schema_names: &BTreeSet<String>,
) -> Option<Diagnostic> {
    let parameters = match argument {
        Argument::ArrowFunctionExpression(function)
            if typed_framework_handler_parameters(&function.params) =>
        {
            &function.params
        }
        Argument::FunctionExpression(function)
            if typed_framework_handler_parameters(&function.params) =>
        {
            &function.params
        }
        _ => return None,
    };
    let route_names = route_parameter_names(route_pattern);
    if parameters.rest.is_some() {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS",
                "Framework v2 route handlers do not support rest parameters",
            )
            .with_path(path)
            .with_span(parameters.span),
        );
    }
    let mut body_parameter = None::<String>;
    let mut bound_route_names = BTreeSet::new();
    for parameter in &parameters.items {
        let BindingPattern::BindingIdentifier(identifier) = &parameter.pattern else {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS",
                    "Framework v2 route handler parameters must be simple identifiers",
                )
                .with_path(path)
                .with_span(parameter.span),
            );
        };
        let parameter_name = identifier.name.as_str();
        let Some(annotation) = &parameter.type_annotation else {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS",
                    "Framework v2 route handler parameters must have TypeScript type annotations",
                )
                .with_path(path)
                .with_span(parameter.span),
            );
        };
        let ty = &annotation.type_annotation;
        if malformed_header_wrapper(ty) {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEADER_BINDING",
                    "Header<T> binding requires a string literal header name",
                )
                .with_path(path)
                .with_span(parameter.span)
                .with_hint("Use Header<\"x-request-id\"> or Header<\"x-request-id\", string>."),
            );
        }
        if let Some(provider_kind) = provider_type_reference_without_literal_name(ty) {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_DYNAMIC_PROVIDER_NAME",
                    format!("{provider_kind} provider injection requires a string literal name"),
                )
                .with_path(path)
                .with_span(parameter.span)
                .with_hint("Use Postgres<\"main\">, Sqlite<\"main\">, or SqlServer<\"main\">."),
            );
        }
        if let Some(marker) = unknown_generic_injection_marker(ty) {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNKNOWN_INJECTION_MARKER",
                    format!("unknown provider/service/queue marker '{marker}'"),
                )
                .with_path(path)
                .with_span(parameter.span)
                .with_hint(
                    "Use supported provider markers or wrap ordinary body models with Body<T>.",
                ),
            );
        }
        if let Some(wrapper) = wrapper_type_reference(ty) {
            if wrapper.0 == "Header" && wrapper.2.is_none() {
                return Some(
                    Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEADER_BINDING",
                        "Header<T> binding requires a string literal header name",
                    )
                    .with_path(path)
                    .with_span(parameter.span)
                    .with_hint("Use Header<\"x-request-id\"> or Header<\"x-request-id\", string>."),
                );
            }
            if matches!(wrapper.0, "Body" | "Query" | "Route" | "Header")
                && unresolved_body_type(wrapper.1, schema_names)
            {
                return Some(
                    Diagnostic::new(
                        "SLOPPYC_E_UNRESOLVED_TYPE",
                        format!(
                            "handler parameter '{parameter_name}' references an unresolved binding type"
                        ),
                    )
                    .with_path(path)
                    .with_span(parameter.span)
                    .with_hint("Declare a concrete type alias or interface in the same source file."),
                );
            }
            if wrapper.0 == "Route" {
                if !route_names.contains(parameter_name) {
                    return Some(
                        Diagnostic::new(
                            "SLOPPYC_E_ROUTE_BINDING_MISMATCH",
                            format!(
                                "explicit Route<T> parameter '{parameter_name}' does not match any route segment"
                            ),
                        )
                        .with_path(path)
                        .with_span(parameter.span)
                        .with_hint("Rename the parameter to match a route segment or use Query<T>, Header<...>, or Body<T>."),
                    );
                }
                bound_route_names.insert(parameter_name.to_string());
            }
            if wrapper.0 == "Body" {
                if body_parameter
                    .as_ref()
                    .is_some_and(|existing| existing != parameter_name)
                {
                    return Some(
                        Diagnostic::new(
                            "SLOPPYC_E_MULTIPLE_BODY_BINDINGS",
                            "route handlers may have only one body parameter",
                        )
                        .with_path(path)
                        .with_span(parameter.span)
                        .with_hint(
                            "Use one Body<T> wrapper or combine the body shape into one model.",
                        ),
                    );
                }
                body_parameter = Some(parameter_name.to_string());
            }
            continue;
        }
        if framework_context_type(ty).is_some()
            || provider_type_reference(ty).is_some()
            || work_queue_type_reference(ty).is_some()
        {
            continue;
        }
        if route_names.contains(parameter_name) && primitive_type_schema(ty).is_some() {
            bound_route_names.insert(parameter_name.to_string());
            continue;
        }
        if primitive_type_schema(ty).is_some() {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_AMBIGUOUS_BINDING",
                    format!("primitive handler parameter '{parameter_name}' has no binding source"),
                )
                .with_path(path)
                .with_span(parameter.span)
                .with_hint(
                    "Use Query<T>, Route<T>, Header<...>, or Body<T> to make the binding explicit.",
                ),
            );
        }
        if unresolved_body_type(ty, schema_names) {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNRESOLVED_TYPE",
                    format!(
                        "handler parameter '{parameter_name}' references an unresolved body type"
                    ),
                )
                .with_path(path)
                .with_span(parameter.span)
                .with_hint("Declare a concrete type alias or interface in the same source file."),
            );
        }
        if body_parameter
            .as_ref()
            .is_some_and(|existing| existing != parameter_name)
        {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_MULTIPLE_BODY_BINDINGS",
                    "route handlers may have only one body parameter",
                )
                .with_path(path)
                .with_span(parameter.span)
                .with_hint(
                    "Use explicit Body<T> wrappers or combine the body shape into one model.",
                ),
            );
        }
        body_parameter = Some(parameter_name.to_string());
    }
    if let Some(unbound) = route_names
        .iter()
        .find(|name| !bound_route_names.contains(*name))
    {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNBOUND_ROUTE_PARAMETER",
                format!("route parameter '{unbound}' is not bound by the handler signature"),
            )
            .with_path(path)
            .with_span(parameters.span)
            .with_hint("Add a matching parameter name or an explicit Route<T> binding."),
        );
    }
    None
}

fn malformed_header_wrapper(ty: &TSType<'_>) -> bool {
    let TSType::TSTypeReference(reference) = ty else {
        return false;
    };
    typescript_type_name(&reference.type_name) == Some("Header")
        && reference
            .type_arguments
            .as_ref()
            .and_then(|arguments| arguments.params.first())
            .and_then(type_string_literal_value)
            .is_none()
}

fn provider_type_reference_without_literal_name(ty: &TSType<'_>) -> Option<&'static str> {
    let TSType::TSTypeReference(reference) = ty else {
        return None;
    };
    let provider_kind = match typescript_type_name(&reference.type_name)? {
        "Postgres" => "postgres",
        "Sqlite" => "sqlite",
        "SqlServer" => "sqlserver",
        _ => return None,
    };
    if reference
        .type_arguments
        .as_ref()
        .and_then(|arguments| arguments.params.first())
        .and_then(type_string_literal_value)
        .is_some()
    {
        None
    } else {
        Some(provider_kind)
    }
}

fn unknown_generic_injection_marker(ty: &TSType<'_>) -> Option<String> {
    let TSType::TSTypeReference(reference) = ty else {
        return None;
    };
    let name = typescript_type_name(&reference.type_name)?;
    let Some(arguments) = &reference.type_arguments else {
        return None;
    };
    if matches!(
        name,
        "Route"
            | "Query"
            | "Body"
            | "Header"
            | "Service"
            | "Config"
            | "Postgres"
            | "Sqlite"
            | "SqlServer"
            | "WorkQueue"
            | "PasswordString"
    ) {
        return None;
    }
    arguments
        .params
        .first()
        .and_then(type_string_literal_value)?;
    Some(name.to_string())
}

fn unresolved_body_type(ty: &TSType<'_>, schema_names: &BTreeSet<String>) -> bool {
    let TSType::TSTypeReference(reference) = ty else {
        return false;
    };
    let Some(name) = typescript_type_name(&reference.type_name) else {
        return false;
    };
    if schema_names.contains(name) {
        return false;
    }
    !matches!(
        name,
        "Email"
            | "NonEmptyString"
            | "PasswordString"
            | "SecretString"
            | "Uuid"
            | "PositiveInt"
            | "DateTime"
            | "Instant"
            | "Postgres"
            | "Sqlite"
            | "SqlServer"
            | "WorkQueue"
            | "RequestContext"
            | "SlopRequest"
            | "SlopResponse"
            | "CancellationSignal"
            | "Deadline"
            | "Route"
            | "Query"
            | "Body"
            | "Header"
            | "Service"
            | "Config"
    )
}

fn handler_diagnostic(
    path: &Path,
    argument: &Argument<'_>,
    route_pattern: &str,
    schema_names: &BTreeSet<String>,
    fallback_span: Span,
) -> Diagnostic {
    if let Some(diagnostic) =
        framework_binding_diagnostic(path, argument, route_pattern, schema_names)
    {
        return diagnostic;
    }
    let (code, message, hint) = match argument {
        Argument::ArrowFunctionExpression(function) => {
            if handler_parameters_are_unsupported(&function.params) {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS",
                    "compiled route handlers may declare zero parameters or one simple context parameter",
                    Some("The current runtime passes one plain request context object."),
                )
            } else if arrow_has_typescript_syntax(function) {
                (
                    "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_HANDLER",
                    "TypeScript handler syntax is not supported by the supported app compiler",
                    Some("Use JavaScript handler syntax until TypeScript lowering is implemented."),
                )
            } else if handler_result_uses_unsupported_values_arrow(function, schema_names) {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE",
                    "route handler result arguments must be inline JSON-safe values",
                    Some("Use literals, arrays, and object literals instead of closed-over bindings."),
                )
            } else if function.r#async {
                (
                    "SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY",
                    "async handler extraction currently supports one direct Results.* return",
                    Some("Keep async handlers to direct Results.* returns for the current Promise settlement contract."),
                )
            } else {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER",
                    "route handler must be a simple function returning a supported Results.* descriptor",
                    None,
                )
            }
        }
        Argument::FunctionExpression(function) => {
            if handler_parameters_are_unsupported(&function.params) {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS",
                    "compiled route handlers may declare zero parameters or one simple context parameter",
                    Some("The current runtime passes one plain request context object."),
                )
            } else if function_has_typescript_syntax(function) {
                (
                    "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_HANDLER",
                    "TypeScript handler syntax is not supported by the supported app compiler",
                    Some("Use JavaScript handler syntax until TypeScript lowering is implemented."),
                )
            } else if handler_result_uses_unsupported_values_function(function, schema_names) {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE",
                    "route handler result arguments must be inline JSON-safe values",
                    Some("Use literals, arrays, and object literals instead of closed-over bindings."),
                )
            } else if function.r#async {
                (
                    "SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY",
                    "async handler extraction currently supports one direct Results.* return",
                    Some("Keep async handlers to direct Results.* returns for the current Promise settlement contract."),
                )
            } else {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER",
                    "route handler must be a simple function returning a supported Results.* descriptor",
                    None,
                )
            }
        }
        _ => (
            "SLOPPYC_E_UNSUPPORTED_HANDLER",
            "route handler must be an inline function or arrow expression",
            None,
        ),
    };

    let mut diagnostic = Diagnostic::new(code, message)
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(fallback_span));
    if let Some(hint) = hint {
        diagnostic = diagnostic.with_hint(hint);
    }
    diagnostic
}

fn handler_parameters_are_unsupported(parameters: &oxc_ast::ast::FormalParameters<'_>) -> bool {
    if parameters.items.len() > 1 || parameters.rest.is_some() {
        return true;
    }

    let Some(parameter) = parameters.items.first() else {
        return false;
    };

    parameter.initializer.is_some()
        || !matches!(parameter.pattern, BindingPattern::BindingIdentifier(_))
}

fn handler_context_parameter_name(
    parameters: &oxc_ast::ast::FormalParameters<'_>,
) -> Option<String> {
    let parameter = parameters.items.first()?;
    let BindingPattern::BindingIdentifier(identifier) = &parameter.pattern else {
        return None;
    };
    Some(identifier.name.as_str().to_string())
}

fn helper_effects_from_initializer(
    expression: &Expression<'_>,
    provider_bindings: &BTreeMap<String, ProviderBinding>,
    source: &str,
    source_name: &str,
) -> FunctionEffectSummary {
    match expression {
        Expression::ArrowFunctionExpression(function) => function_effects_from_arrow(
            function,
            provider_bindings,
            &BTreeMap::new(),
            source,
            source_name,
        ),
        Expression::FunctionExpression(function) => function_effects_from_function(
            function,
            provider_bindings,
            &BTreeMap::new(),
            source,
            source_name,
        ),
        _ => FunctionEffectSummary::default(),
    }
}

fn function_effects_from_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    provider_bindings: &BTreeMap<String, ProviderBinding>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    source: &str,
    source_name: &str,
) -> FunctionEffectSummary {
    let mut summary = FunctionEffectSummary {
        provider_bindings: provider_bindings.clone(),
        source_name: source_name.to_string(),
        source_text: source.to_string(),
        ..FunctionEffectSummary::default()
    };
    for statement in &function.body.statements {
        collect_statement_effects(statement, helper_effects, &mut summary);
    }
    dedupe_effects(&mut summary.effects);
    summary
}

fn function_effects_from_function(
    function: &oxc_ast::ast::Function<'_>,
    provider_bindings: &BTreeMap<String, ProviderBinding>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    source: &str,
    source_name: &str,
) -> FunctionEffectSummary {
    let mut summary = FunctionEffectSummary {
        provider_bindings: provider_bindings.clone(),
        source_name: source_name.to_string(),
        source_text: source.to_string(),
        ..FunctionEffectSummary::default()
    };
    if let Some(body) = &function.body {
        for statement in &body.statements {
            collect_statement_effects(statement, helper_effects, &mut summary);
        }
    }
    dedupe_effects(&mut summary.effects);
    summary
}

fn collect_statement_effects(
    statement: &Statement<'_>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    summary: &mut FunctionEffectSummary,
) {
    match statement {
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(name) = binding_identifier(&declarator.id) {
                    if let Some(init) = &declarator.init {
                        if let Some(binding) = data_provider_binding(init) {
                            summary.provider_bindings.insert(name.to_string(), binding);
                        } else {
                            collect_expression_effects(init, helper_effects, summary);
                        }
                    }
                }
            }
        }
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                collect_expression_effects(argument, helper_effects, summary);
            }
        }
        Statement::ExpressionStatement(statement) => {
            collect_expression_effects(&statement.expression, helper_effects, summary);
        }
        Statement::BlockStatement(block) => {
            for statement in &block.body {
                collect_statement_effects(statement, helper_effects, summary);
            }
        }
        Statement::IfStatement(statement) => {
            collect_expression_effects(&statement.test, helper_effects, summary);
            collect_statement_effects(&statement.consequent, helper_effects, summary);
            if let Some(alternate) = &statement.alternate {
                collect_statement_effects(alternate, helper_effects, summary);
            }
        }
        Statement::DoWhileStatement(statement) => {
            collect_statement_effects(&statement.body, helper_effects, summary);
            collect_expression_effects(&statement.test, helper_effects, summary);
        }
        Statement::WhileStatement(statement) => {
            collect_expression_effects(&statement.test, helper_effects, summary);
            collect_statement_effects(&statement.body, helper_effects, summary);
        }
        Statement::ForStatement(statement) => {
            if let Some(init) = &statement.init {
                collect_for_init_effects(init, helper_effects, summary);
            }
            if let Some(test) = &statement.test {
                collect_expression_effects(test, helper_effects, summary);
            }
            if let Some(update) = &statement.update {
                collect_expression_effects(update, helper_effects, summary);
            }
            collect_statement_effects(&statement.body, helper_effects, summary);
        }
        Statement::ForInStatement(statement) => {
            collect_expression_effects(&statement.right, helper_effects, summary);
            collect_statement_effects(&statement.body, helper_effects, summary);
        }
        Statement::ForOfStatement(statement) => {
            collect_expression_effects(&statement.right, helper_effects, summary);
            collect_statement_effects(&statement.body, helper_effects, summary);
        }
        Statement::SwitchStatement(statement) => {
            collect_expression_effects(&statement.discriminant, helper_effects, summary);
            for case in &statement.cases {
                if let Some(test) = &case.test {
                    collect_expression_effects(test, helper_effects, summary);
                }
                for statement in &case.consequent {
                    collect_statement_effects(statement, helper_effects, summary);
                }
            }
        }
        Statement::TryStatement(statement) => {
            for statement in &statement.block.body {
                collect_statement_effects(statement, helper_effects, summary);
            }
            if let Some(handler) = &statement.handler {
                for statement in &handler.body.body {
                    collect_statement_effects(statement, helper_effects, summary);
                }
            }
            if let Some(finalizer) = &statement.finalizer {
                for statement in &finalizer.body {
                    collect_statement_effects(statement, helper_effects, summary);
                }
            }
        }
        Statement::ThrowStatement(statement) => {
            collect_expression_effects(&statement.argument, helper_effects, summary);
        }
        _ => {}
    }
}

fn collect_for_init_effects(
    init: &ForStatementInit<'_>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    summary: &mut FunctionEffectSummary,
) {
    match init {
        ForStatementInit::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_expression_effects(init, helper_effects, summary);
                }
            }
        }
        ForStatementInit::CallExpression(call) => {
            collect_call_effects(call, helper_effects, summary);
            for argument in &call.arguments {
                collect_argument_effects(argument, helper_effects, summary);
            }
        }
        ForStatementInit::ParenthesizedExpression(parenthesized) => {
            collect_expression_effects(&parenthesized.expression, helper_effects, summary);
        }
        _ => {}
    }
}

fn collect_expression_effects(
    expression: &Expression<'_>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    summary: &mut FunctionEffectSummary,
) {
    match expression {
        Expression::CallExpression(call) => {
            collect_call_effects(call, helper_effects, summary);
            for argument in &call.arguments {
                collect_argument_effects(argument, helper_effects, summary);
            }
        }
        Expression::ConditionalExpression(conditional) => {
            collect_expression_effects(&conditional.test, helper_effects, summary);
            collect_expression_effects(&conditional.consequent, helper_effects, summary);
            collect_expression_effects(&conditional.alternate, helper_effects, summary);
        }
        Expression::LogicalExpression(logical) => {
            collect_expression_effects(&logical.left, helper_effects, summary);
            collect_expression_effects(&logical.right, helper_effects, summary);
        }
        Expression::SequenceExpression(sequence) => {
            for expression in &sequence.expressions {
                collect_expression_effects(expression, helper_effects, summary);
            }
        }
        Expression::ChainExpression(chain) => {
            collect_chain_effects(&chain.expression, helper_effects, summary);
        }
        Expression::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_effects(&property.value, helper_effects, summary);
                }
            }
        }
        Expression::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_effects(element, helper_effects, summary);
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            collect_expression_effects(&parenthesized.expression, helper_effects, summary);
        }
        Expression::AwaitExpression(await_expression) => {
            collect_expression_effects(&await_expression.argument, helper_effects, summary);
        }
        Expression::StaticMemberExpression(member) => {
            if let Expression::Identifier(identifier) = &member.object {
                if summary
                    .provider_bindings
                    .contains_key(identifier.name.as_str())
                {
                    summary.unknown_provider_usage = true;
                    return;
                }
            }
            collect_expression_effects(&member.object, helper_effects, summary);
        }
        Expression::Identifier(identifier)
            if summary
                .provider_bindings
                .contains_key(identifier.name.as_str()) =>
        {
            summary.unknown_provider_usage = true;
        }
        _ => {}
    }
}

fn collect_chain_effects(
    expression: &ChainElement<'_>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    summary: &mut FunctionEffectSummary,
) {
    match expression {
        ChainElement::CallExpression(call) => {
            collect_call_effects(call, helper_effects, summary);
            for argument in &call.arguments {
                collect_argument_effects(argument, helper_effects, summary);
            }
        }
        ChainElement::StaticMemberExpression(member) => {
            collect_expression_effects(&member.object, helper_effects, summary);
        }
        ChainElement::ComputedMemberExpression(member) => {
            collect_expression_effects(&member.object, helper_effects, summary);
            collect_expression_effects(&member.expression, helper_effects, summary);
        }
        ChainElement::TSNonNullExpression(expression) => {
            collect_expression_effects(&expression.expression, helper_effects, summary);
        }
        _ => {}
    }
}

fn collect_argument_effects(
    argument: &Argument<'_>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    summary: &mut FunctionEffectSummary,
) {
    match argument {
        Argument::CallExpression(call) => {
            collect_call_effects(call, helper_effects, summary);
            for argument in &call.arguments {
                collect_argument_effects(argument, helper_effects, summary);
            }
        }
        Argument::ConditionalExpression(conditional) => {
            collect_expression_effects(&conditional.test, helper_effects, summary);
            collect_expression_effects(&conditional.consequent, helper_effects, summary);
            collect_expression_effects(&conditional.alternate, helper_effects, summary);
        }
        Argument::LogicalExpression(logical) => {
            collect_expression_effects(&logical.left, helper_effects, summary);
            collect_expression_effects(&logical.right, helper_effects, summary);
        }
        Argument::SequenceExpression(sequence) => {
            for expression in &sequence.expressions {
                collect_expression_effects(expression, helper_effects, summary);
            }
        }
        Argument::ChainExpression(chain) => {
            collect_chain_effects(&chain.expression, helper_effects, summary);
        }
        Argument::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_effects(&property.value, helper_effects, summary);
                }
            }
        }
        Argument::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_effects(element, helper_effects, summary);
            }
        }
        Argument::ParenthesizedExpression(parenthesized) => {
            collect_expression_effects(&parenthesized.expression, helper_effects, summary);
        }
        Argument::AwaitExpression(await_expression) => {
            collect_expression_effects(&await_expression.argument, helper_effects, summary);
        }
        _ => {}
    }
}

fn collect_array_element_effects(
    element: &ArrayExpressionElement<'_>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    summary: &mut FunctionEffectSummary,
) {
    match element {
        ArrayExpressionElement::CallExpression(call) => {
            collect_call_effects(call, helper_effects, summary);
        }
        ArrayExpressionElement::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_effects(&property.value, helper_effects, summary);
                }
            }
        }
        ArrayExpressionElement::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_effects(element, helper_effects, summary);
            }
        }
        _ => {}
    }
}

fn collect_call_effects(
    call: &CallExpression<'_>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    summary: &mut FunctionEffectSummary,
) {
    if let Some((receiver, method)) = static_member_name(&call.callee) {
        if let Some(binding) = summary.provider_bindings.get(receiver) {
            if let Some(access) = provider_method_access(binding, method, call) {
                summary.effects.push(EffectMetadata {
                    provider: binding.token.clone(),
                    capability_kind: binding.capability_kind.clone(),
                    provider_kind: binding.provider.clone(),
                    access,
                    operation: method.to_string(),
                    reason: format!("{receiver}.{method}"),
                    source_name: summary.source_name.clone(),
                    source_text: summary.source_text.clone(),
                    span: call.span,
                });
            } else if method != "close" {
                summary.unknown_provider_usage = true;
            }
        }
    }

    if let Expression::Identifier(identifier) = &call.callee {
        let helper_name = identifier.name.as_str();
        summary.helper_calls.insert(helper_name.to_string());
        if let Some(helper) = helper_effects.get(helper_name) {
            summary.effects.extend(helper.effects.iter().cloned());
            summary.unknown_provider_usage |= helper.unknown_provider_usage;
            for (name, binding) in &helper.provider_bindings {
                summary
                    .provider_bindings
                    .entry(name.clone())
                    .or_insert_with(|| binding.clone());
            }
        }
    }
}

fn resolve_helper_effect_callgraph(helper_effects: &mut BTreeMap<String, FunctionEffectSummary>) {
    let mut changed = true;
    while changed {
        changed = false;
        let snapshot = helper_effects.clone();
        for summary in helper_effects.values_mut() {
            let calls = summary.helper_calls.iter().cloned().collect::<Vec<_>>();
            for helper_name in calls {
                let Some(callee) = snapshot.get(&helper_name) else {
                    continue;
                };
                let before_effects = summary.effects.len();
                summary.effects.extend(callee.effects.iter().cloned());
                dedupe_effects(&mut summary.effects);
                if summary.effects.len() != before_effects {
                    changed = true;
                }
                for (name, binding) in &callee.provider_bindings {
                    if summary
                        .provider_bindings
                        .insert(name.clone(), binding.clone())
                        .is_none()
                    {
                        changed = true;
                    }
                }
                if callee.unknown_provider_usage && !summary.unknown_provider_usage {
                    summary.unknown_provider_usage = true;
                    changed = true;
                }
            }
        }
    }
}

fn data_provider_binding(expression: &Expression<'_>) -> Option<ProviderBinding> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, method) = static_member_name(&call.callee)?;
    if receiver != "data" || !database_provider_supported(method) || call.arguments.len() != 1 {
        return None;
    }
    let name = string_argument(call.arguments.first()?)?;
    Some(ProviderBinding {
        token: normalize_database_provider_token(name),
        capability_kind: "database".to_string(),
        provider: method.to_string(),
    })
}

fn provider_method_access(
    binding: &ProviderBinding,
    method: &str,
    call: &CallExpression<'_>,
) -> Option<&'static str> {
    if binding.capability_kind != "database" {
        return None;
    }
    match method {
        "query" | "queryOne" => Some("read"),
        "exec" => Some(sql_access_from_call(call).unwrap_or("write")),
        "transaction" => Some("readwrite"),
        _ => None,
    }
}

fn sql_access_from_call(call: &CallExpression<'_>) -> Option<&'static str> {
    let sql = call.arguments.first().and_then(string_argument)?;
    let trimmed = sql.trim_start().to_ascii_uppercase();
    if trimmed.starts_with("SELECT") {
        Some("read")
    } else {
        Some("write")
    }
}

fn dedupe_effects(effects: &mut Vec<EffectMetadata>) {
    let mut seen = BTreeSet::new();
    effects.retain(|effect| {
        seen.insert(format!(
            "{}:{}:{}",
            effect.provider, effect.access, effect.operation
        ))
    });
}

fn coalesce_manual_capability_overrides(capabilities: &mut Vec<DatabaseCapability>) {
    let manual_tokens = capabilities
        .iter()
        .filter(|capability| !capability.from_provider_use)
        .map(|capability| capability.token.clone())
        .collect::<BTreeSet<_>>();
    capabilities.retain(|capability| {
        !capability.from_provider_use || !manual_tokens.contains(&capability.token)
    });
}

fn validate_provider_effect_registrations(
    _path: &Path,
    routes: &[Route],
    capabilities: &[DatabaseCapability],
) -> Result<(), Diagnostic> {
    for route in routes {
        for effect in &route.handler.effects {
            let registered = capabilities.iter().any(|capability| {
                capability.token == effect.provider
                    && capability.capability_kind == effect.capability_kind
                    && capability.provider == effect.provider_kind
            });
            if !registered {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_MISSING_PROVIDER",
                    format!(
                        "route uses unregistered {} provider '{}'",
                        effect.capability_kind, effect.provider
                    ),
                )
                .with_path(&route.source_path)
                .with_span(route.span)
                .with_hint(
                    "Register the provider with app.use(...), builder.capabilities metadata, or an explicit runtime-only escape hatch once that pattern is supported.",
                ));
            }
        }
    }
    Ok(())
}

fn apply_inferred_capability_access(capabilities: &mut [DatabaseCapability], routes: &[Route]) {
    let mut inferred = BTreeMap::<String, &'static str>::new();
    for route in routes {
        for effect in &route.handler.effects {
            inferred
                .entry(effect.provider.clone())
                .and_modify(|access| *access = merge_access(access, effect.access))
                .or_insert(effect.access);
        }
    }

    for capability in capabilities {
        if capability.from_provider_use {
            if let Some(access) = inferred.get(&capability.token) {
                capability.access = (*access).to_string();
            }
        }
    }
}

fn merge_access(left: &'static str, right: &'static str) -> &'static str {
    match (left, right) {
        ("read", "read") => "read",
        ("write", "write") => "write",
        ("readwrite", _) | (_, "readwrite") => "readwrite",
        _ => "readwrite",
    }
}

fn arrow_has_typescript_syntax(function: &oxc_ast::ast::ArrowFunctionExpression<'_>) -> bool {
    function.type_parameters.is_some()
        || function.return_type.is_some()
        || parameters_have_typescript_syntax(&function.params)
}

fn function_has_typescript_syntax(function: &oxc_ast::ast::Function<'_>) -> bool {
    function.type_parameters.is_some()
        || function.this_param.is_some()
        || function.return_type.is_some()
        || parameters_have_typescript_syntax(&function.params)
}

fn parameters_have_typescript_syntax(parameters: &oxc_ast::ast::FormalParameters<'_>) -> bool {
    parameters.items.iter().any(|parameter| {
        parameter.type_annotation.is_some()
            || parameter.optional
            || parameter.accessibility.is_some()
            || parameter.readonly
            || parameter.r#override
    }) || parameters
        .rest
        .as_ref()
        .is_some_and(|rest| rest.type_annotation.is_some())
}

fn handler_result_uses_unsupported_values_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    schema_names: &BTreeSet<String>,
) -> bool {
    let roots = function_parameter_roots(&function.params);
    if function.expression {
        return function
            .body
            .statements
            .first()
            .and_then(expression_statement_result_call)
            .is_some_and(|call| !results_call_arguments_are_supported(call, &roots, schema_names));
    }

    function
        .body
        .statements
        .first()
        .and_then(return_statement_result_call)
        .is_some_and(|call| !results_call_arguments_are_supported(call, &roots, schema_names))
}

fn handler_result_uses_unsupported_values_function(
    function: &oxc_ast::ast::Function<'_>,
    schema_names: &BTreeSet<String>,
) -> bool {
    let roots = function_parameter_roots(&function.params);
    let Some(body) = &function.body else {
        return false;
    };
    body.statements
        .first()
        .and_then(return_statement_result_call)
        .is_some_and(|call| !results_call_arguments_are_supported(call, &roots, schema_names))
}

fn response_metadata_from_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
) -> Option<ResponseMetadata> {
    let call = if function.expression {
        function
            .body
            .statements
            .first()
            .and_then(expression_statement_result_call)
    } else {
        function
            .body
            .statements
            .first()
            .and_then(return_statement_result_call)
    }?;
    response_metadata_from_call(call)
}

fn response_metadata_from_function(
    function: &oxc_ast::ast::Function<'_>,
) -> Option<ResponseMetadata> {
    let body = function.body.as_ref()?;
    let call = body
        .statements
        .first()
        .and_then(return_statement_result_call)?;
    response_metadata_from_call(call)
}

fn response_metadata_many_from_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    source_name: &str,
    source: &str,
    schema_names: &BTreeSet<String>,
) -> Vec<ResponseMetadata> {
    let mut responses = Vec::new();
    let mut response_schema_scopes = vec![BTreeMap::new()];
    for statement in &function.body.statements {
        collect_statement_responses(
            statement,
            source_name,
            source,
            &mut response_schema_scopes,
            schema_names,
            &mut responses,
        );
    }
    dedupe_response_metadata(responses)
}

fn response_metadata_many_from_function(
    function: &oxc_ast::ast::Function<'_>,
    source_name: &str,
    source: &str,
    schema_names: &BTreeSet<String>,
) -> Vec<ResponseMetadata> {
    let mut responses = Vec::new();
    if let Some(body) = &function.body {
        let mut response_schema_scopes = vec![BTreeMap::new()];
        for statement in &body.statements {
            collect_statement_responses(
                statement,
                source_name,
                source,
                &mut response_schema_scopes,
                schema_names,
                &mut responses,
            );
        }
    }
    dedupe_response_metadata(responses)
}

fn collect_statement_responses(
    statement: &Statement<'_>,
    source_name: &str,
    source: &str,
    response_schema_scopes: &mut Vec<BTreeMap<String, String>>,
    schema_names: &BTreeSet<String>,
    responses: &mut Vec<ResponseMetadata>,
) {
    match statement {
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                let BindingPattern::BindingIdentifier(identifier) = &declarator.id else {
                    continue;
                };
                let Some(init) = &declarator.init else {
                    continue;
                };
                let Some(schema) =
                    response_schema_from_expression(init, response_schema_scopes, schema_names)
                else {
                    continue;
                };
                if let Some(scope) = response_schema_scopes.last_mut() {
                    scope.insert(identifier.name.as_str().to_string(), schema);
                }
            }
        }
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                collect_expression_responses(
                    argument,
                    source_name,
                    source,
                    response_schema_scopes,
                    schema_names,
                    responses,
                );
            }
        }
        Statement::ExpressionStatement(statement) => {
            collect_expression_responses(
                &statement.expression,
                source_name,
                source,
                response_schema_scopes,
                schema_names,
                responses,
            );
        }
        Statement::BlockStatement(block) => {
            response_schema_scopes.push(BTreeMap::new());
            for statement in &block.body {
                collect_statement_responses(
                    statement,
                    source_name,
                    source,
                    response_schema_scopes,
                    schema_names,
                    responses,
                );
            }
            response_schema_scopes.pop();
        }
        Statement::IfStatement(statement) => {
            let mut consequent_scopes = response_schema_scopes.clone();
            collect_statement_responses(
                &statement.consequent,
                source_name,
                source,
                &mut consequent_scopes,
                schema_names,
                responses,
            );
            if let Some(alternate) = &statement.alternate {
                let mut alternate_scopes = response_schema_scopes.clone();
                collect_statement_responses(
                    alternate,
                    source_name,
                    source,
                    &mut alternate_scopes,
                    schema_names,
                    responses,
                );
            }
        }
        _ => {}
    }
}

fn collect_expression_responses(
    expression: &Expression<'_>,
    source_name: &str,
    source: &str,
    response_schema_scopes: &mut Vec<BTreeMap<String, String>>,
    schema_names: &BTreeSet<String>,
    responses: &mut Vec<ResponseMetadata>,
) {
    if let Some(call) = result_call(expression) {
        if let Some(mut response) = response_metadata_from_call(call) {
            response.body_schema =
                response_schema_from_result_call(call, response_schema_scopes, schema_names);
            response.source_name = Some(source_name.to_string());
            response.source_text = Some(source.to_string());
            response.span = Some(call.span);
            responses.push(response);
        }
        return;
    }
    match expression {
        Expression::ConditionalExpression(conditional) => {
            collect_expression_responses(
                &conditional.consequent,
                source_name,
                source,
                response_schema_scopes,
                schema_names,
                responses,
            );
            collect_expression_responses(
                &conditional.alternate,
                source_name,
                source,
                response_schema_scopes,
                schema_names,
                responses,
            );
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            collect_expression_responses(
                &parenthesized.expression,
                source_name,
                source,
                response_schema_scopes,
                schema_names,
                responses,
            );
        }
        _ => {}
    }
}

fn response_schema_from_result_call(
    call: &CallExpression<'_>,
    response_schema_scopes: &[BTreeMap<String, String>],
    schema_names: &BTreeSet<String>,
) -> Option<String> {
    response_schema_from_result_type_arguments(call, schema_names).or_else(|| {
        response_body_argument(call).and_then(|argument| {
            response_schema_from_argument(argument, response_schema_scopes, schema_names)
        })
    })
}

fn response_schema_from_result_type_arguments(
    call: &CallExpression<'_>,
    schema_names: &BTreeSet<String>,
) -> Option<String> {
    let (receiver, _) = static_member_name(&call.callee)?;
    if receiver != "Results" {
        return None;
    }
    response_schema_from_type_arguments(call, schema_names)
}

fn response_schema_from_data_call_type_arguments(
    call: &CallExpression<'_>,
    schema_names: &BTreeSet<String>,
) -> Option<String> {
    let (_, method) = static_member_name(&call.callee)?;
    if !matches!(method, "queryOne") {
        return None;
    }
    response_schema_from_type_arguments(call, schema_names)
}

fn response_schema_from_type_arguments(
    call: &CallExpression<'_>,
    schema_names: &BTreeSet<String>,
) -> Option<String> {
    let ty = call.type_arguments.as_ref()?.params.first()?;
    schema_name_from_type(ty, schema_names)
}

fn response_body_argument<'a>(call: &'a CallExpression<'a>) -> Option<&'a Argument<'a>> {
    let (_, helper) = static_member_name(&call.callee)?;
    let index = match helper {
        "ok" | "json" | "bytes" | "accepted" | "badRequest" | "notFound" | "problem" => 0,
        "created" | "status" => 1,
        _ => return None,
    };
    call.arguments.get(index)
}

fn response_schema_from_argument(
    argument: &Argument<'_>,
    response_schema_scopes: &[BTreeMap<String, String>],
    schema_names: &BTreeSet<String>,
) -> Option<String> {
    match argument {
        Argument::Identifier(identifier) => {
            response_schema_lookup(response_schema_scopes, identifier.name.as_str())
        }
        Argument::CallExpression(call) => {
            response_schema_from_data_call_type_arguments(call, schema_names)
        }
        Argument::ParenthesizedExpression(parenthesized) => response_schema_from_expression(
            &parenthesized.expression,
            response_schema_scopes,
            schema_names,
        ),
        _ => None,
    }
}

fn response_schema_from_expression(
    expression: &Expression<'_>,
    response_schema_scopes: &[BTreeMap<String, String>],
    schema_names: &BTreeSet<String>,
) -> Option<String> {
    match expression {
        Expression::Identifier(identifier) => {
            response_schema_lookup(response_schema_scopes, identifier.name.as_str())
        }
        Expression::CallExpression(call) => {
            response_schema_from_data_call_type_arguments(call, schema_names)
        }
        Expression::AwaitExpression(await_expression) => response_schema_from_expression(
            &await_expression.argument,
            response_schema_scopes,
            schema_names,
        ),
        Expression::ParenthesizedExpression(parenthesized) => response_schema_from_expression(
            &parenthesized.expression,
            response_schema_scopes,
            schema_names,
        ),
        _ => None,
    }
}

fn response_schema_lookup(
    response_schema_scopes: &[BTreeMap<String, String>],
    name: &str,
) -> Option<String> {
    response_schema_scopes
        .iter()
        .rev()
        .find_map(|scope| scope.get(name).cloned())
}

fn dedupe_response_metadata(responses: Vec<ResponseMetadata>) -> Vec<ResponseMetadata> {
    let mut seen = BTreeSet::new();
    let mut deduped = Vec::new();
    for response in responses {
        let key = format!(
            "{}:{}:{}:{}:{}",
            response.helper,
            response.status,
            response.kind,
            response.body_schema.as_deref().unwrap_or(""),
            response.partial
        );
        if seen.insert(key) {
            deduped.push(response);
        }
    }
    deduped
}

fn response_metadata_from_call(call: &CallExpression<'_>) -> Option<ResponseMetadata> {
    let (_, helper) = static_member_name(&call.callee)?;
    let (status, kind) = match helper {
        "ok" => (200, "json"),
        "json" => (200, "json"),
        "text" => (200, "text"),
        "html" => (200, "html"),
        "bytes" => (200, "bytes"),
        "created" => (201, "json"),
        "accepted" => (202, "json"),
        "noContent" => (204, "none"),
        "badRequest" => (400, "problem"),
        "notFound" => (404, "problem"),
        "problem" => (500, "problem"),
        "status" => (status_result_code(call)?, "json"),
        _ => return None,
    };
    Some(ResponseMetadata {
        helper: helper.to_string(),
        status,
        kind: kind.to_string(),
        body_schema: None,
        source_name: None,
        source_text: None,
        span: Some(call.span),
        partial: false,
    })
}

fn status_result_code(call: &CallExpression<'_>) -> Option<u16> {
    let Argument::NumericLiteral(literal) = call.arguments.first()? else {
        return None;
    };
    let value = literal.value;
    if value.fract() == 0.0 && (100.0..=599.0).contains(&value) {
        Some(value as u16)
    } else {
        None
    }
}

fn request_bindings_from_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    schema_names: &BTreeSet<String>,
) -> Vec<RequestBinding> {
    let Some(ctx_name) = handler_context_parameter_name(&function.params) else {
        return Vec::new();
    };
    let mut bindings = Vec::new();
    for statement in &function.body.statements {
        collect_statement_request_bindings(statement, &ctx_name, schema_names, &mut bindings);
    }
    dedupe_request_bindings(bindings)
}

fn request_bindings_from_function(
    function: &oxc_ast::ast::Function<'_>,
    schema_names: &BTreeSet<String>,
) -> Vec<RequestBinding> {
    let Some(ctx_name) = handler_context_parameter_name(&function.params) else {
        return Vec::new();
    };
    let mut bindings = Vec::new();
    if let Some(body) = &function.body {
        for statement in &body.statements {
            collect_statement_request_bindings(statement, &ctx_name, schema_names, &mut bindings);
        }
    }
    dedupe_request_bindings(bindings)
}

fn dedupe_request_bindings(bindings: Vec<RequestBinding>) -> Vec<RequestBinding> {
    let mut seen = BTreeSet::new();
    let mut deduped = Vec::new();
    for binding in bindings {
        let key = format!(
            "{}:{}:{}",
            binding.kind,
            binding.name.clone().unwrap_or_default(),
            binding.schema.clone().unwrap_or_default()
        );
        if seen.insert(key) {
            deduped.push(binding);
        }
    }
    deduped
}

fn collect_statement_request_bindings(
    statement: &Statement<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    bindings: &mut Vec<RequestBinding>,
) {
    match statement {
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                collect_expression_request_bindings(argument, ctx_name, schema_names, bindings);
            }
        }
        Statement::ExpressionStatement(statement) => {
            collect_expression_request_bindings(
                &statement.expression,
                ctx_name,
                schema_names,
                bindings,
            );
        }
        _ => {}
    }
}

fn collect_expression_request_bindings(
    expression: &Expression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    bindings: &mut Vec<RequestBinding>,
) {
    if let Some(binding) = request_binding_from_expression(expression, ctx_name) {
        bindings.push(binding);
    }
    match expression {
        Expression::CallExpression(call) => {
            if let Some(binding) = request_binding_from_call(call, ctx_name, schema_names) {
                bindings.push(binding);
            }
            for argument in &call.arguments {
                collect_argument_request_bindings(argument, ctx_name, schema_names, bindings);
            }
        }
        Expression::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_request_bindings(
                        &property.value,
                        ctx_name,
                        schema_names,
                        bindings,
                    );
                }
            }
        }
        Expression::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_request_bindings(element, ctx_name, schema_names, bindings);
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            collect_expression_request_bindings(
                &parenthesized.expression,
                ctx_name,
                schema_names,
                bindings,
            );
        }
        Expression::StaticMemberExpression(member) => {
            collect_expression_request_bindings(&member.object, ctx_name, schema_names, bindings);
        }
        _ => {}
    }
}

fn collect_argument_request_bindings(
    argument: &Argument<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    bindings: &mut Vec<RequestBinding>,
) {
    match argument {
        Argument::CallExpression(call) => {
            if let Some(binding) = request_binding_from_call(call, ctx_name, schema_names) {
                bindings.push(binding);
            }
            for argument in &call.arguments {
                collect_argument_request_bindings(argument, ctx_name, schema_names, bindings);
            }
        }
        Argument::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_request_bindings(
                        &property.value,
                        ctx_name,
                        schema_names,
                        bindings,
                    );
                }
            }
        }
        Argument::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_request_bindings(element, ctx_name, schema_names, bindings);
            }
        }
        Argument::StaticMemberExpression(member) => {
            collect_expression_request_bindings(&member.object, ctx_name, schema_names, bindings);
        }
        _ => {}
    }
}

fn collect_array_element_request_bindings(
    element: &ArrayExpressionElement<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    bindings: &mut Vec<RequestBinding>,
) {
    match element {
        ArrayExpressionElement::CallExpression(call) => {
            if let Some(binding) = request_binding_from_call(call, ctx_name, schema_names) {
                bindings.push(binding);
            }
        }
        ArrayExpressionElement::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_request_bindings(
                        &property.value,
                        ctx_name,
                        schema_names,
                        bindings,
                    );
                }
            }
        }
        ArrayExpressionElement::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_request_bindings(element, ctx_name, schema_names, bindings);
            }
        }
        _ => {}
    }
}

fn request_binding_from_expression(
    expression: &Expression<'_>,
    ctx_name: &str,
) -> Option<RequestBinding> {
    let chain = static_member_chain(expression)?;
    if chain.len() == 3 && chain[0] == ctx_name && matches!(chain[1], "route" | "query" | "header")
    {
        return Some(RequestBinding {
            kind: chain[1].to_string(),
            name: Some(chain[2].to_string()),
            schema: None,
            parameter: None,
            type_name: None,
            source_name: None,
            source_text: None,
            span: None,
            wrapper: None,
            injection_kind: None,
            provider_kind: None,
            capability: None,
            semantic: None,
            redacted: false,
        });
    }
    None
}

fn request_binding_from_call(
    call: &CallExpression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<RequestBinding> {
    let chain = static_member_chain(&call.callee)?;
    if chain.len() == 3 && chain[0] == ctx_name && chain[1] == "body" {
        let schema = body_binding_schema(call, chain[2], schema_names)?;
        return Some(RequestBinding {
            kind: format!("body.{}", chain[2]),
            name: None,
            schema,
            parameter: None,
            type_name: None,
            source_name: None,
            source_text: None,
            span: None,
            wrapper: None,
            injection_kind: None,
            provider_kind: None,
            capability: None,
            semantic: None,
            redacted: false,
        });
    }
    None
}

fn body_binding_schema(
    call: &CallExpression<'_>,
    method: &str,
    schema_names: &BTreeSet<String>,
) -> Option<Option<String>> {
    match method {
        "text" if call.arguments.is_empty() => Some(None),
        "json" if call.arguments.is_empty() => Some(None),
        "json" if call.arguments.len() == 1 => {
            let schema = call.arguments.first().and_then(argument_identifier)?;
            if schema_names.contains(schema) {
                Some(Some(schema.to_string()))
            } else {
                None
            }
        }
        _ => None,
    }
}

fn body_binding_call_is_supported(
    call: &CallExpression<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    let Some(chain) = static_member_chain(&call.callee) else {
        return false;
    };
    chain.len() == 3
        && allowed_roots.contains(chain[0])
        && chain[1] == "body"
        && body_binding_schema(call, chain[2], schema_names).is_some()
}

fn body_json_schema_argument_spans_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Vec<Span> {
    let mut spans = Vec::new();
    for statement in &function.body.statements {
        collect_statement_schema_argument_spans(statement, ctx_name, schema_names, &mut spans);
    }
    spans
}

fn body_json_schema_argument_spans_function(
    function: &oxc_ast::ast::Function<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Vec<Span> {
    let mut spans = Vec::new();
    if let Some(body) = &function.body {
        for statement in &body.statements {
            collect_statement_schema_argument_spans(statement, ctx_name, schema_names, &mut spans);
        }
    }
    spans
}

fn collect_statement_schema_argument_spans(
    statement: &Statement<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    spans: &mut Vec<Span>,
) {
    match statement {
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                collect_expression_schema_argument_spans(argument, ctx_name, schema_names, spans);
            }
        }
        Statement::ExpressionStatement(statement) => {
            collect_expression_schema_argument_spans(
                &statement.expression,
                ctx_name,
                schema_names,
                spans,
            );
        }
        _ => {}
    }
}

fn collect_expression_schema_argument_spans(
    expression: &Expression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    spans: &mut Vec<Span>,
) {
    match expression {
        Expression::CallExpression(call) => {
            if let Some(span) = body_json_schema_argument_span(call, ctx_name, schema_names) {
                spans.push(span);
            }
            for argument in &call.arguments {
                collect_argument_schema_argument_spans(argument, ctx_name, schema_names, spans);
            }
        }
        Expression::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_schema_argument_spans(
                        &property.value,
                        ctx_name,
                        schema_names,
                        spans,
                    );
                }
            }
        }
        Expression::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_schema_argument_spans(element, ctx_name, schema_names, spans);
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            collect_expression_schema_argument_spans(
                &parenthesized.expression,
                ctx_name,
                schema_names,
                spans,
            );
        }
        _ => {}
    }
}

fn collect_argument_schema_argument_spans(
    argument: &Argument<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    spans: &mut Vec<Span>,
) {
    match argument {
        Argument::CallExpression(call) => {
            if let Some(span) = body_json_schema_argument_span(call, ctx_name, schema_names) {
                spans.push(span);
            }
            for argument in &call.arguments {
                collect_argument_schema_argument_spans(argument, ctx_name, schema_names, spans);
            }
        }
        Argument::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_schema_argument_spans(
                        &property.value,
                        ctx_name,
                        schema_names,
                        spans,
                    );
                }
            }
        }
        Argument::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_schema_argument_spans(element, ctx_name, schema_names, spans);
            }
        }
        _ => {}
    }
}

fn collect_array_element_schema_argument_spans(
    element: &ArrayExpressionElement<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    spans: &mut Vec<Span>,
) {
    match element {
        ArrayExpressionElement::CallExpression(call) => {
            if let Some(span) = body_json_schema_argument_span(call, ctx_name, schema_names) {
                spans.push(span);
            }
        }
        ArrayExpressionElement::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_schema_argument_spans(
                        &property.value,
                        ctx_name,
                        schema_names,
                        spans,
                    );
                }
            }
        }
        ArrayExpressionElement::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_schema_argument_spans(element, ctx_name, schema_names, spans);
            }
        }
        _ => {}
    }
}

fn body_json_schema_argument_span(
    call: &CallExpression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<Span> {
    let chain = static_member_chain(&call.callee)?;
    if chain.len() == 3 && chain[0] == ctx_name && chain[1] == "body" && chain[2] == "json" {
        if call.arguments.len() != 1 {
            return None;
        }
        let Argument::Identifier(identifier) = call.arguments.first()? else {
            return None;
        };
        if !schema_names.contains(identifier.name.as_str()) {
            return None;
        }
        return Some(identifier.span);
    }
    None
}

fn sanitize_handler_schema_references(
    mut source: String,
    handler_start: u32,
    spans: &[Span],
) -> String {
    let mut spans = spans.to_vec();
    spans.sort_by_key(|span| std::cmp::Reverse(span.start));
    for span in spans {
        let Some(start) = span.start.checked_sub(handler_start) else {
            continue;
        };
        let Some(end) = span.end.checked_sub(handler_start) else {
            continue;
        };
        let Ok(start) = usize::try_from(start) else {
            continue;
        };
        let Ok(end) = usize::try_from(end) else {
            continue;
        };
        if start <= end && end <= source.len() {
            source.replace_range(start..end, "undefined");
        }
    }
    source
}

fn argument_identifier<'a>(argument: &'a Argument<'a>) -> Option<&'a str> {
    match argument {
        Argument::Identifier(identifier) => Some(identifier.name.as_str()),
        _ => None,
    }
}

fn argument_span(argument: &Argument<'_>) -> Option<Span> {
    match argument {
        Argument::SpreadElement(node) => Some(node.span),
        Argument::BooleanLiteral(node) => Some(node.span),
        Argument::NullLiteral(node) => Some(node.span),
        Argument::NumericLiteral(node) => Some(node.span),
        Argument::BigIntLiteral(node) => Some(node.span),
        Argument::RegExpLiteral(node) => Some(node.span),
        Argument::StringLiteral(node) => Some(node.span),
        Argument::TemplateLiteral(node) => Some(node.span),
        Argument::Identifier(node) => Some(node.span),
        Argument::MetaProperty(node) => Some(node.span),
        Argument::Super(node) => Some(node.span),
        Argument::ArrayExpression(node) => Some(node.span),
        Argument::ArrowFunctionExpression(node) => Some(node.span),
        Argument::AssignmentExpression(node) => Some(node.span),
        Argument::AwaitExpression(node) => Some(node.span),
        Argument::BinaryExpression(node) => Some(node.span),
        Argument::CallExpression(node) => Some(node.span),
        Argument::ChainExpression(node) => Some(node.span),
        Argument::ClassExpression(node) => Some(node.span),
        Argument::ConditionalExpression(node) => Some(node.span),
        Argument::FunctionExpression(node) => Some(node.span),
        Argument::ImportExpression(node) => Some(node.span),
        Argument::LogicalExpression(node) => Some(node.span),
        Argument::NewExpression(node) => Some(node.span),
        Argument::ObjectExpression(node) => Some(node.span),
        Argument::ParenthesizedExpression(node) => Some(node.span),
        Argument::SequenceExpression(node) => Some(node.span),
        Argument::TaggedTemplateExpression(node) => Some(node.span),
        Argument::ThisExpression(node) => Some(node.span),
        Argument::UnaryExpression(node) => Some(node.span),
        Argument::UpdateExpression(node) => Some(node.span),
        Argument::YieldExpression(node) => Some(node.span),
        Argument::PrivateInExpression(node) => Some(node.span),
        Argument::JSXElement(node) => Some(node.span),
        Argument::JSXFragment(node) => Some(node.span),
        Argument::TSAsExpression(node) => Some(node.span),
        Argument::TSSatisfiesExpression(node) => Some(node.span),
        Argument::TSTypeAssertion(node) => Some(node.span),
        Argument::TSNonNullExpression(node) => Some(node.span),
        Argument::TSInstantiationExpression(node) => Some(node.span),
        Argument::V8IntrinsicExpression(node) => Some(node.span),
        Argument::ComputedMemberExpression(node) => Some(node.span),
        Argument::StaticMemberExpression(node) => Some(node.span),
        Argument::PrivateFieldExpression(node) => Some(node.span),
    }
}

fn handler_body_is_supported_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    schema_names: &BTreeSet<String>,
) -> bool {
    let roots = function_parameter_roots(&function.params);
    if function.expression {
        return function.body.statements.len() == 1
            && function.body.statements.first().is_some_and(|statement| {
                expression_statement_is_supported_result(statement, &roots, schema_names)
            });
    }

    function.body.statements.len() == 1
        && function.body.statements.first().is_some_and(|statement| {
            return_statement_returns_supported_result(statement, &roots, schema_names)
        })
}

fn handler_body_is_supported_function(
    function: &oxc_ast::ast::Function<'_>,
    schema_names: &BTreeSet<String>,
) -> bool {
    let roots = function_parameter_roots(&function.params);
    if function.generator || function.body.is_none() {
        return false;
    }
    let Some(body) = &function.body else {
        return false;
    };
    body.statements.len() == 1
        && body.statements.first().is_some_and(|statement| {
            return_statement_returns_supported_result(statement, &roots, schema_names)
        })
}

fn return_statement_returns_supported_result(
    statement: &Statement<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    return_statement_result_call(statement)
        .is_some_and(|call| results_call_arguments_are_supported(call, allowed_roots, schema_names))
}

fn expression_statement_is_supported_result(
    statement: &Statement<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    expression_statement_result_call(statement)
        .is_some_and(|call| results_call_arguments_are_supported(call, allowed_roots, schema_names))
}

fn return_statement_result_call<'a>(
    statement: &'a Statement<'a>,
) -> Option<&'a CallExpression<'a>> {
    let Statement::ReturnStatement(return_statement) = statement else {
        return None;
    };
    let argument = return_statement.argument.as_ref()?;
    result_call(argument)
}

fn expression_statement_result_call<'a>(
    statement: &'a Statement<'a>,
) -> Option<&'a CallExpression<'a>> {
    let Statement::ExpressionStatement(expression_statement) = statement else {
        return None;
    };
    result_call(&expression_statement.expression)
}

fn result_call<'a>(expression: &'a Expression<'a>) -> Option<&'a CallExpression<'a>> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    if static_member_name(&call.callee).is_some_and(|(object, property)| {
        object == "Results" && results_helper_is_supported(property)
    }) {
        Some(call)
    } else {
        None
    }
}

fn results_helper_is_supported(property: &str) -> bool {
    matches!(
        property,
        "text"
            | "html"
            | "bytes"
            | "json"
            | "ok"
            | "created"
            | "accepted"
            | "noContent"
            | "notFound"
            | "badRequest"
            | "status"
            | "problem"
    )
}

fn results_call_arguments_are_supported(
    call: &CallExpression<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    let Some((object, property)) = static_member_name(&call.callee) else {
        return false;
    };
    if object != "Results" {
        return false;
    }

    let argument_count_supported = match property {
        "text" | "html" | "bytes" => matches!(call.arguments.len(), 1 | 2),
        "json" | "ok" | "accepted" | "notFound" | "badRequest" => call.arguments.len() <= 2,
        "created" | "status" => (1..=3).contains(&call.arguments.len()),
        "noContent" => call.arguments.is_empty(),
        "problem" => call.arguments.len() <= 2,
        _ => false,
    };

    if property == "bytes" {
        return matches!(call.arguments.len(), 1 | 2)
            && call
                .arguments
                .first()
                .is_some_and(argument_is_inline_bytes_value)
            && call.arguments.get(1).is_none_or(|argument| {
                argument_is_inline_json_safe_value(argument, allowed_roots, schema_names)
            });
    }

    argument_count_supported
        && call.arguments.iter().all(|argument| {
            argument_is_inline_json_safe_value(argument, allowed_roots, schema_names)
        })
}

fn argument_is_inline_bytes_value(argument: &Argument<'_>) -> bool {
    match argument {
        Argument::NewExpression(expression) => {
            matches!(&expression.callee, Expression::Identifier(identifier) if identifier.name == "Uint8Array")
                && expression.arguments.len() == 1
                && expression
                    .arguments
                    .first()
                    .is_some_and(argument_is_inline_byte_array)
        }
        Argument::ParenthesizedExpression(parenthesized) => {
            expression_is_inline_bytes_value(&parenthesized.expression)
        }
        _ => false,
    }
}

fn expression_is_inline_bytes_value(expression: &Expression<'_>) -> bool {
    match expression {
        Expression::NewExpression(expression) => {
            matches!(&expression.callee, Expression::Identifier(identifier) if identifier.name == "Uint8Array")
                && expression.arguments.len() == 1
                && expression
                    .arguments
                    .first()
                    .is_some_and(argument_is_inline_byte_array)
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_is_inline_bytes_value(&parenthesized.expression)
        }
        _ => false,
    }
}

fn argument_is_inline_byte_array(argument: &Argument<'_>) -> bool {
    match argument {
        Argument::ArrayExpression(array) => {
            array.elements.iter().all(array_element_is_byte_literal)
        }
        Argument::ParenthesizedExpression(parenthesized) => {
            expression_is_inline_byte_array(&parenthesized.expression)
        }
        _ => false,
    }
}

fn expression_is_inline_byte_array(expression: &Expression<'_>) -> bool {
    match expression {
        Expression::ArrayExpression(array) => {
            array.elements.iter().all(array_element_is_byte_literal)
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_is_inline_byte_array(&parenthesized.expression)
        }
        _ => false,
    }
}

fn array_element_is_byte_literal(element: &ArrayExpressionElement<'_>) -> bool {
    let ArrayExpressionElement::NumericLiteral(literal) = element else {
        return false;
    };
    literal.value.is_finite()
        && literal.value.fract() == 0.0
        && literal.value >= 0.0
        && literal.value <= 255.0
}

fn argument_is_inline_json_safe_value(
    argument: &Argument<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    match argument {
        Argument::StringLiteral(_)
        | Argument::NumericLiteral(_)
        | Argument::BooleanLiteral(_)
        | Argument::NullLiteral(_) => true,
        Argument::ArrayExpression(array) => array.elements.iter().all(|element| {
            array_element_is_inline_json_safe_value(element, allowed_roots, schema_names)
        }),
        Argument::ObjectExpression(object) => {
            object.properties.iter().all(|property| match property {
                ObjectPropertyKind::ObjectProperty(property) => {
                    property.kind == PropertyKind::Init
                        && !property.method
                        && !property.shorthand
                        && !property.computed
                        && property_key_is_inline_json_safe(&property.key)
                        && expression_is_inline_json_safe_value(
                            &property.value,
                            allowed_roots,
                            schema_names,
                        )
                }
                ObjectPropertyKind::SpreadProperty(_) => false,
            })
        }
        Argument::StaticMemberExpression(member) => {
            static_member_root_name(&member.object).is_some_and(|root| allowed_roots.contains(root))
        }
        Argument::CallExpression(call) => {
            body_binding_call_is_supported(call, allowed_roots, schema_names)
        }
        Argument::ParenthesizedExpression(parenthesized) => expression_is_inline_json_safe_value(
            &parenthesized.expression,
            allowed_roots,
            schema_names,
        ),
        _ => false,
    }
}

fn array_element_is_inline_json_safe_value(
    element: &ArrayExpressionElement<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    match element {
        ArrayExpressionElement::StringLiteral(_)
        | ArrayExpressionElement::NumericLiteral(_)
        | ArrayExpressionElement::BooleanLiteral(_)
        | ArrayExpressionElement::NullLiteral(_) => true,
        ArrayExpressionElement::ArrayExpression(array) => array.elements.iter().all(|element| {
            array_element_is_inline_json_safe_value(element, allowed_roots, schema_names)
        }),
        ArrayExpressionElement::ObjectExpression(object) => {
            object.properties.iter().all(|property| match property {
                ObjectPropertyKind::ObjectProperty(property) => {
                    property.kind == PropertyKind::Init
                        && !property.method
                        && !property.shorthand
                        && !property.computed
                        && property_key_is_inline_json_safe(&property.key)
                        && expression_is_inline_json_safe_value(
                            &property.value,
                            allowed_roots,
                            schema_names,
                        )
                }
                ObjectPropertyKind::SpreadProperty(_) => false,
            })
        }
        _ => false,
    }
}

fn expression_is_inline_json_safe_value(
    expression: &Expression<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    match expression {
        Expression::StringLiteral(_)
        | Expression::NumericLiteral(_)
        | Expression::BooleanLiteral(_)
        | Expression::NullLiteral(_) => true,
        Expression::ArrayExpression(array) => array.elements.iter().all(|element| {
            array_element_is_inline_json_safe_value(element, allowed_roots, schema_names)
        }),
        Expression::ObjectExpression(object) => {
            object.properties.iter().all(|property| match property {
                ObjectPropertyKind::ObjectProperty(property) => {
                    property.kind == PropertyKind::Init
                        && !property.method
                        && !property.shorthand
                        && !property.computed
                        && property_key_is_inline_json_safe(&property.key)
                        && expression_is_inline_json_safe_value(
                            &property.value,
                            allowed_roots,
                            schema_names,
                        )
                }
                ObjectPropertyKind::SpreadProperty(_) => false,
            })
        }
        Expression::ParenthesizedExpression(parenthesized) => expression_is_inline_json_safe_value(
            &parenthesized.expression,
            allowed_roots,
            schema_names,
        ),
        Expression::CallExpression(call) => {
            body_binding_call_is_supported(call, allowed_roots, schema_names)
        }
        Expression::StaticMemberExpression(member) => {
            static_member_root_name(&member.object).is_some_and(|root| allowed_roots.contains(root))
        }
        _ => false,
    }
}

fn function_parameter_roots(parameters: &oxc_ast::ast::FormalParameters<'_>) -> BTreeSet<String> {
    let mut roots = BTreeSet::new();
    for parameter in &parameters.items {
        collect_binding_roots(&parameter.pattern, &mut roots);
    }
    roots
}

fn collect_binding_roots(binding: &BindingPattern<'_>, roots: &mut BTreeSet<String>) {
    match binding {
        BindingPattern::BindingIdentifier(identifier) => {
            roots.insert(identifier.name.as_str().to_string());
        }
        BindingPattern::ObjectPattern(pattern) => {
            for property in &pattern.properties {
                collect_binding_roots(&property.value, roots);
            }
            if let Some(rest) = &pattern.rest {
                collect_binding_roots(&rest.argument, roots);
            }
        }
        BindingPattern::ArrayPattern(pattern) => {
            for element in pattern.elements.iter().flatten() {
                collect_binding_roots(element, roots);
            }
            if let Some(rest) = &pattern.rest {
                collect_binding_roots(&rest.argument, roots);
            }
        }
        BindingPattern::AssignmentPattern(pattern) => {
            collect_binding_roots(&pattern.left, roots);
        }
    }
}

fn static_member_root_name<'a>(expression: &'a Expression<'a>) -> Option<&'a str> {
    match expression {
        Expression::Identifier(identifier) => Some(identifier.name.as_str()),
        Expression::StaticMemberExpression(member) => static_member_root_name(&member.object),
        Expression::ParenthesizedExpression(parenthesized) => {
            static_member_root_name(&parenthesized.expression)
        }
        _ => None,
    }
}

fn property_key_is_inline_json_safe(key: &PropertyKey<'_>) -> bool {
    matches!(
        key,
        PropertyKey::StaticIdentifier(_)
            | PropertyKey::StringLiteral(_)
            | PropertyKey::NumericLiteral(_)
    )
}

fn source_slice(source: &str, span: Span) -> Option<String> {
    let start = usize::try_from(span.start).ok()?;
    let end = usize::try_from(span.end).ok()?;
    source.get(start..end).map(ToOwned::to_owned)
}

fn join_route_patterns(prefix: &str, child: &str) -> String {
    if prefix == "/" {
        if child.starts_with('/') {
            child.to_string()
        } else {
            format!("/{child}")
        }
    } else if child == "/" {
        prefix.to_string()
    } else if child.starts_with('/') {
        format!("{prefix}{child}")
    } else {
        format!("{prefix}/{child}")
    }
}

fn write_artifacts(out_dir: &Path, app: &ExtractedApp) -> Result<(), Diagnostic> {
    validate_output_dir(out_dir)?;
    fs::create_dir_all(out_dir).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_OUTPUT",
            format!("failed to create output directory: {error}"),
        )
        .with_path(out_dir)
    })?;

    let app_js = emit_app_js(app);
    let source_map = emit_source_map(app, &app_js);
    let plan = emit_plan(app, &sha256_hex(&app_js.source), &sha256_hex(&source_map))?;

    write_artifact(out_dir, "app.js", &app_js.source)?;
    write_artifact(out_dir, "app.js.map", &source_map)?;
    write_artifact(out_dir, "app.plan.json", &plan)?;
    Ok(())
}

fn write_artifact(out_dir: &Path, name: &str, contents: &str) -> Result<(), Diagnostic> {
    let temp_name = format!("{name}.tmp");
    let temp_path = out_dir.join(&temp_name);
    let final_path = out_dir.join(name);
    fs::write(&temp_path, contents).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_OUTPUT",
            format!("failed to write {temp_name}: {error}"),
        )
        .with_path(out_dir)
    })?;
    if final_path.exists() {
        fs::remove_file(&final_path).map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_OUTPUT",
                format!("failed to replace {name}: {error}"),
            )
            .with_path(out_dir)
        })?;
    }
    fs::rename(&temp_path, &final_path).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_OUTPUT",
            format!("failed to finalize {name}: {error}"),
        )
        .with_path(out_dir)
    })
}

fn validate_output_dir(out_dir: &Path) -> Result<(), Diagnostic> {
    if out_dir.as_os_str().is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_INVALID_OUTPUT",
            "output directory must not be empty",
        ));
    }

    for component in out_dir.components() {
        if matches!(component, std::path::Component::ParentDir) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_INVALID_OUTPUT",
                "output directory must not contain '..'",
            )
            .with_path(out_dir));
        }
    }
    Ok(())
}

fn sha256_hex(contents: &str) -> String {
    let digest = Sha256::digest(contents.as_bytes());
    let mut output = String::with_capacity("sha256:".len() + 64);
    output.push_str("sha256:");
    for byte in digest {
        output.push_str(&format!("{byte:02x}"));
    }
    output
}

fn completeness_json(completeness: &Completeness) -> Value {
    json!({
        "status": completeness.status.as_str(),
        "reasons": completeness
            .reasons
            .iter()
            .map(|reason| {
                json!({
                    "code": reason.code,
                    "message": reason.message
                })
            })
            .collect::<Vec<_>>()
    })
}

fn emit_plan(
    app: &ExtractedApp,
    bundle_hash: &str,
    source_map_hash: &str,
) -> Result<String, Diagnostic> {
    let has_async_handlers = app.routes.iter().any(|route| route.handler.is_async);
    let emits_app_metadata = !app.schemas.is_empty() || !app.config_reads.is_empty();
    let emits_metadata = emits_app_metadata
        || app.routes.iter().any(|route| {
            !route.handler.bindings.is_empty()
                || route.handler.response.is_some()
                || !route.handler.responses.is_empty()
                || route.handler.runtime_deferred
                || !route.handler.effects.is_empty()
        });
    let handlers = app
        .routes
        .iter()
        .enumerate()
        .map(|(index, route)| {
            let id = index + 1;
            let (line, column) = line_column(&route.handler.source_text, route.handler.span.start);
            let mut handler = json!({
                "async": route.handler.is_async,
                "id": id,
                "exportName": format!("__sloppy_handler_{id}"),
                "displayName": route.name.clone().unwrap_or_else(|| format!("{} {}", route.method, route.pattern)),
                "source": {
                    "path": route.handler.source_name,
                    "line": line,
                    "column": column
                }
            });
            if route.handler.runtime_deferred {
                handler["runtimeDispatch"] = json!("deferred");
            }
            handler
        })
        .collect::<Vec<_>>();

    let route_completeness_values = app
        .routes
        .iter()
        .map(|route| {
            route_completeness(&RouteCompletenessInput {
                has_response_metadata: route.handler.response.is_some(),
                body_json_without_schema: route
                    .handler
                    .bindings
                    .iter()
                    .any(|binding| binding.kind == "body.json" && binding.schema.is_none()),
                missing_provider_registration: route.handler.effects.iter().any(|effect| {
                    !app.capabilities.iter().any(|capability| {
                        capability.token == effect.provider
                            && capability.capability_kind == effect.capability_kind
                            && capability.provider == effect.provider_kind
                    })
                }),
                runtime_only: route.handler.runtime_deferred,
            })
        })
        .collect::<Vec<_>>();
    let app_completeness = plan_completeness(&route_completeness_values);

    let routes = app
        .routes
        .iter()
        .enumerate()
        .map(|(index, route)| {
            let id = index + 1;
            let (line, column) = line_column(&route.source, route.span.start);
            let completeness = &route_completeness_values[index];
            let mut route_json = json!({
                "method": route.method,
                "pattern": route.pattern,
                "handlerId": id,
                "name": route.name,
                "source": {
                    "path": route.source_name,
                    "line": line,
                    "column": column
                }
            });
            if let Some(module) = &route.module {
                route_json["module"] = json!(module);
            }
            if let Some(framework_path) = &route.framework_path {
                route_json["frameworkPath"] = json!(framework_path);
            }
            if route.handler.runtime_deferred {
                let route_params = route_parameter_names(
                    route.framework_path.as_deref().unwrap_or(&route.pattern),
                )
                .into_iter()
                .map(|name| json!({ "name": name }))
                .collect::<Vec<_>>();
                if !route_params.is_empty() {
                    route_json["routeParams"] = json!(route_params);
                }
            }
            let emits_route_metadata = emits_app_metadata
                || !route.handler.bindings.is_empty()
                || route.handler.response.is_some()
                || !route.handler.responses.is_empty()
                || !route.handler.effects.is_empty();
            if !route.handler.bindings.is_empty() {
                route_json["bindings"] = json!(route
                    .handler
                    .bindings
                    .iter()
                    .map(|binding| {
                        let mut binding_json = json!({
                            "kind": binding.kind,
                            "name": binding.name,
                            "schema": binding.schema
                        });
                        if let Some(parameter) = &binding.parameter {
                            binding_json["parameter"] = json!(parameter);
                        }
                        if let Some(type_name) = &binding.type_name {
                            binding_json["type"] = json!(type_name);
                        }
                        if let Some(wrapper) = &binding.wrapper {
                            binding_json["wrapper"] = json!(wrapper);
                        }
                        if let Some(kind) = &binding.injection_kind {
                            binding_json["injectionKind"] = json!(kind);
                        }
                        if let Some(provider_kind) = &binding.provider_kind {
                            binding_json["providerKind"] = json!(provider_kind);
                        }
                        if let Some(capability) = &binding.capability {
                            binding_json["capability"] = json!(capability);
                        }
                        if let Some(semantic) = &binding.semantic {
                            binding_json["semantic"] = json!(semantic);
                        }
                        if binding.redacted {
                            binding_json["secret"] = json!(true);
                            binding_json["redaction"] = json!("secret");
                        }
                        if let Some(span) = binding.span {
                            binding_json["source"] = source_location_json(
                                binding
                                    .source_name
                                    .as_deref()
                                    .unwrap_or(&route.handler.source_name),
                                binding
                                    .source_text
                                    .as_deref()
                                    .unwrap_or(&route.handler.source_text),
                                span,
                            );
                        }
                        binding_json
                    })
                    .collect::<Vec<_>>());
                let injections = route
                    .handler
                    .bindings
                    .iter()
                    .filter(|binding| binding.injection_kind.is_some())
                    .map(|binding| {
                        json!({
                            "kind": binding.injection_kind,
                            "providerKind": binding.provider_kind,
                            "name": binding.name,
                            "parameter": binding.parameter,
                            "capability": binding.capability
                        })
                    })
                    .collect::<Vec<_>>();
                if !injections.is_empty() {
                    route_json["injections"] = json!(injections);
                }
            }
            if emits_route_metadata {
                if let Some(response) = &route.handler.response {
                    let mut response_json = json!({
                        "helper": response.helper,
                        "status": response.status,
                        "kind": response.kind
                    });
                    if response.body_schema.is_some() {
                        response_json["bodySchema"] = json!(response.body_schema);
                    }
                    if response.partial {
                        response_json["partial"] = json!(true);
                    }
                    route_json["response"] = response_json;
                }
                if route.handler.responses.len() > 1 || route.handler.runtime_deferred {
                    route_json["responses"] = json!(route
                        .handler
                        .responses
                        .iter()
                        .map(|response| {
                            let mut value = json!({
                                "helper": response.helper,
                                "status": response.status,
                                "kind": response.kind,
                                "bodySchema": response.body_schema,
                                "partial": response.partial
                            });
                            if let (Some(source_name), Some(source_text), Some(span)) =
                                (&response.source_name, &response.source_text, response.span)
                            {
                                value["source"] =
                                    source_location_json(source_name, source_text, span);
                            }
                            value
                        })
                        .collect::<Vec<_>>());
                }
            }
            if !route.handler.effects.is_empty() {
                route_json["effects"] = json!(route
                    .handler
                    .effects
                    .iter()
                    .map(|effect| {
                        json!({
                            "provider": effect.provider,
                            "capabilityKind": effect.capability_kind,
                            "providerKind": effect.provider_kind,
                            "access": effect.access,
                            "operation": effect.operation,
                            "reason": effect.reason,
                            "source": source_location_json(
                                &effect.source_name,
                                &effect.source_text,
                                effect.span
                            )
                        })
                    })
                    .collect::<Vec<_>>());
            }
            route_json["completeness"] = completeness_json(completeness);
            route_json
        })
        .collect::<Vec<_>>();

    let modules = app
        .modules
        .iter()
        .map(|module| {
            json!({
                "name": module.name,
                "source": {
                    "path": module.source_name
                }
            })
        })
        .collect::<Vec<_>>();

    let source_files = app
        .source_files
        .iter()
        .map(|source_file| {
            json!({
                "path": source_file.name,
                "hash": sha256_hex(&source_file.source)
            })
        })
        .collect::<Vec<_>>();

    let data_provider_capabilities = app
        .capabilities
        .iter()
        .filter(|capability| capability.capability_kind == "database")
        .collect::<Vec<_>>();

    let data_providers = data_provider_capabilities
        .iter()
        .map(|capability| {
            let mut provider = json!({
                "token": capability.token,
                "capabilityKind": capability.capability_kind,
                "providerKind": capability.provider,
                "provider": capability.provider,
                "capability": capability.token,
                "service": null,
                "source": source_location_json(
                    &capability.source_name,
                    &capability.source,
                    capability.span
                )
            });
            if let Some(database) = &capability.database {
                provider["database"] = json!(database);
            }
            provider
        })
        .collect::<Vec<_>>();

    let capabilities = app
        .capabilities
        .iter()
        .map(|capability| {
            json!({
                "token": capability.token,
                "kind": capability.capability_kind,
                "access": capability.access,
                "provider": capability.token,
                "source": source_location_json(
                    &capability.source_name,
                    &capability.source,
                    capability.span
                )
            })
        })
        .collect::<Vec<_>>();

    let configuration = app.configuration.as_ref().map(|configuration| {
        let keys = configuration
            .keys
            .iter()
            .map(|key| {
                json!({
                    "key": key.key,
                    "source": key.source,
                    "value": key.value,
                    "sensitive": key.sensitive
                })
            })
            .collect::<Vec<_>>();
        let providers = configuration
            .providers
            .iter()
            .map(|provider| {
                json!({
                    "provider": provider.provider,
                    "name": provider.name,
                    "prefix": provider.prefix,
                    "source": provider.source
                })
            })
            .collect::<Vec<_>>();
        let requirements = configuration
            .requirements
            .iter()
            .map(|requirement| {
                let mut value = json!({
                    "key": requirement.key,
                    "type": requirement.value_type,
                    "required": requirement.required,
                    "status": requirement.status,
                    "source": requirement.source,
                    "requiredBy": requirement.required_by,
                    "redaction": if requirement.sensitive { "secret" } else { "none" },
                    "secret": requirement.sensitive
                });
                if let Some(default_value) = &requirement.default_value {
                    value["default"] = if requirement.sensitive {
                        json!("<redacted>")
                    } else {
                        default_value.clone()
                    };
                }
                value
            })
            .collect::<Vec<_>>();
        let package_required = configuration
            .package_manifest
            .required
            .iter()
            .map(package_manifest_entry_json)
            .collect::<Vec<_>>();
        let package_optional = configuration
            .package_manifest
            .optional
            .iter()
            .map(package_manifest_entry_json)
            .collect::<Vec<_>>();
        let mut configuration_json = json!({
            "environment": configuration.environment,
            "keys": keys,
            "providers": providers
        });
        if !requirements.is_empty() {
            configuration_json["requirements"] = json!(requirements);
        }
        if !package_required.is_empty() || !package_optional.is_empty() {
            configuration_json["packageManifest"] = json!({
                "required": package_required,
                "optional": package_optional
            });
        }
        configuration_json
    });

    let schemas = app
        .schemas
        .iter()
        .map(|schema| {
            let (line, column) = line_column(&schema.source, schema.span.start);
            json!({
                "name": schema.name,
                "definition": schema.definition,
                "source": {
                    "path": schema.source_name,
                    "line": line,
                    "column": column
                }
            })
        })
        .collect::<Vec<_>>();

    let config_reads = app
        .config_reads
        .iter()
        .map(|read| {
            let (line, column) = line_column(&read.source, read.span.start);
            json!({
                "key": read.key,
                "type": read.value_type,
                "hasDefault": read.has_default,
                "required": read.required,
                "secret": read.sensitive,
                "source": {
                    "path": read.source_name,
                    "line": line,
                    "column": column
                }
            })
        })
        .collect::<Vec<_>>();

    let mut value = json!({
        "schemaVersion": 1,
        "compilerVersion": COMPILER_VERSION,
        "runtimeMinimumVersion": RUNTIME_MINIMUM_VERSION,
        "stdlibVersion": STDLIB_VERSION,
        "target": {
            "platform": "windows-x64",
            "engine": "v8"
        },
        "bundle": {
            "path": "app.js",
            "id": "sloppyc-app-js",
            "hash": bundle_hash
        },
        "sourceMap": {
            "path": "app.js.map",
            "id": "sloppyc-app-js-map",
            "hash": source_map_hash
        },
        "handlers": handlers,
        "routes": routes,
        "modules": modules,
        "sourceFiles": source_files,
        "dataProviders": data_providers,
        "capabilities": capabilities,
        "completeness": completeness_json(&app_completeness),
        "strongPlan": {
            "version": 1,
            "profile": "compiler-30-strong-plan",
            "compatibility": {
                "schemaVersion": 1,
                "unknownOptionalFields": "ignored",
                "unknownRequiredFeatures": "rejected"
            },
            "evidence": {
                "sourceGraph": true,
                "routes": true,
                "providers": !data_providers.is_empty(),
                "bindings": app.routes.iter().any(|route| !route.handler.bindings.is_empty()),
                "effects": app.routes.iter().any(|route| !route.handler.effects.is_empty()),
                "capabilities": !app.capabilities.is_empty()
            }
        },
        "features": {
            "asyncHandlers": has_async_handlers,
            "dataProviders": !data_providers.is_empty(),
            "capabilities": !app.capabilities.is_empty(),
            "strongPlanMetadata": true,
            "sourceMaps": true
        }
    });
    let mut required_features = Vec::new();
    let mut doctor_checks = Vec::new();
    if app.uses_time_runtime {
        required_features.push("stdlib.time");
        value["strongPlan"]["evidence"]["time"] = json!(true);
        value["features"]["time"] = json!(true);
    }
    if app.uses_fs_runtime {
        required_features.push("stdlib.fs");
        value["strongPlan"]["evidence"]["filesystem"] = json!(true);
        value["features"]["fileSystem"] = json!(true);
    }
    if app.uses_crypto_runtime {
        required_features.push("stdlib.crypto");
        value["strongPlan"]["evidence"]["crypto"] = json!(true);
        value["features"]["crypto"] = json!(true);
    }
    if app.uses_codec_runtime {
        required_features.push("stdlib.codec");
        value["strongPlan"]["evidence"]["codec"] = json!(true);
        value["features"]["codec"] = json!(true);
    }
    if app.uses_crypto_runtime && app.noncrypto_hash_security_context_visible {
        value["strongPlan"]["evidence"]["nonCryptoHashSecurityContext"] = json!(true);
        doctor_checks.push(json!({
            "id": "stdlib.crypto.noncrypto_hash.security_context",
            "status": "warn",
            "message": "NonCryptoHash.xxHash64 is visible in a security-looking context; use Password, Hash, or Hmac for security or attacker-resistance"
        }));
    }
    if app.uses_codec_runtime && app.checksum_security_context_visible {
        value["strongPlan"]["evidence"]["checksumSecurityContext"] = json!(true);
        doctor_checks.push(json!({
            "id": "stdlib.codec.checksum.security_context",
            "status": "warn",
            "message": "Checksums.crc32 is visible in a security-looking context; use Hash or Hmac for security or attacker-resistance"
        }));
    }
    if app.uses_net_runtime {
        required_features.push("stdlib.net");
        value["strongPlan"]["evidence"]["network"] = json!(true);
        value["features"]["network"] = json!(true);
    }
    if app.uses_os_runtime {
        required_features.push("stdlib.os");
        value["strongPlan"]["evidence"]["os"] = json!(true);
        value["features"]["os"] = json!(true);
    }
    if app.uses_http_client_runtime {
        required_features.push("stdlib.httpclient");
        value["strongPlan"]["evidence"]["httpClient"] = json!(true);
        value["features"]["httpClient"] = json!(true);
        doctor_checks.push(json!({
            "id": "stdlib.httpclient.contract",
            "status": "warn",
            "message": "HttpClient is Plan-visible; default compiler metadata does not infer static outbound targets yet"
        }));
    }
    if app.uses_workers_runtime {
        required_features.push("stdlib.workers");
        value["strongPlan"]["evidence"]["workers"] = json!(true);
        value["features"]["workers"] = json!(true);
    }
    if !required_features.is_empty() {
        value["requiredFeatures"] = json!(required_features);
    }
    if !doctor_checks.is_empty() {
        value["doctorChecks"] = json!(doctor_checks);
    }
    if let Some(configuration) = configuration {
        value["configuration"] = configuration;
    }
    if !schemas.is_empty() {
        value["schemas"] = json!(schemas);
    }
    if !config_reads.is_empty() {
        value["configReads"] = json!(config_reads);
    }
    if emits_metadata {
        value["features"]["metadataInference"] = json!(true);
    }

    serde_json::to_string_pretty(&value)
        .map(|json| format!("{json}\n"))
        .map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_EMIT",
                format!("failed to emit app.plan.json: {error}"),
            )
        })
}

fn source_location_json(source_name: &str, source: &str, span: Span) -> Value {
    let (line, column) = line_column(source, span.start);
    json!({
        "path": source_name,
        "line": line,
        "column": column
    })
}

fn emit_app_js(app: &ExtractedApp) -> EmittedAppJs {
    let mut output = String::new();
    let mut mappings = Vec::new();
    let mut handler_generated_starts = Vec::new();
    let mut generated_line = 0usize;
    let needs_provider_open_helper = app
        .routes
        .iter()
        .any(|route| route.handler.source.contains("__sloppy_open_data_provider"));
    let needs_framework_arg_helper = app
        .routes
        .iter()
        .any(|route| route.handler.source.contains("__sloppy_framework_arg"));
    let needs_framework_services =
        needs_framework_arg_helper || !app.service_registrations.is_empty();

    push_generated_line(
        &mut output,
        &mut generated_line,
        "const __sloppyRuntime = globalThis.__sloppy_runtime;",
    );
    push_generated_line(
        &mut output,
        &mut generated_line,
        "if (__sloppyRuntime === undefined) {",
    );
    push_generated_line(
        &mut output,
        &mut generated_line,
        "  throw new Error(\"Sloppy bootstrap runtime was not loaded\");",
    );
    push_generated_line(&mut output, &mut generated_line, "}");
    let mut runtime_exports = vec!["Results"];
    if app.uses_data_runtime {
        runtime_exports.push("data");
    }
    if app.uses_sql_runtime {
        runtime_exports.push("sql");
    }
    if app.uses_time_runtime {
        runtime_exports.extend([
            "Time",
            "Deadline",
            "CancellationController",
            "TimeoutError",
            "CancelledError",
            "InvalidDeadlineError",
            "TimerDisposedError",
        ]);
    }
    if app.uses_fs_runtime {
        runtime_exports.extend(["File", "Directory", "Path", "FileHandle", "FileWatcher"]);
    }
    if app.uses_crypto_runtime {
        runtime_exports.extend([
            "Random",
            "Hash",
            "Hmac",
            "Password",
            "ConstantTime",
            "Secret",
            "NonCryptoHash",
        ]);
    }
    if app.uses_codec_runtime {
        runtime_exports.extend(CODEC_EXPORTS.iter().copied());
    }
    if app.uses_net_runtime {
        runtime_exports.extend([
            "TcpClient",
            "TcpListener",
            "TcpConnection",
            "NetworkAddress",
        ]);
    }
    if app.uses_os_runtime {
        runtime_exports.extend(["System", "Environment", "Process", "Signals"]);
    }
    if app.uses_http_client_runtime {
        runtime_exports.push("HttpClient");
    }
    if app.uses_workers_runtime {
        runtime_exports.extend(WORKER_EXPORTS.iter().copied());
    }
    push_generated_line(
        &mut output,
        &mut generated_line,
        &format!(
            "const {{ {} }} = __sloppyRuntime;",
            runtime_exports.join(", ")
        ),
    );
    if needs_framework_services {
        for line in crate::framework_runtime::FRAMEWORK_SERVICE_RUNTIME.lines() {
            push_generated_line(&mut output, &mut generated_line, line);
        }
        for registration in &app.service_registrations {
            let token =
                serde_json::to_string(&registration.token).unwrap_or_else(|_| "\"\"".to_string());
            let method = match registration.lifetime {
                "singleton" => "addSingleton",
                "scoped" => "addScoped",
                "transient" => "addTransient",
                _ => "addTransient",
            };
            push_generated_line(
                &mut output,
                &mut generated_line,
                &format!(
                    "__sloppy_framework_services.{method}({token}, {});",
                    registration.factory_source
                ),
            );
        }
    }
    if app.uses_data_runtime && needs_provider_open_helper {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_open_data_provider(kind, token) {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (kind === \"sqlite\") { return data.sqlite(token); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  throw new Error(`sloppy: ${kind} provider bridge unavailable`);",
        );
        push_generated_line(&mut output, &mut generated_line, "}");
    }
    if needs_framework_arg_helper {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_framework_arg(ctx, scope, binding) {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.kind === \"body.json\") { return ctx.request.json(); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.kind === \"context\") { return ctx; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.kind === \"injection\") { return __sloppy_framework_injection(scope, binding); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.kind === \"config\") { throw new Error(`sloppy: Config injection for '${binding.name}' is unavailable in this runtime lane.`); }",
        );
        push_generated_line(&mut output, &mut generated_line, "  let value;");
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.kind === \"route\") { value = ctx.route[binding.name]; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  else if (binding.kind === \"query\") { value = ctx.query[binding.name]; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  else if (binding.kind === \"header\") { value = ctx.request.headers.get(binding.name); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  else { throw new TypeError(`Sloppy Framework binding kind '${binding.kind}' is not supported.`); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  return __sloppy_framework_coerce(value, binding);",
        );
        push_generated_line(&mut output, &mut generated_line, "}");
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_framework_coerce(value, binding) {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (value === null || value === undefined) { return value; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  const type = String(binding.type || binding.schema || \"\");",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (type.includes(\"boolean\")) {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    const normalized = String(value).toLowerCase();",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    if (normalized === \"true\") { return true; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    if (normalized === \"false\") { return false; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected a boolean value.`);",
        );
        push_generated_line(&mut output, &mut generated_line, "  }");
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (type.includes(\"number\") || type.includes(\"PositiveInt\") || type === \"int\") {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    const parsed = Number(value);",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    if (!Number.isFinite(parsed)) { throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected a numeric value.`); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    if ((type.includes(\"PositiveInt\") || type === \"int\") && !Number.isInteger(parsed)) { throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected an integer value.`); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    if (type.includes(\"PositiveInt\") && parsed <= 0) { throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected a positive integer value.`); }",
        );
        push_generated_line(&mut output, &mut generated_line, "    return parsed;");
        push_generated_line(&mut output, &mut generated_line, "  }");
        push_generated_line(&mut output, &mut generated_line, "  return value;");
        push_generated_line(&mut output, &mut generated_line, "}");
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_framework_injection(scope, binding) {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  const dependencyName = binding.capability || (binding.name && binding.name.includes(\".\") ? binding.name : `data.${binding.name}`);",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.injectionKind === \"service\") { return scope.get(binding.name); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.injectionKind === \"queue\") { return scope.get(dependencyName); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.injectionKind === \"provider\" && binding.providerKind === \"sqlite\") { return scope.track(data.sqlite(dependencyName)); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.injectionKind === \"provider\" && binding.providerKind === \"postgres\") { return scope.track(data.postgres.open({ provider: dependencyName })); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.injectionKind === \"provider\" && binding.providerKind === \"sqlserver\") { return scope.track(data.sqlserver.open({ provider: dependencyName })); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  throw new Error(`sloppy: ${binding.injectionKind} injection for '${binding.name}' is unavailable in this runtime lane.`);",
        );
        push_generated_line(&mut output, &mut generated_line, "}");
    }
    push_generated_line(&mut output, &mut generated_line, "");

    for helper_source in &app.helper_sources {
        push_generated_source(&mut output, &mut generated_line, helper_source);
    }
    if !app.helper_sources.is_empty() {
        push_generated_line(&mut output, &mut generated_line, "");
    }

    for (index, route) in app.routes.iter().enumerate() {
        let id = index + 1;
        let prefix = format!("globalThis.__sloppy_handler_{id} = ");
        let handler_start_line = generated_line;
        let handler_start_column = prefix.len();
        handler_generated_starts.push(HandlerGeneratedStart {
            handler_id: id,
            generated_line: handler_start_line,
            generated_column: handler_start_column,
        });
        mappings.extend(handler_source_mappings(
            &route.handler.source_text,
            route.handler.span,
            &route.handler.source,
            handler_start_line,
            handler_start_column,
            source_index_for(app, &route.handler.source_name),
        ));

        output.push_str(&prefix);
        output.push_str(&route.handler.source);
        output.push_str(";\n");
        generated_line += route.handler.source.matches('\n').count() + 1;
        push_generated_line(
            &mut output,
            &mut generated_line,
            &format!(
                "globalThis.__sloppy_register_handler({id}, globalThis.__sloppy_handler_{id});"
            ),
        );
    }

    EmittedAppJs {
        source: output,
        mappings,
        handler_generated_starts,
    }
}

fn emit_source_map(app: &ExtractedApp, emitted_js: &EmittedAppJs) -> String {
    let mappings = &emitted_js.mappings;
    let sources = app
        .source_files
        .iter()
        .map(|file| file.name.clone())
        .collect::<Vec<_>>();
    let sources_content = app
        .source_files
        .iter()
        .map(|file| file.source.clone())
        .collect::<Vec<_>>();
    let source_files = app
        .source_files
        .iter()
        .map(|file| {
            json!({
                "path": file.name,
                "hash": sha256_hex(&file.source)
            })
        })
        .collect::<Vec<_>>();
    let handlers = app
        .routes
        .iter()
        .enumerate()
        .map(|(index, route)| {
            let id = index + 1;
            let generated = emitted_js
                .handler_generated_starts
                .iter()
                .find(|start| start.handler_id == id)
                .map(|start| generated_location_json(start.generated_line, start.generated_column))
                .unwrap_or_else(|| json!(null));
            json!({
                "id": id,
                "generated": generated,
                "source": source_location_json(
                    &route.handler.source_name,
                    &route.handler.source_text,
                    route.handler.span
                )
            })
        })
        .collect::<Vec<_>>();
    let routes = app
        .routes
        .iter()
        .enumerate()
        .map(|(index, route)| {
            let mut route_json = json!({
                "handlerId": index + 1,
                "method": route.method,
                "pattern": route.pattern,
                "module": route.module,
                "source": source_location_json(&route.source_name, &route.source, route.span)
            });
            if let Some(framework_path) = &route.framework_path {
                route_json["frameworkPath"] = json!(framework_path);
            }
            route_json
        })
        .collect::<Vec<_>>();
    let modules = app
        .modules
        .iter()
        .map(|module| {
            json!({
                "name": module.name,
                "source": {
                    "path": module.source_name
                }
            })
        })
        .collect::<Vec<_>>();
    let schemas = app
        .schemas
        .iter()
        .map(|schema| {
            json!({
                "name": schema.name,
                "source": source_location_json(&schema.source_name, &schema.source, schema.span)
            })
        })
        .collect::<Vec<_>>();
    let providers = app
        .capabilities
        .iter()
        .map(|capability| {
            json!({
                "token": capability.token,
                "capabilityKind": capability.capability_kind,
                "providerKind": capability.provider,
                "source": source_location_json(
                    &capability.source_name,
                    &capability.source,
                    capability.span
                )
            })
        })
        .collect::<Vec<_>>();
    let capabilities = app
        .capabilities
        .iter()
        .map(|capability| {
            json!({
                "token": capability.token,
                "kind": capability.capability_kind,
                "access": capability.access,
                "source": source_location_json(
                    &capability.source_name,
                    &capability.source,
                    capability.span
                )
            })
        })
        .collect::<Vec<_>>();
    let effects = app
        .routes
        .iter()
        .enumerate()
        .flat_map(|(index, route)| {
            route.handler.effects.iter().map(move |effect| {
                json!({
                    "handlerId": index + 1,
                    "provider": effect.provider,
                    "capabilityKind": effect.capability_kind,
                    "providerKind": effect.provider_kind,
                    "access": effect.access,
                    "operation": effect.operation,
                    "reason": effect.reason,
                    "source": source_location_json(
                        &effect.source_name,
                        &effect.source_text,
                        effect.span
                    )
                })
            })
        })
        .collect::<Vec<_>>();
    let value = json!({
        "version": 3,
        "file": "app.js",
        "sources": sources,
        "sourcesContent": sources_content,
        "names": [],
        "mappings": encode_source_map_mappings(mappings),
        "x_sloppy": {
            "version": 1,
            "sourceFiles": source_files,
            "handlers": handlers,
            "routes": routes,
            "modules": modules,
            "schemas": schemas,
            "providers": providers,
            "capabilities": capabilities,
            "effects": effects
        }
    });

    let json = serde_json::to_string_pretty(&value).unwrap_or_else(|_| "{}".to_string());
    format!("{json}\n")
}

fn package_manifest_entry_json(entry: &ConfigurationPackageEntry) -> Value {
    let mut value = json!({
        "key": entry.key,
        "env": entry.env,
        "type": entry.value_type,
        "secret": entry.sensitive
    });
    if let Some(default_value) = &entry.default_value {
        value["default"] = if entry.sensitive {
            json!("<redacted>")
        } else {
            default_value.clone()
        };
    }
    value
}

fn generated_location_json(generated_line: usize, generated_column: usize) -> Value {
    json!({
        "line": generated_line + 1,
        "column": generated_column + 1
    })
}

fn push_generated_line(output: &mut String, generated_line: &mut usize, line: &str) {
    output.push_str(line);
    output.push('\n');
    *generated_line += 1;
}

fn push_generated_source(output: &mut String, generated_line: &mut usize, source: &str) {
    output.push_str(source);
    output.push('\n');
    *generated_line += source.split('\n').count();
}

fn handler_source_mappings(
    source: &str,
    span: Span,
    handler_source: &str,
    generated_start_line: usize,
    generated_start_column: usize,
    source_index: usize,
) -> Vec<SourceMapMapping> {
    let Some(source_start) = usize::try_from(span.start).ok() else {
        return Vec::new();
    };
    let mut mappings = Vec::new();
    let mut relative_start = 0usize;
    let mut generated_line_offset = 0usize;

    loop {
        let original_offset = source_start.saturating_add(relative_start);
        let (original_line, original_column) = line_column(
            source,
            span.start
                .saturating_add(u32::try_from(relative_start).unwrap_or(u32::MAX)),
        );
        mappings.push(SourceMapMapping {
            generated_line: generated_start_line + generated_line_offset,
            generated_column: if generated_line_offset == 0 {
                generated_start_column
            } else {
                0
            },
            source_index,
            original_line: original_line.saturating_sub(1),
            original_column: original_column.saturating_sub(1),
        });

        let Some(next_newline) = handler_source[relative_start..].find('\n') else {
            break;
        };
        relative_start += next_newline + 1;
        generated_line_offset += 1;
        if relative_start >= handler_source.len() || original_offset >= source.len() {
            break;
        }
    }

    mappings
}

fn source_index_for(app: &ExtractedApp, source_name: &str) -> usize {
    app.source_files
        .iter()
        .position(|file| file.name == source_name)
        .unwrap_or(0)
}

fn encode_source_map_mappings(mappings: &[SourceMapMapping]) -> String {
    if mappings.is_empty() {
        return String::new();
    }

    let mut sorted = mappings.to_vec();
    sorted.sort_by_key(|mapping| (mapping.generated_line, mapping.generated_column));
    let Some(max_line) = sorted.last().map(|mapping| mapping.generated_line) else {
        return String::new();
    };

    let mut output = String::new();
    let mut mapping_index = 0usize;
    let mut previous_source = 0i64;
    let mut previous_original_line = 0i64;
    let mut previous_original_column = 0i64;

    for line in 0..=max_line {
        if line > 0 {
            output.push(';');
        }

        let mut previous_generated_column = 0i64;
        let mut first_segment = true;
        while mapping_index < sorted.len() && sorted[mapping_index].generated_line == line {
            let mapping = &sorted[mapping_index];
            if !first_segment {
                output.push(',');
            }
            first_segment = false;

            let generated_column = usize_to_i64(mapping.generated_column);
            let source_index = usize_to_i64(mapping.source_index);
            let original_line = usize_to_i64(mapping.original_line);
            let original_column = usize_to_i64(mapping.original_column);
            output.push_str(&encode_vlq(generated_column - previous_generated_column));
            output.push_str(&encode_vlq(source_index - previous_source));
            output.push_str(&encode_vlq(original_line - previous_original_line));
            output.push_str(&encode_vlq(original_column - previous_original_column));

            previous_generated_column = generated_column;
            previous_source = source_index;
            previous_original_line = original_line;
            previous_original_column = original_column;
            mapping_index += 1;
        }
    }

    output
}

fn usize_to_i64(value: usize) -> i64 {
    i64::try_from(value).unwrap_or(i64::MAX)
}

fn encode_vlq(value: i64) -> String {
    const BASE64: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut vlq = if value < 0 {
        ((value.saturating_abs() as u64) << 1) | 1
    } else {
        (value as u64) << 1
    };
    let mut output = String::new();

    loop {
        let mut digit = (vlq & 31) as usize;
        vlq >>= 5;
        if vlq > 0 {
            digit |= 32;
        }
        output.push(char::from(BASE64[digit]));
        if vlq == 0 {
            break;
        }
    }

    output
}

trait AstSpan {
    fn span(&self) -> Span;
}

impl AstSpan for Statement<'_> {
    fn span(&self) -> Span {
        match self {
            Statement::BlockStatement(node) => node.span,
            Statement::BreakStatement(node) => node.span,
            Statement::ContinueStatement(node) => node.span,
            Statement::DebuggerStatement(node) => node.span,
            Statement::DoWhileStatement(node) => node.span,
            Statement::EmptyStatement(node) => node.span,
            Statement::ExpressionStatement(node) => node.span,
            Statement::ForInStatement(node) => node.span,
            Statement::ForOfStatement(node) => node.span,
            Statement::ForStatement(node) => node.span,
            Statement::IfStatement(node) => node.span,
            Statement::LabeledStatement(node) => node.span,
            Statement::ReturnStatement(node) => node.span,
            Statement::SwitchStatement(node) => node.span,
            Statement::ThrowStatement(node) => node.span,
            Statement::TryStatement(node) => node.span,
            Statement::WhileStatement(node) => node.span,
            Statement::WithStatement(node) => node.span,
            Statement::VariableDeclaration(node) => node.span,
            Statement::FunctionDeclaration(node) => node.span,
            Statement::ClassDeclaration(node) => node.span,
            Statement::TSTypeAliasDeclaration(node) => node.span,
            Statement::TSInterfaceDeclaration(node) => node.span,
            Statement::TSEnumDeclaration(node) => node.span,
            Statement::TSModuleDeclaration(node) => node.span,
            Statement::TSGlobalDeclaration(node) => node.span,
            Statement::TSImportEqualsDeclaration(node) => node.span,
            Statement::ImportDeclaration(node) => node.span,
            Statement::ExportAllDeclaration(node) => node.span,
            Statement::ExportDefaultDeclaration(node) => node.span,
            Statement::ExportNamedDeclaration(node) => node.span,
            Statement::TSExportAssignment(node) => node.span,
            Statement::TSNamespaceExportDeclaration(node) => node.span,
        }
    }
}

impl AstSpan for Expression<'_> {
    fn span(&self) -> Span {
        match self {
            Expression::BooleanLiteral(node) => node.span,
            Expression::NullLiteral(node) => node.span,
            Expression::NumericLiteral(node) => node.span,
            Expression::BigIntLiteral(node) => node.span,
            Expression::RegExpLiteral(node) => node.span,
            Expression::StringLiteral(node) => node.span,
            Expression::TemplateLiteral(node) => node.span,
            Expression::Identifier(node) => node.span,
            Expression::MetaProperty(node) => node.span,
            Expression::Super(node) => node.span,
            Expression::ArrayExpression(node) => node.span,
            Expression::ArrowFunctionExpression(node) => node.span,
            Expression::AssignmentExpression(node) => node.span,
            Expression::AwaitExpression(node) => node.span,
            Expression::BinaryExpression(node) => node.span,
            Expression::CallExpression(node) => node.span,
            Expression::ChainExpression(node) => node.span,
            Expression::ClassExpression(node) => node.span,
            Expression::ConditionalExpression(node) => node.span,
            Expression::FunctionExpression(node) => node.span,
            Expression::ImportExpression(node) => node.span,
            Expression::LogicalExpression(node) => node.span,
            Expression::NewExpression(node) => node.span,
            Expression::ObjectExpression(node) => node.span,
            Expression::ParenthesizedExpression(node) => node.span,
            Expression::SequenceExpression(node) => node.span,
            Expression::TaggedTemplateExpression(node) => node.span,
            Expression::ThisExpression(node) => node.span,
            Expression::UnaryExpression(node) => node.span,
            Expression::UpdateExpression(node) => node.span,
            Expression::YieldExpression(node) => node.span,
            Expression::PrivateInExpression(node) => node.span,
            Expression::JSXElement(node) => node.span,
            Expression::JSXFragment(node) => node.span,
            Expression::TSAsExpression(node) => node.span,
            Expression::TSSatisfiesExpression(node) => node.span,
            Expression::TSTypeAssertion(node) => node.span,
            Expression::TSNonNullExpression(node) => node.span,
            Expression::TSInstantiationExpression(node) => node.span,
            Expression::V8IntrinsicExpression(node) => node.span,
            Expression::ComputedMemberExpression(node) => node.span,
            Expression::StaticMemberExpression(node) => node.span,
            Expression::PrivateFieldExpression(node) => node.span,
        }
    }
}

#[cfg(test)]
mod tests {
    use std::{
        ffi::OsString,
        fs,
        path::{Path, PathBuf},
    };

    use super::{
        canonical_config_key, checksum_security_context_visible, command_from_args, extract,
        noncrypto_hash_security_context_visible, route_pattern_supported, CliCommand,
        CompileOptions,
    };

    fn fixture_temp_dir(name: &str) -> PathBuf {
        let root = std::env::temp_dir().join(format!("sloppyc-{name}-{}", std::process::id()));
        if root.exists() {
            fs::remove_dir_all(&root).expect("stale test directory should be removable");
        }
        fs::create_dir_all(&root).expect("test directory should be created");
        root
    }

    fn extract_temp_input(
        root: &Path,
        source: &str,
    ) -> Result<super::ExtractedApp, super::Diagnostic> {
        let input = root.join("input.js");
        fs::write(&input, source).expect("fixture input should be writable");
        extract(&input, source)
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
                options: CompileOptions {
                    environment: Some("Development".to_string()),
                    host: Some("127.0.0.1".to_string()),
                    port: Some(5173),
                    config_overrides: vec![("Auth:Issuer".to_string(), "cli".to_string())],
                },
            }
        );
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
            environment: Some("Development".to_string()),
            host: Some("0.0.0.0".to_string()),
            port: Some(6000),
            config_overrides: Vec::new(),
        };
        let config = super::ConfigurationModel::load(&input, &options, &[])
            .expect("configuration should load");
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
            uses_data_runtime: true,
            uses_sql_runtime: false,
            source_files: Vec::new(),
            routes: Vec::new(),
            service_registrations: Vec::new(),
            modules: Vec::new(),
            helper_sources: Vec::new(),
            capabilities: vec![super::DatabaseCapability {
                token: "data.main".to_string(),
                capability_kind: "database".to_string(),
                provider: "sqlite".to_string(),
                config_name: Some("main".to_string()),
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
            uses_net_runtime: false,
            uses_os_runtime: false,
            uses_http_client_runtime: false,
            uses_workers_runtime: false,
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
            environment: Some("Development".to_string()),
            host: None,
            port: None,
            config_overrides: vec![("Auth:Issuer".to_string(), "cli".to_string())],
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
  issuer: { key: "Jwt:Issuer", type: "string", required: true }
});
app.get("/", () => Results.text("ok"));
export default app;
"#;
        let mut app = extract(std::path::Path::new("app.js"), source)
            .expect("bind descriptors should extract");
        assert_eq!(app.config_reads.len(), 3);
        assert!(app
            .config_reads
            .iter()
            .any(|read| { read.key == "Auth:JwtSecret" && read.sensitive && read.required }));
        assert!(app.config_reads.iter().any(|read| {
            read.key == "Auth:TokenTtlMinutes" && read.has_default && !read.required
        }));
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
    fn configuration_plan_redacts_tls_passphrase_but_not_paths() {
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

        let diagnostic =
            super::ConfigurationModel::load(&input, &super::CompileOptions::new(), &[])
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
        assert!(emitted_js
            .source
            .contains("const { Results, HttpClient } = __sloppyRuntime;"));
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
        let diagnostic = extract(std::path::Path::new("app.js"), source)
            .expect_err("duplicate routes should fail");
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
        let diagnostic =
            extract_temp_input(&root, source).expect_err("wrong module shape should fail");
        assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE");

        fs::remove_dir_all(&root).expect("test directory should be removable");
    }

    #[test]
    fn rejects_dynamic_route_pattern() {
        let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const pattern = "/";
app.mapGet(pattern, () => Results.text("Hello"));
export default app;
"#;
        let diagnostic =
            extract(std::path::Path::new("app.js"), source).expect_err("dynamic route should fail");
        assert_eq!(
            diagnostic.code,
            "SLOPPYC_E_UNSUPPORTED_DYNAMIC_ROUTE_PATTERN"
        );
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
  body: ctx.body.json(UserCreate)
}));
export default app;
"#;
        let app = extract(std::path::Path::new("app.js"), source)
            .expect("metadata fixture should extract");
        assert_eq!(app.schemas.len(), 1);
        assert_eq!(app.schemas[0].name, "UserCreate");
        assert_eq!(app.config_reads.len(), 1);
        assert_eq!(app.config_reads[0].key, "Sloppy:Server:Host");
        assert_eq!(app.routes[0].handler.bindings.len(), 4);
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
        assert_eq!(plan["routes"][0]["response"]["helper"], "json");
        assert_eq!(plan["features"]["metadataInference"], true);
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
    fn extracts_route_metadata_without_runtime_claims() {
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
            .source
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
            .source
            .contains("function listUsers()"));
        assert!(app.routes[0]
            .handler
            .source
            .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
        assert!(!app.routes[0].handler.source.contains("uses"));
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
            .source
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
            .source
            .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
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
            .source
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
import { TcpClient, TcpListener, TcpConnection, NetworkAddress } from "sloppy/net";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
        let app = extract(std::path::Path::new("app.js"), source)
            .expect("sloppy/net import should be recognized");
        assert!(app.uses_net_runtime);

        let emitted_js = super::emit_app_js(&app);
        assert!(emitted_js.source.contains(
            "const { Results, TcpClient, TcpListener, TcpConnection, NetworkAddress } = __sloppyRuntime;"
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
import { System, Environment, Process, Signals } from "sloppy/os";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
        let app = extract(std::path::Path::new("app.js"), source)
            .expect("sloppy/os import should be recognized");
        assert!(app.uses_os_runtime);

        let emitted_js = super::emit_app_js(&app);
        assert!(emitted_js.source.contains(
            "const { Results, System, Environment, Process, Signals } = __sloppyRuntime;"
        ));
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
        assert!(emitted_js
            .source
            .contains("const { Results, HttpClient } = __sloppyRuntime;"));
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
        assert!(value["doctorChecks"].as_array().is_some_and(|checks| checks
            .iter()
            .any(|check| check["id"]
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

    #[test]
    fn typed_framework_metadata_fixture_expected_outputs_stay_current() {
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
        let fixture_name = "framework-v2-metadata";
        let fixture = root
            .join("tests/fixtures")
            .join(fixture_name)
            .join("input.ts");
        let source = fs::read_to_string(&fixture).expect("fixture input should exist");
        let mut app = extract(&fixture, &source).expect("framework v2 fixture should extract");
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
                .expect_err("unsupported framework v2 source should fail");
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
    fn rejects_member_expression_captures_outside_context_roots() {
        let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const config = { message: "captured" };
app.mapGet("/", (ctx) => Results.json({ message: config.message, id: ctx.route.id }));
export default app;
"#;
        let diagnostic = extract(std::path::Path::new("app.js"), source)
            .expect_err("captured member expression should fail");
        assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE");
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
            let emitted_plan = super::emit_plan(&app, &emitted_js_hash, &emitted_map_hash)
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
    fn rejected_fixture_diagnostics_stay_current() {
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
        for (fixture_name, input_name) in [
            ("unsupported-dynamic-route", "input.js"),
            ("computed-method", "input.js"),
            ("loop-route-registration", "input.js"),
            ("conditional-route-registration", "input.js"),
            ("unsupported-handler-parameter", "input.js"),
            ("unsupported-handler-capture", "input.js"),
            ("unsupported-handler-shape", "input.js"),
            ("unsupported-typescript-handler", "input.ts"),
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
            ("unsupported-dynamic-import", "input.js"),
            ("missing-relative-import", "input.js"),
            ("missing-provider-effect", "input.js"),
            ("non-sqlite-provider-bridge", "input.js"),
            ("unsupported-provider-method", "input.js"),
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
        let input = root.join("tests/fixtures/computed-method/input.js");
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
            "SLOPPYC_E_UNSUPPORTED_COMPUTED_ROUTE_METHOD"
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
        let out_dir =
            std::env::temp_dir().join(format!("sloppyc-build-test-{}", std::process::id()));

        if out_dir.exists() {
            fs::remove_dir_all(&out_dir).expect("stale test output directory should be removable");
        }

        super::build(&input, &out_dir, &CompileOptions::new())
            .expect("compiler example should build");

        let emitted_plan =
            fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be written");
        let expected_plan =
            fs::read_to_string(root.join("../examples/compiler-hello/expected/app.plan.json"))
                .expect("expected plan should exist");
        assert_eq!(emitted_plan, expected_plan);

        let emitted_js =
            fs::read_to_string(out_dir.join("app.js")).expect("app.js should be written");
        let expected_js =
            fs::read_to_string(root.join("../examples/compiler-hello/expected/app.js"))
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
}
