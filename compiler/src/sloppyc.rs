use std::{
    collections::{BTreeMap, BTreeSet},
    ffi::OsString,
    fs,
    path::{Path, PathBuf},
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};

use oxc_allocator::Allocator;
use oxc_ast::ast::{
    Argument, ArrayExpression, ArrayExpressionElement, BinaryOperator, BindingPattern,
    CallExpression, ChainElement, ClassElement, Declaration, ExportAllDeclaration,
    ExportDefaultDeclaration, ExportNamedDeclaration, Expression, ExpressionStatement,
    ForStatementInit, ImportDeclaration, ImportDeclarationSpecifier, ImportOrExportKind,
    MethodDefinitionKind, ModuleExportName, ObjectExpression, ObjectPropertyKind, PropertyKey,
    PropertyKind, Statement, TSLiteral, TSSignature, TSType, TSTypeName, VariableDeclaration,
};
use oxc_codegen::Codegen;
use oxc_parser::Parser;
use oxc_semantic::SemanticBuilder;
use oxc_span::{SourceType, Span};
use oxc_transformer::{Module, TransformOptions, Transformer};
use serde_json::json;
use serde_json::Value;

use crate::diagnostic::Diagnostic;
pub(crate) use crate::graph::*;
use crate::hash::{sha256_bytes_hex, sha256_hex};
use crate::parser::{source_type_for_path, ParseContext};
#[cfg(test)]
use crate::plan_emit::emit_plan;
use crate::plan_emit::{
    dependency_graph_json, emit_dependency_graph, emit_plan_with_route_artifact,
};
use crate::resolver;
use crate::route_artifact::{emit_route_artifact, ROUTE_ARTIFACT_PATH};
use crate::source::{line_column, source_map_source_name};
use crate::static_eval::{eval_string_argument, eval_string_expression, StaticStringEnv};
use crate::version::COMPILER_VERSION;

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

const DEFAULT_HEALTH_PATH: &str = "/health";
const DEFAULT_LIVENESS_PATH: &str = "/health/live";
const DEFAULT_READINESS_PATH: &str = "/health/ready";
const DEFAULT_MANAGEMENT_PATH: &str = "/_sloppy";

mod configuration;
use configuration::*;
mod schema;
pub(crate) use schema::ts_type_span;
use schema::*;
mod effects;
use effects::*;
mod framework_features;
use framework_features::*;

#[derive(Debug, Eq, PartialEq)]
enum CliCommand {
    Help,
    Version,
    Build {
        input: PathBuf,
        out_dir: PathBuf,
        options: Box<CompileOptions>,
    },
    Invalid(String),
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct CompileOptions {
    pub kind: Option<ProjectKind>,
    pub environment: Option<String>,
    pub host: Option<String>,
    pub port: Option<u16>,
    pub config_dir: Option<PathBuf>,
    pub config_overrides: Vec<(String, String)>,
    pub declared_capabilities: Vec<String>,
    pub declared_capabilities_from_sloppy_json: bool,
    pub module_include: Vec<String>,
    pub asset_include: Vec<String>,
    pub timings_json: Option<PathBuf>,
}

impl CompileOptions {
    pub fn new() -> Self {
        Self {
            kind: None,
            environment: None,
            host: None,
            port: None,
            config_dir: None,
            config_overrides: Vec::new(),
            declared_capabilities: Vec::new(),
            declared_capabilities_from_sloppy_json: false,
            module_include: Vec::new(),
            asset_include: Vec::new(),
            timings_json: None,
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

#[derive(Debug)]
struct CompileMetrics {
    schema_version: u32,
    started_at_unix_ms: u128,
    phases_ms: BTreeMap<&'static str, u128>,
    counters: BTreeMap<&'static str, u64>,
    artifacts: BTreeMap<&'static str, u64>,
}

impl CompileMetrics {
    fn new() -> Self {
        let started_at_unix_ms = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|duration| duration.as_millis())
            .unwrap_or(0);
        Self {
            schema_version: 1,
            started_at_unix_ms,
            phases_ms: BTreeMap::new(),
            counters: BTreeMap::new(),
            artifacts: BTreeMap::new(),
        }
    }

    fn add_phase(&mut self, name: &'static str, duration: Duration) {
        *self.phases_ms.entry(name).or_insert(0) += duration.as_millis();
    }

    fn set_counter(&mut self, name: &'static str, value: u64) {
        self.counters.insert(name, value);
    }

    fn set_artifact_bytes(&mut self, name: &'static str, value: usize) {
        self.artifacts.insert(name, value as u64);
    }

    fn record_app(&mut self, app: &ExtractedApp) {
        self.set_counter("filesParsed", app.source_files.len() as u64);
        self.set_counter(
            "sourceBytes",
            app.source_files
                .iter()
                .map(|file| file.source.len() as u64)
                .sum(),
        );
        self.set_counter("routes", app.routes.len() as u64);
        self.set_counter("handlers", app.routes.len() as u64);
        self.set_counter("schemas", app.schemas.len() as u64);
        self.set_counter("services", app.service_registrations.len() as u64);
        self.set_counter("providers", app.capabilities.len() as u64);
        self.set_counter("configReads", app.config_reads.len() as u64);
    }

    fn to_json(&self) -> Value {
        json!({
            "schemaVersion": self.schema_version,
            "startedAtUnixMs": self.started_at_unix_ms,
            "phases": self.phases_ms,
            "counters": self.counters,
            "artifacts": self.artifacts
        })
    }
}

struct HandlerExtractionContext<'a> {
    route_pattern: &'a str,
    route_kind: &'static str,
    source: &'a str,
    source_name: &'a str,
    allow_data_handler_body: bool,
    schema_names: &'a BTreeSet<String>,
    provider_bindings: &'a BTreeMap<String, ProviderBinding>,
    helper_effects: &'a BTreeMap<String, FunctionEffectSummary>,
}

#[derive(Debug, Clone)]
struct RouteGroupState {
    prefix: String,
    tags: Vec<String>,
    middleware: Vec<FrameworkMiddleware>,
    auth: Option<AuthRequirementMetadata>,
}

#[derive(Debug, Clone, Default)]
struct RouteMetadata {
    name: Option<String>,
    tags: Vec<String>,
    auth: Option<AuthRequirementMetadata>,
    accepts_schema: Option<String>,
    returns_schema: Option<String>,
    returns_status: Option<u16>,
    summary: Option<String>,
    description: Option<String>,
    deprecated: Option<String>,
    consumes: Vec<String>,
    produces: Vec<String>,
    headers: Vec<RouteContractParameter>,
    query_schema: Option<String>,
    params_schema: Option<String>,
    openapi_override: Option<Value>,
    output_cache: Option<Value>,
    cache_headers: Option<Value>,
    rate_limits: Vec<RateLimitMetadata>,
    websocket: Option<WebSocketRouteOptionsMetadata>,
    realtime_channel_source: Option<String>,
    realtime_options_source: Option<String>,
}

#[derive(Debug, Clone)]
struct SchemaReferenceEdit {
    argument_span: Span,
}

#[derive(Debug, Clone)]
struct HealthOptions {
    path: String,
    liveness_path: String,
    readiness_path: String,
    startup_path: Option<String>,
    checks: Vec<HealthCheck>,
}

#[derive(Debug, Clone)]
struct DocsOptions {
    enabled: bool,
    strict: bool,
    path: String,
    openapi_path: String,
    title: String,
    require_auth: Option<AuthRequirementMetadata>,
}

#[derive(Debug, Clone)]
struct HealthCheck {
    name: String,
    check_source: String,
    liveness: bool,
    readiness: bool,
    startup: bool,
    critical: bool,
    degraded_is_unhealthy: bool,
    tags: Vec<String>,
}

#[derive(Debug, Clone)]
struct ManagementOptions {
    path: String,
    health: bool,
    metrics: bool,
    info: bool,
    runtime: bool,
}

#[derive(Debug, Clone)]
struct FrameworkMiddleware {
    kind: FrameworkMiddlewareKind,
    source: String,
    sequence: usize,
    source_name: String,
    source_text: String,
    span: Span,
}

#[derive(Debug, Clone)]
enum FrameworkMiddlewareKind {
    User,
    RequestId,
    RequestLogging,
}

#[derive(Debug, Clone)]
struct RequestIdOptions {
    header: String,
    response_header: bool,
    trust_incoming: bool,
}

#[derive(Debug, Clone)]
struct RequestLoggingOptions {
    include_route: bool,
    include_duration: bool,
    include_request_id: bool,
}

#[derive(Debug, Clone)]
struct CorsPolicy {
    origins: Vec<String>,
    methods: Vec<String>,
    headers: Vec<String>,
    exposed_headers: Vec<String>,
    credentials: bool,
    max_age_seconds: Option<u64>,
}

#[derive(Debug, Clone)]
struct ControllerDescriptor {
    source_name: String,
    source_text: String,
    methods: BTreeMap<String, ControllerMethodDescriptor>,
}

#[derive(Debug, Clone)]
struct ControllerMethodDescriptor {
    span: Span,
    requires_results_import: bool,
}

struct HealthRouteSpec<'a> {
    framework_path: &'a str,
    name: &'a str,
    kind: &'static str,
    checks: Vec<&'a HealthCheck>,
}

type RouteCallParts<'a> = (
    &'a str,
    &'static str,
    &'static str,
    String,
    RouteMetadata,
    &'a Argument<'a>,
);

type ExtractedRouteCall<'a> = (&'a str, &'static str, &'static str, String, Handler);

#[derive(Debug)]
struct AppState {
    sloppy_imported: bool,
    results_imported: bool,
    data_imported: bool,
    sql_imported: bool,
    orm_imported: bool,
    migrations_imported: bool,
    provider_health_imported: bool,
    schema_imported: bool,
    time_imported: bool,
    fs_imported: bool,
    crypto_imported: bool,
    noncrypto_hash_security_context_visible: bool,
    codec_imported: bool,
    checksum_security_context_visible: bool,
    cache_imported: bool,
    net_imported: bool,
    os_imported: bool,
    http_client_imported: bool,
    webhooks_imported: bool,
    redis_imported: bool,
    realtime_imported: bool,
    workers_imported: bool,
    ffi_imported: bool,
    ffi_libraries: Vec<FfiLibraryMetadata>,
    ffi_structs: Vec<FfiStructMetadata>,
    problem_details_imported: bool,
    auth_imported: bool,
    config_imported: bool,
    request_id_imported: bool,
    request_logging_imported: bool,
    sqlite_imported: bool,
    unsupported_import_alias: bool,
    unsupported_import_name: Option<(String, Span)>,
    unsupported_import_specifier: Option<(String, Span)>,
    dynamic_import: Option<Span>,
    results_required_span: Option<Span>,
    app_vars: BTreeSet<String>,
    builder_vars: BTreeSet<String>,
    group_vars: BTreeMap<String, RouteGroupState>,
    provider_bindings: BTreeMap<String, ProviderBinding>,
    helper_sources: BTreeMap<String, String>,
    helper_effects: BTreeMap<String, FunctionEffectSummary>,
    orm_metadata_sources: Vec<(u32, String)>,
    orm_tables: Vec<Value>,
    orm_relations: Vec<Value>,
    orm_extraction_partial: bool,
    middleware: Vec<FrameworkMiddleware>,
    next_middleware_sequence: usize,
    cors_policy: Option<CorsPolicy>,
    controllers: BTreeMap<String, ControllerDescriptor>,
    app_provider_uses: BTreeSet<String>,
    imported_modules: Vec<ImportedModule>,
    used_modules: Vec<(String, Span)>,
    modules: BTreeMap<(String, String), FunctionModule>,
    routes: Vec<Route>,
    dynamic_routes: Vec<DynamicRoute>,
    static_asset_routes: Vec<Route>,
    static_strings: StaticStringEnv,
    service_registrations: Vec<ServiceRegistration>,
    capabilities: Vec<DatabaseCapability>,
    schemas: Vec<SchemaMetadata>,
    schema_names: BTreeSet<String>,
    config_reads: Vec<ConfigReadMetadata>,
    default_export: Option<String>,
    uses_health: bool,
    auth: AuthMetadata,
    problem_details: Option<ProblemDetailsDescriptor>,
}

impl AppState {
    fn new() -> Self {
        Self {
            sloppy_imported: false,
            results_imported: false,
            data_imported: false,
            sql_imported: false,
            orm_imported: false,
            migrations_imported: false,
            provider_health_imported: false,
            schema_imported: false,
            time_imported: false,
            fs_imported: false,
            crypto_imported: false,
            noncrypto_hash_security_context_visible: false,
            codec_imported: false,
            checksum_security_context_visible: false,
            cache_imported: false,
            net_imported: false,
            os_imported: false,
            http_client_imported: false,
            webhooks_imported: false,
            redis_imported: false,
            realtime_imported: false,
            workers_imported: false,
            ffi_imported: false,
            ffi_libraries: Vec::new(),
            ffi_structs: Vec::new(),
            problem_details_imported: false,
            auth_imported: false,
            config_imported: false,
            request_id_imported: false,
            request_logging_imported: false,
            sqlite_imported: false,
            unsupported_import_alias: false,
            unsupported_import_name: None,
            unsupported_import_specifier: None,
            dynamic_import: None,
            results_required_span: None,
            app_vars: BTreeSet::new(),
            builder_vars: BTreeSet::new(),
            group_vars: BTreeMap::new(),
            provider_bindings: BTreeMap::new(),
            helper_sources: BTreeMap::new(),
            helper_effects: BTreeMap::new(),
            orm_metadata_sources: Vec::new(),
            orm_tables: Vec::new(),
            orm_relations: Vec::new(),
            orm_extraction_partial: false,
            middleware: Vec::new(),
            next_middleware_sequence: 0,
            cors_policy: None,
            controllers: BTreeMap::new(),
            app_provider_uses: BTreeSet::new(),
            imported_modules: Vec::new(),
            used_modules: Vec::new(),
            modules: BTreeMap::new(),
            routes: Vec::new(),
            dynamic_routes: Vec::new(),
            static_asset_routes: Vec::new(),
            static_strings: StaticStringEnv::default(),
            service_registrations: Vec::new(),
            capabilities: Vec::new(),
            schemas: Vec::new(),
            schema_names: BTreeSet::new(),
            config_reads: Vec::new(),
            default_export: None,
            uses_health: false,
            auth: AuthMetadata::default(),
            problem_details: None,
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
        } else if arg == "--kind" {
            index += 1;
            if index >= values.len() {
                return CliCommand::Invalid(
                    "build requires web or program after --kind".to_string(),
                );
            }
            let Some(kind_text) = values[index].to_str() else {
                return CliCommand::Invalid("build kind must be valid UTF-8".to_string());
            };
            options.kind = match kind_text {
                "web" => Some(ProjectKind::Web),
                "program" => Some(ProjectKind::Program),
                _ => {
                    return CliCommand::Invalid(format!(
                        "build --kind expects web or program, got '{kind_text}'"
                    ));
                }
            };
        } else if arg == "--config-dir" {
            index += 1;
            if index >= values.len() {
                return CliCommand::Invalid(
                    "build requires a directory after --config-dir".to_string(),
                );
            }
            options.config_dir = Some(PathBuf::from(&values[index]));
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
        } else if arg == "--capability" {
            index += 1;
            if index >= values.len() {
                return CliCommand::Invalid(
                    "build requires a capability name after --capability".to_string(),
                );
            }
            let Some(capability) = values[index].to_str() else {
                return CliCommand::Invalid("build capability must be valid UTF-8".to_string());
            };
            if declared_capability_shape(capability).is_none() {
                return CliCommand::Invalid(format!(
                    "build --capability expects fs, net, os, time, crypto, codec, or workers, got '{capability}'"
                ));
            }
            options.declared_capabilities.push(capability.to_string());
        } else if arg == "--capability-origin" {
            index += 1;
            if index >= values.len() {
                return CliCommand::Invalid(
                    "build requires an origin after --capability-origin".to_string(),
                );
            }
            let Some(origin) = values[index].to_str() else {
                return CliCommand::Invalid(
                    "build capability origin must be valid UTF-8".to_string(),
                );
            };
            match origin {
                "command-line" => options.declared_capabilities_from_sloppy_json = false,
                "sloppy.json" => options.declared_capabilities_from_sloppy_json = true,
                _ => {
                    return CliCommand::Invalid(
                        "build --capability-origin expects command-line or sloppy.json".to_string(),
                    );
                }
            }
        } else if arg == "--module-include" {
            index += 1;
            if index >= values.len() {
                return CliCommand::Invalid(
                    "build requires a project-relative glob after --module-include".to_string(),
                );
            }
            let Some(pattern) = values[index].to_str() else {
                return CliCommand::Invalid(
                    "build module include pattern must be valid UTF-8".to_string(),
                );
            };
            if !include_pattern_is_safe(pattern) {
                return CliCommand::Invalid(
                    "build --module-include expects a non-empty project-relative glob without '..'"
                        .to_string(),
                );
            }
            options.module_include.push(pattern.to_string());
        } else if arg == "--asset-include" {
            index += 1;
            if index >= values.len() {
                return CliCommand::Invalid(
                    "build requires a project-relative glob after --asset-include".to_string(),
                );
            }
            let Some(pattern) = values[index].to_str() else {
                return CliCommand::Invalid(
                    "build asset include pattern must be valid UTF-8".to_string(),
                );
            };
            if !include_pattern_is_safe(pattern) {
                return CliCommand::Invalid(
                    "build --asset-include expects a non-empty project-relative glob without '..'"
                        .to_string(),
                );
            }
            options.asset_include.push(pattern.to_string());
        } else if arg == "--timings-json" || arg == "--diagnostics-timing-json" {
            index += 1;
            if index >= values.len() {
                return CliCommand::Invalid(format!("build requires a file after {arg}"));
            }
            options.timings_json = Some(PathBuf::from(&values[index]));
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
            options: Box::new(options),
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
        "  sloppyc build <input.js|input.ts> --out <directory> [--kind web|program] [--environment <name>] [--host <host>] [--port <port>] [--config-dir <dir>] [--config <key=value>] [--module-include <glob>] [--asset-include <glob>] [--timings-json|--diagnostics-timing-json <file>]\n",
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
    let mut metrics = options.timings_json.as_ref().map(|_| CompileMetrics::new());

    let read_start = Instant::now();
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
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("readInputMs", read_start.elapsed());
    }

    let mut extracted = extract_for_options_with_metrics(input, &source, options, metrics.as_mut())
        .map_err(|diagnostic| {
            let diagnostic_source = diagnostic_render_source(input, &source, &diagnostic);
            Box::new(CompileError {
                code: 1,
                diagnostic,
                source: diagnostic_source,
            })
        })?;
    let config_start = Instant::now();
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
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("configurationMs", config_start.elapsed());
    }
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
    apply_declared_capabilities(&mut extracted, options).map_err(|diagnostic| {
        let diagnostic_source = diagnostic_render_source(input, &source, &diagnostic);
        Box::new(CompileError {
            code: 1,
            diagnostic,
            source: diagnostic_source,
        })
    })?;
    if let Some(metrics) = metrics.as_mut() {
        metrics.record_app(&extracted);
    }
    write_artifacts(out_dir, &extracted, metrics.as_mut()).map_err(|diagnostic| {
        let diagnostic_source = diagnostic_render_source(input, &source, &diagnostic);
        Box::new(CompileError {
            code: 1,
            diagnostic,
            source: diagnostic_source,
        })
    })?;
    if let (Some(metrics), Some(path)) = (metrics.as_ref(), options.timings_json.as_ref()) {
        validate_timings_output_path(input, out_dir, path).map_err(|diagnostic| {
            let diagnostic_source = diagnostic_render_source(input, &source, &diagnostic);
            Box::new(CompileError {
                code: 1,
                diagnostic,
                source: diagnostic_source,
            })
        })?;
        write_timings_json(path, metrics).map_err(|diagnostic| {
            let diagnostic_source = diagnostic_render_source(input, &source, &diagnostic);
            Box::new(CompileError {
                code: 1,
                diagnostic,
                source: diagnostic_source,
            })
        })?;
    }
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

fn canonical_if_present(path: &Path) -> PathBuf {
    path.canonicalize().unwrap_or_else(|_| path.to_path_buf())
}

fn validate_timings_output_path(
    input: &Path,
    out_dir: &Path,
    timings_path: &Path,
) -> Result<(), Diagnostic> {
    let timings = canonical_if_present(timings_path);
    let bundle = out_dir.join("app.js");
    let source_map = out_dir.join("app.js.map");
    let plan = out_dir.join("app.plan.json");
    let conflicts = [
        (input, "input source"),
        (bundle.as_path(), "emitted bundle"),
        (source_map.as_path(), "emitted source map"),
        (plan.as_path(), "emitted Plan"),
    ];
    for (path, label) in conflicts {
        if timings == canonical_if_present(path) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_OUTPUT",
                format!("timings JSON output conflicts with {label} path"),
            )
            .with_path(timings_path));
        }
    }
    Ok(())
}

fn write_timings_json(path: &Path, metrics: &CompileMetrics) -> Result<(), Diagnostic> {
    if let Some(parent) = path.parent() {
        if !parent.as_os_str().is_empty() {
            fs::create_dir_all(parent).map_err(|error| {
                Diagnostic::new(
                    "SLOPPYC_E_OUTPUT",
                    format!("failed to create timings output directory: {error}"),
                )
                .with_path(parent)
            })?;
        }
    }
    let value = metrics.to_json();
    let mut contents = serde_json::to_string_pretty(&value).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_OUTPUT",
            format!("failed to serialize timings JSON: {error}"),
        )
        .with_path(path)
    })?;
    contents.push('\n');
    fs::write(path, contents).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_OUTPUT",
            format!("failed to write timings JSON: {error}"),
        )
        .with_path(path)
    })
}

#[derive(Debug, Clone)]
struct ModuleGraph {
    entry_dir: PathBuf,
    visiting: BTreeSet<PathBuf>,
    modules: BTreeMap<PathBuf, CachedModule>,
    source_file_names: BTreeSet<String>,
    source_files: Vec<SourceFile>,
    uses_data_runtime: bool,
    uses_sql_runtime: bool,
    uses_orm_runtime: bool,
    uses_migrations_runtime: bool,
    uses_provider_health_runtime: bool,
    uses_time_runtime: bool,
    uses_fs_runtime: bool,
    uses_crypto_runtime: bool,
    noncrypto_hash_security_context_visible: bool,
    uses_codec_runtime: bool,
    checksum_security_context_visible: bool,
    uses_cache_runtime: bool,
    uses_net_runtime: bool,
    uses_os_runtime: bool,
    uses_http_client_runtime: bool,
    uses_webhooks_runtime: bool,
    uses_redis_runtime: bool,
    uses_realtime_runtime: bool,
    uses_workers_runtime: bool,
    dependency_graph: DependencyGraph,
    uses_ffi_runtime: bool,
    ffi_libraries: Vec<FfiLibraryMetadata>,
    ffi_structs: Vec<FfiStructMetadata>,
}

#[derive(Debug, Clone)]
struct CachedModule {
    exports: BTreeMap<String, CachedModuleExport>,
    duplicate_exports: BTreeSet<String>,
}

#[derive(Debug, Clone)]
struct CachedModuleExport {
    routes: Vec<Route>,
    schemas: Vec<SchemaMetadata>,
}

impl ModuleGraph {
    fn new(entry_path: &Path, source_root: Option<&Path>) -> Self {
        let mut entry_dir = source_root.map(Path::to_path_buf).unwrap_or_else(|| {
            entry_path
                .parent()
                .unwrap_or_else(|| Path::new(""))
                .to_path_buf()
        });
        if entry_dir.as_os_str().is_empty() {
            entry_dir = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
        }
        Self {
            entry_dir: fs::canonicalize(&entry_dir).unwrap_or(entry_dir),
            visiting: BTreeSet::new(),
            modules: BTreeMap::new(),
            source_file_names: BTreeSet::new(),
            source_files: Vec::new(),
            uses_data_runtime: false,
            uses_sql_runtime: false,
            uses_orm_runtime: false,
            uses_migrations_runtime: false,
            uses_provider_health_runtime: false,
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
            uses_redis_runtime: false,
            uses_realtime_runtime: false,
            uses_workers_runtime: false,
            dependency_graph: DependencyGraph::default(),
            uses_ffi_runtime: false,
            ffi_libraries: Vec::new(),
            ffi_structs: Vec::new(),
        }
    }

    fn record_source(&mut self, path: &Path, source: &str) -> String {
        let name = source_map_source_name(path);
        if self.source_file_names.insert(name.clone()) {
            self.source_files.push(SourceFile {
                name: name.clone(),
                source: source.to_string(),
            });
        }
        name
    }

    fn add_package_record(&mut self, package: &resolver::PackageResolution) {
        self.dependency_graph.ensure_defaults();
        let root = resolver::normalized_artifact_id(&package.root, &self.entry_dir);
        let package_json = package
            .package_json
            .as_ref()
            .map(|path| resolver::normalized_artifact_id(path, &self.entry_dir));
        let entry = resolver::normalized_artifact_id(&package.entry, &self.entry_dir);
        if self
            .dependency_graph
            .packages
            .iter()
            .any(|record| record.name == package.name && record.root == root)
        {
            return;
        }
        self.dependency_graph.packages.push(PackageRecord {
            name: package.name.clone(),
            version: package.version.clone(),
            root,
            package_json,
            entry,
            format: package.format,
            source: package.source.to_string(),
        });
    }

    fn add_node_builtin(&mut self, builtin: &resolver::NodeBuiltinResolution, source: &Path) {
        self.dependency_graph.ensure_defaults();
        if !self
            .dependency_graph
            .node_builtins
            .iter()
            .any(|record| record.specifier == builtin.specifier)
        {
            self.dependency_graph.node_builtins.push(NodeBuiltinRecord {
                specifier: builtin.specifier.clone(),
                status: builtin.status.to_string(),
                backing: builtin.backing.map(ToOwned::to_owned),
                capability: builtin.capability.map(ToOwned::to_owned),
            });
        }
        if let Some(capability) = builtin.capability {
            match capability {
                "fs" => self.uses_fs_runtime = true,
                "time" => self.uses_time_runtime = true,
                "cache" => self.uses_cache_runtime = true,
                "net" => self.uses_net_runtime = true,
                "redis" => {
                    self.uses_redis_runtime = true;
                    self.uses_net_runtime = true;
                }
                "os" => self.uses_os_runtime = true,
                "crypto" => self.uses_crypto_runtime = true,
                "codec" => self.uses_codec_runtime = true,
                _ => {}
            }
        }
        if builtin.status == "unsupported" {
            self.dependency_graph
                .compatibility_findings
                .push(CompatibilityFinding {
                code: "SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN".to_string(),
                severity: "error".to_string(),
                message: format!(
                    "{} is not supported by Sloppy's Node compatibility registry yet.",
                    builtin.specifier
                ),
                source: Some(source_map_source_name(source)),
                package: None,
                specifier: Some(builtin.specifier.clone()),
                hint: Some(
                    "Use a Sloppy stdlib API or a dependency path that avoids this Node builtin."
                        .to_string(),
                ),
            });
        }
    }

    fn add_dependency_module(
        &mut self,
        id: String,
        source: String,
        format: ModuleFormat,
        package: Option<String>,
        included_by: Option<String>,
    ) {
        self.dependency_graph.ensure_defaults();
        if let Some(record) = self
            .dependency_graph
            .modules
            .iter()
            .position(|record| record.id == id)
        {
            let record = &mut self.dependency_graph.modules[record];
            record.source = source;
            record.format = format;
            if package.is_some() || record.package.is_none() {
                record.package = package;
            }
            if record.included_by.is_none() {
                record.included_by = included_by;
            }
            return;
        }
        self.dependency_graph.modules.push(DependencyModuleRecord {
            id,
            source,
            format,
            package,
            imports: Vec::new(),
            resolved_imports: Vec::new(),
            dynamic_imports: Vec::new(),
            included_by,
        });
    }

    fn add_source_dependency_module(
        &mut self,
        path: &Path,
        package: Option<String>,
        included_by: Option<String>,
    ) -> String {
        let id = resolver::normalized_artifact_id(path, &self.entry_dir);
        self.add_dependency_module(
            id.clone(),
            id.clone(),
            ModuleFormat::Esm,
            package,
            included_by,
        );
        id
    }

    fn add_relative_dependency_import(
        &mut self,
        from_path: &Path,
        specifier: &str,
        resolved_path: &Path,
    ) {
        let from_id = self.add_source_dependency_module(from_path, None, None);
        let resolved_id =
            self.add_source_dependency_module(resolved_path, None, Some(from_id.clone()));
        self.add_dependency_import(&from_id, specifier, &resolved_id, "relative");
    }

    fn add_package_dependency_import(
        &mut self,
        from_path: &Path,
        specifier: &str,
        package_id: &str,
    ) {
        let from_id = self.add_source_dependency_module(from_path, None, None);
        self.add_dependency_import(&from_id, specifier, package_id, "package");
    }

    fn add_dependency_import(
        &mut self,
        from_id: &str,
        specifier: &str,
        resolved_id: &str,
        kind: &str,
    ) {
        if !self
            .dependency_graph
            .modules
            .iter()
            .any(|record| record.id == from_id)
        {
            self.add_dependency_module(
                from_id.to_string(),
                from_id.to_string(),
                ModuleFormat::Esm,
                None,
                None,
            );
        }
        let Some(module) = self
            .dependency_graph
            .modules
            .iter_mut()
            .find(|record| record.id == from_id)
        else {
            return;
        };
        if !module.imports.iter().any(|import| import == specifier) {
            module.imports.push(specifier.to_string());
        }
        if !module.resolved_imports.iter().any(|import| {
            import.specifier == specifier
                && import.resolved_id == resolved_id
                && import.kind == kind
        }) {
            module.resolved_imports.push(ResolvedImportRecord {
                specifier: specifier.to_string(),
                resolved_id: resolved_id.to_string(),
                kind: kind.to_string(),
            });
        }
    }

    fn add_dynamic_import(
        &mut self,
        from_id: &str,
        specifier: Option<String>,
        resolved_id: Option<String>,
        kind: &str,
    ) {
        if !self
            .dependency_graph
            .modules
            .iter()
            .any(|record| record.id == from_id)
        {
            self.add_dependency_module(
                from_id.to_string(),
                from_id.to_string(),
                ModuleFormat::Esm,
                None,
                None,
            );
        }
        let Some(module) = self
            .dependency_graph
            .modules
            .iter_mut()
            .find(|record| record.id == from_id)
        else {
            return;
        };
        module.dynamic_imports.push(DynamicImportRecord {
            specifier,
            resolved_id,
            kind: kind.to_string(),
        });
    }

    fn add_dependency_asset(&mut self, path: &Path, included_by: String) {
        self.dependency_graph.ensure_defaults();
        let path = resolver::normalized_artifact_id(path, &self.entry_dir);
        if self
            .dependency_graph
            .assets
            .iter()
            .any(|asset| asset.path == path && asset.included_by == included_by)
        {
            return;
        }
        self.dependency_graph
            .assets
            .push(AssetRecord { path, included_by });
    }
}

fn declared_capability_shape(name: &str) -> Option<(&'static str, &'static str)> {
    match name {
        "fs" => Some(("filesystem", "readwrite")),
        "net" => Some(("network", "connect-listen")),
        "os" => Some(("os", "info")),
        "time" => Some(("time", "use")),
        "crypto" => Some(("crypto", "use")),
        "codec" => Some(("codec", "use")),
        "workers" => Some(("workers", "use")),
        "ffi" => Some(("ffi", "use")),
        _ => None,
    }
}

fn apply_declared_capabilities(
    app: &mut ExtractedApp,
    options: &CompileOptions,
) -> Result<(), Diagnostic> {
    let mut seen = app
        .capabilities
        .iter()
        .map(|capability| capability.token.clone())
        .collect::<BTreeSet<_>>();

    for capability in &options.declared_capabilities {
        let Some((kind, access)) = declared_capability_shape(capability) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_INVALID_CAPABILITY",
                format!("unsupported declared capability '{capability}'"),
            ));
        };
        match capability.as_str() {
            "fs" => app.uses_fs_runtime = true,
            "cache" => app.uses_cache_runtime = true,
            "net" => app.uses_net_runtime = true,
            "redis" => {
                app.uses_redis_runtime = true;
                app.uses_net_runtime = true;
            }
            "os" => app.uses_os_runtime = true,
            "time" => app.uses_time_runtime = true,
            "crypto" => app.uses_crypto_runtime = true,
            "codec" => app.uses_codec_runtime = true,
            "workers" => app.uses_workers_runtime = true,
            "ffi" => app.uses_ffi_runtime = true,
            _ => {}
        }
        if !seen.insert(capability.clone()) {
            continue;
        }
        let (source_name, source) = if options.declared_capabilities_from_sloppy_json {
            (
                "sloppy.json".to_string(),
                format!("capabilities.{capability}"),
            )
        } else {
            (
                "command-line".to_string(),
                format!("--capability {capability}"),
            )
        };
        app.capabilities.push(DatabaseCapability {
            token: capability.clone(),
            capability_kind: kind.to_string(),
            provider: String::new(),
            config_name: None,
            config_key: None,
            access: access.to_string(),
            database: None,
            config_source: None,
            source_name,
            source,
            span: Span::new(0, 0),
            from_provider_use: false,
        });
    }

    Ok(())
}

#[cfg(test)]
fn extract(path: &Path, source: &str) -> Result<ExtractedApp, Diagnostic> {
    extract_web_with_metrics(path, source, None, None)
}

fn extract_for_options_with_metrics(
    path: &Path,
    source: &str,
    options: &CompileOptions,
    mut metrics: Option<&mut CompileMetrics>,
) -> Result<ExtractedApp, Diagnostic> {
    match options.kind {
        Some(ProjectKind::Web) => {
            extract_web_with_metrics(path, source, options.config_dir.as_deref(), metrics)
        }
        Some(ProjectKind::Program) => extract_program_with_metrics(path, source, options, metrics),
        None => {
            match extract_web_with_metrics(
                path,
                source,
                options.config_dir.as_deref(),
                metrics.as_deref_mut(),
            ) {
                Ok(app) => Ok(app),
                Err(web_error) => {
                    let has_sloppy_web_import = source_has_sloppy_web_import(path, source)?;
                    if has_sloppy_web_import && web_error_indicates_missing_web_shape(&web_error) {
                        return Err(Diagnostic::new(
                        "SLOPPYC_E_AMBIGUOUS_SOURCE_KIND",
                        "This source imports Sloppy but does not export a supported web app shape.",
                    )
                    .with_path(path)
                    .with_hint("Use --kind program to run it as a program, or export a Sloppy app."));
                    }
                    if has_sloppy_web_import {
                        return Err(web_error);
                    }
                    extract_program_with_metrics(path, source, options, metrics)
                }
            }
        }
    }
}

fn web_error_indicates_missing_web_shape(error: &Diagnostic) -> bool {
    matches!(
        error.code,
        "SLOPPYC_E_MISSING_APP"
            | "SLOPPYC_E_MISSING_ROUTE"
            | "SLOPPYC_E_UNSUPPORTED_IMPORT"
            | "SLOPPYC_E_UNSUPPORTED_TOP_LEVEL"
    )
}

fn extract_web_with_metrics(
    path: &Path,
    source: &str,
    source_root: Option<&Path>,
    metrics: Option<&mut CompileMetrics>,
) -> Result<ExtractedApp, Diagnostic> {
    let mut graph = ModuleGraph::new(path, source_root);
    extract_entry(path, source, &mut graph, metrics)
}

mod program;
#[cfg(test)]
use program::NODE_BUFFER_SHIM;
use program::{
    extract_program_with_metrics, include_pattern_is_safe, program_module_id,
    source_has_sloppy_web_import, transform_dynamic_web_entry,
};
fn span_source(source: &str, span: Span) -> &str {
    &source[span.start as usize..span.end as usize]
}

fn identifier_like(value: &str) -> bool {
    let mut chars = value.chars();
    let Some(first) = chars.next() else {
        return false;
    };
    (first == '_' || first == '$' || first.is_ascii_alphabetic())
        && chars.all(|ch| ch == '_' || ch == '$' || ch.is_ascii_alphanumeric())
}

fn json_string(value: &str) -> String {
    serde_json::to_string(value).unwrap_or_else(|_| "\"\"".to_string())
}

fn mark_program_import(
    graph: &mut ModuleGraph,
    import_kind: &resolver::ImportKind,
    import: &ImportDeclaration<'_>,
) {
    match import_kind {
        resolver::ImportKind::SlopData => {
            mark_sloppy_data_runtime_usage(graph, import);
        }
        resolver::ImportKind::SlopTime => graph.uses_time_runtime = true,
        resolver::ImportKind::SlopFilesystem => graph.uses_fs_runtime = true,
        resolver::ImportKind::SlopCrypto => graph.uses_crypto_runtime = true,
        resolver::ImportKind::SlopCodec => graph.uses_codec_runtime = true,
        resolver::ImportKind::SlopCache => graph.uses_cache_runtime = true,
        resolver::ImportKind::SlopNet => {
            if let Some(specifiers) = &import.specifiers {
                for specifier in specifiers {
                    if let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier {
                        if specifier.import_kind == ImportOrExportKind::Type {
                            continue;
                        }
                        mark_sloppy_net_runtime_usage(
                            &mut graph.uses_net_runtime,
                            &mut graph.uses_http_client_runtime,
                            specifier.imported.name().as_str(),
                        );
                    }
                }
            }
        }
        resolver::ImportKind::SlopHttp => graph.uses_http_client_runtime = true,
        resolver::ImportKind::SlopWebhooks => {
            graph.uses_data_runtime = true;
            graph.uses_crypto_runtime = true;
            graph.uses_http_client_runtime = true;
            graph.uses_workers_runtime = true;
            graph.uses_webhooks_runtime = true;
        }
        resolver::ImportKind::SlopRedis => {
            graph.uses_redis_runtime = true;
            graph.uses_net_runtime = true;
        }
        resolver::ImportKind::SlopOs => graph.uses_os_runtime = true,
        resolver::ImportKind::SlopOrm => {
            graph.uses_orm_runtime = true;
            graph.uses_data_runtime = true;
            graph.uses_sql_runtime = true;
        }
        resolver::ImportKind::SlopWorkers => graph.uses_workers_runtime = true,
        resolver::ImportKind::SlopFfi => graph.uses_ffi_runtime = true,
        resolver::ImportKind::SlopStdlib => {
            graph.uses_time_runtime = true;
            graph.uses_fs_runtime = true;
            graph.uses_crypto_runtime = true;
            graph.uses_codec_runtime = true;
            graph.uses_cache_runtime = true;
            graph.uses_net_runtime = true;
            graph.uses_redis_runtime = true;
            graph.uses_os_runtime = true;
            graph.uses_workers_runtime = true;
        }
        _ => {}
    }
}

fn extract_entry(
    path: &Path,
    source: &str,
    graph: &mut ModuleGraph,
    mut metrics: Option<&mut CompileMetrics>,
) -> Result<ExtractedApp, Diagnostic> {
    let source_type = source_type_for_path(path, ParseContext::Entry)?;
    let parse_start = Instant::now();
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, source_type).parse();
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("parseEntryMs", parse_start.elapsed());
    }

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
    let extract_start = Instant::now();
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
            Statement::ClassDeclaration(class) => {
                extract_class_declaration(path, source, &source_name, &mut state, class)?
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
            _ => {
                if record_dynamic_top_level_route_statement(
                    source,
                    &source_name,
                    statement,
                    &mut state,
                ) {
                    continue;
                }
                return Err(top_level_statement_diagnostic(path, source, statement));
            }
        }
    }
    extract_ffi_declarations_from_statements(
        path,
        source,
        &source_name,
        &parsed.program.body,
        &mut state.ffi_libraries,
        &mut state.ffi_structs,
    )?;

    for statement in &parsed.program.body {
        if let Statement::ExpressionStatement(statement) = statement {
            extract_expression_statement(path, source, &source_name, graph, &mut state, statement)?;
        }
    }
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("extractMs", extract_start.elapsed());
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
                .with_hint("Use documented named imports from \"sloppy\", \"sloppy/time\", \"sloppy/fs\", \"sloppy/crypto\", \"sloppy/codec\", \"sloppy/cache\", \"sloppy/net\", \"sloppy/http\", \"sloppy/os\", \"sloppy/workers\", or \"sloppy/ffi\"; Sloppy does not implement Node or npm resolution."));
    }

    if let Some((specifier, span)) = &state.unsupported_import_name {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT",
            format!("unsupported sloppy import \"{specifier}\""),
        )
        .with_path(path)
        .with_span(*span)
        .with_hint("Use documented imports from \"sloppy\", \"sloppy/time\", \"sloppy/fs\", \"sloppy/crypto\", \"sloppy/codec\", \"sloppy/cache\", \"sloppy/net\", \"sloppy/http\", \"sloppy/os\", \"sloppy/workers\", or \"sloppy/ffi\"."));
    }

    if !state.sloppy_imported {
        let hint = if state.unsupported_import_alias {
            "Import without aliases: import { Sloppy } from \"sloppy\";"
        } else {
            "Use: import { Sloppy } from \"sloppy\";"
        };
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT",
            if state.unsupported_import_alias {
                "input must import Sloppy from \"sloppy\" without aliases"
            } else {
                "input must import Sloppy from \"sloppy\""
            },
        )
        .with_path(path)
        .with_hint(hint));
    }

    let module_graph_start = Instant::now();
    if !state.results_imported {
        if let Some(span) = state.results_required_span {
            return Err(missing_results_import_diagnostic(path, span));
        }
    }

    for (local_name, span) in state.used_modules.clone() {
        state.helper_sources.remove(&local_name);
        state.helper_effects.remove(&local_name);
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
        let module_export = extract_relative_module(graph, &imported, metrics.as_deref_mut())?;
        let module = FunctionModule {
            name: imported.export_name.clone(),
            source_name: source_map_source_name(&imported.path),
        };
        state
            .modules
            .entry((module.source_name.clone(), module.name.clone()))
            .or_insert(module);
        state.uses_health |= module_export
            .routes
            .iter()
            .any(|route| route.health.is_some());
        for schema in module_export.schemas {
            if !state.schema_names.insert(schema.name.clone()) {
                return Err(duplicate_schema_diagnostic(
                    &imported.path,
                    schema.span,
                    &schema.name,
                ));
            }
            state.schemas.push(schema);
        }
        state.routes.extend(module_export.routes);
    }
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("resolveModuleGraphMs", module_graph_start.elapsed());
    }

    let Some(default_export) = state.default_export.as_deref() else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_MISSING_APP",
            "input must export one app as default",
        )
        .with_path(path)
        .with_hint("End the file with: export default app;"));
    };

    if !state.app_vars.contains(default_export) {
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

    if state.routes.is_empty()
        && state.dynamic_routes.is_empty()
        && state.static_asset_routes.is_empty()
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_MISSING_ROUTE",
            "app must register at least one route",
        )
        .with_path(path));
    }

    let app_graph_start = Instant::now();
    state.routes.append(&mut state.static_asset_routes);
    append_cors_preflight_routes(path, &mut state.routes)?;

    let mut route_keys = BTreeSet::new();
    let mut route_names = BTreeSet::new();
    for route in &state.routes {
        let key = (route.method, route.pattern.as_str());
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

    add_inferred_framework_capabilities(&mut state);
    coalesce_manual_capability_overrides(&mut state.capabilities);
    apply_inferred_capability_access(&mut state.capabilities, &state.routes);
    validate_provider_effect_registrations(path, &state.routes, &state.capabilities)?;

    let mut capability_tokens = BTreeSet::new();
    for capability in &state.capabilities {
        if !capability_tokens.insert(capability.token.clone()) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_DUPLICATE_CAPABILITY",
                "duplicate capability token",
            )
            .with_path(path)
            .with_hint("Declare each capability token once."));
        }
    }

    if let Some(descriptor) = state.problem_details.as_ref() {
        apply_problem_details_to_routes(path, &mut state.routes, descriptor)?;
    }

    state
        .orm_metadata_sources
        .sort_by_key(|(source_start, _)| *source_start);
    let mut helper_sources = state
        .orm_metadata_sources
        .iter()
        .map(|(_, source)| source.clone())
        .collect::<Vec<_>>();
    let safe_helper_sources = state
        .helper_sources
        .iter()
        .filter(|(name, _)| helper_source_is_safe_for_top_level(state.helper_effects.get(*name)))
        .map(|(name, source)| (name.clone(), source.clone()))
        .collect::<BTreeMap<_, _>>();
    helper_sources.extend(helper_sources_in_dependency_order(&safe_helper_sources));
    let framework_needs_os_runtime = state.routes.iter().any(|route| {
        route.handler.bindings.iter().any(|binding| {
            binding.kind == "config"
                || matches!(
                    binding.provider_kind.as_deref(),
                    Some("postgres") | Some("sqlserver")
                )
        })
    });
    let uses_workers_runtime = state.workers_imported
        || graph.uses_workers_runtime
        || state
            .capabilities
            .iter()
            .any(|capability| capability.capability_kind == "queue");
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("appGraphMs", app_graph_start.elapsed());
    }

    let dynamic_entry_source = if state.dynamic_routes.is_empty() {
        None
    } else {
        Some(transform_dynamic_web_entry(path, source)?)
    };
    let uses_realtime_runtime =
        state.realtime_imported || state.routes.iter().any(|route| route.kind != "http");

    Ok(ExtractedApp {
        kind: ProjectKind::Web,
        program_entry: None,
        program_modules: Vec::new(),
        uses_data_runtime: state.data_imported
            || graph.uses_data_runtime
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
        uses_sql_runtime: state.sql_imported || graph.uses_sql_runtime,
        uses_orm_runtime: state.orm_imported || graph.uses_orm_runtime,
        orm_tables: state.orm_tables,
        orm_relations: state.orm_relations,
        orm_extraction_partial: state.orm_extraction_partial,
        uses_migrations_runtime: state.migrations_imported || graph.uses_migrations_runtime,
        uses_provider_health_runtime: state.provider_health_imported
            || graph.uses_provider_health_runtime,
        source_files: graph.source_files.clone(),
        routes: state.routes,
        dynamic_routes: state.dynamic_routes,
        dynamic_entry_source,
        service_registrations: state.service_registrations,
        modules: state.modules.into_values().collect(),
        helper_sources,
        capabilities: state.capabilities,
        configuration: None,
        schemas: state.schemas,
        config_reads: state.config_reads,
        uses_time_runtime: state.time_imported || graph.uses_time_runtime,
        uses_fs_runtime: state.fs_imported,
        uses_crypto_runtime: state.crypto_imported
            || graph.uses_crypto_runtime
            || state.auth.schemes.iter().any(|scheme| {
                matches!(
                    scheme,
                    AuthSchemeMetadata::JwtBearer { .. } | AuthSchemeMetadata::CookieSession { .. }
                )
            }),
        noncrypto_hash_security_context_visible: state.noncrypto_hash_security_context_visible
            || graph.noncrypto_hash_security_context_visible,
        uses_codec_runtime: state.codec_imported
            || graph.uses_codec_runtime
            || state.auth.schemes.iter().any(|scheme| {
                matches!(
                    scheme,
                    AuthSchemeMetadata::JwtBearer { .. } | AuthSchemeMetadata::CookieSession { .. }
                )
            }),
        checksum_security_context_visible: state.checksum_security_context_visible
            || graph.checksum_security_context_visible,
        uses_cache_runtime: state.cache_imported || graph.uses_cache_runtime,
        uses_net_runtime: state.net_imported || graph.uses_net_runtime || state.redis_imported,
        uses_os_runtime: state.os_imported
            || graph.uses_os_runtime
            || framework_needs_os_runtime
            || !state.auth.schemes.is_empty(),
        uses_http_client_runtime: state.http_client_imported || graph.uses_http_client_runtime,
        uses_webhooks_runtime: state.webhooks_imported || graph.uses_webhooks_runtime,
        uses_redis_runtime: state.redis_imported || graph.uses_redis_runtime,
        uses_realtime_runtime,
        uses_workers_runtime,
        uses_ffi_runtime: state.ffi_imported || graph.uses_ffi_runtime,
        ffi: {
            let mut ffi = graph.ffi_libraries.clone();
            ffi.extend(state.ffi_libraries);
            ffi
        },
        ffi_structs: {
            let mut ffi_structs = graph.ffi_structs.clone();
            ffi_structs.extend(state.ffi_structs);
            ffi_structs
        },
        uses_health: state.uses_health,
        auth: state.auth,
        problem_details: state.problem_details.clone(),
        dependency_graph: graph.dependency_graph.clone(),
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

mod imports;
use imports::{
    checksum_security_context_visible, extract_import, import_has_runtime_value_specifier,
    import_specifier_is_runtime_value, mark_sloppy_data_runtime_usage,
    mark_sloppy_net_runtime_usage, mark_sloppy_root_runtime_usage,
    missing_results_import_diagnostic, noncrypto_hash_security_context_visible,
    validate_module_sloppy_cache_import, validate_module_sloppy_codec_import,
    validate_module_sloppy_crypto_import, validate_module_sloppy_data_import,
    validate_module_sloppy_ffi_import, validate_module_sloppy_fs_import,
    validate_module_sloppy_http_import, validate_module_sloppy_net_import,
    validate_module_sloppy_orm_import, validate_module_sloppy_os_import,
    validate_module_sloppy_redis_import, validate_module_sloppy_root_import,
    validate_module_sloppy_sqlite_provider_import, validate_module_sloppy_time_import,
    validate_module_sloppy_webhooks_import, validate_module_sloppy_workers_import,
};
mod ffi;
use ffi::extract_ffi_declarations_from_statements;
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
        if let Some(value) = eval_string_expression(init, &state.static_strings) {
            state.static_strings.bind(name, value);
        }

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
        } else if let Some((receiver, prefix, metadata)) = app_group_call(init)? {
            let full_prefix = if state.app_vars.contains(receiver) {
                prefix.to_string()
            } else if let Some(parent) = state.group_vars.get(receiver) {
                join_route_patterns(&parent.prefix, prefix)
            } else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_ROUTE_GROUP",
                    "route groups must be created from the extracted app object or another extracted route group",
                )
                .with_path(path)
                .with_span(init.span()));
            };
            let mut tags = if state.app_vars.contains(receiver) {
                Vec::new()
            } else {
                state
                    .group_vars
                    .get(receiver)
                    .map(|parent| parent.tags.clone())
                    .unwrap_or_default()
            };
            let auth = metadata.auth.or_else(|| {
                state
                    .group_vars
                    .get(receiver)
                    .and_then(|parent| parent.auth.clone())
            });
            tags.extend(metadata.tags);
            state.group_vars.insert(
                name.to_string(),
                RouteGroupState {
                    prefix: full_prefix,
                    tags,
                    auth,
                    middleware: state
                        .group_vars
                        .get(receiver)
                        .map(|parent| parent.middleware.clone())
                        .unwrap_or_default(),
                },
            );
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
        } else if let Some(metadata_source) =
            orm_table_declaration_source(path, source, name, init, state)?
        {
            state
                .orm_metadata_sources
                .push((declarator.span.start, metadata_source));
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
                &state.helper_effects,
                source,
                source_name,
            );
            state.helper_sources.insert(name.to_string(), helper_source);
            state.helper_effects.insert(name.to_string(), summary);
            resolve_helper_effect_callgraph(&mut state.helper_effects);
        } else if state.schema_imported {
            if let Some(schema) =
                schema_declaration(path, source, source_name, name, init, &state.schema_names)?
            {
                if let Some(init_source) = source_slice(source, init.span()) {
                    let helper_source = format!("const {name} = {init_source};");
                    state.helper_sources.insert(name.to_string(), helper_source);
                    state
                        .helper_effects
                        .entry(name.to_string())
                        .or_insert_with(FunctionEffectSummary::default);
                }
                state.schemas.push(schema);
            } else if let Some((config_reads, helper_source)) =
                config_bind_helper_source(name, init, source, source_name, state)
            {
                state.config_reads.extend(config_reads);
                state.helper_sources.insert(name.to_string(), helper_source);
                state
                    .helper_effects
                    .entry(name.to_string())
                    .or_insert_with(FunctionEffectSummary::default);
            } else if let Some(config_reads) =
                config_read_metadata(path, source, source_name, state, init)?
            {
                state.config_reads.extend(config_reads);
            } else if let Some(diagnostic) = malformed_config_read_diagnostic(path, state, init) {
                return Err(diagnostic);
            } else {
                validate_supported_initializer(path, source, source_name, state, init)?;
            }
        } else if let Some((config_reads, helper_source)) =
            config_bind_helper_source(name, init, source, source_name, state)
        {
            state.config_reads.extend(config_reads);
            state.helper_sources.insert(name.to_string(), helper_source);
            state
                .helper_effects
                .entry(name.to_string())
                .or_insert_with(FunctionEffectSummary::default);
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
    let summary = helper_effects_from_function(
        function,
        &state.provider_bindings,
        &state.helper_effects,
        source,
        source_name,
    );
    state.helper_sources.insert(name.clone(), helper_source);
    state.helper_effects.insert(name, summary);
    resolve_helper_effect_callgraph(&mut state.helper_effects);
    Ok(())
}

fn extract_class_declaration(
    path: &Path,
    source: &str,
    source_name: &str,
    state: &mut AppState,
    class: &oxc_ast::ast::Class<'_>,
) -> Result<(), Diagnostic> {
    let Some(identifier) = &class.id else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller classes must have a name",
        )
        .with_path(path)
        .with_span(class.span));
    };
    if class.is_typescript_syntax() || !class.decorators.is_empty() || class.super_class.is_some() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller classes in compiler input must be plain JavaScript classes",
        )
        .with_path(path)
        .with_span(class.span)
        .with_hint("Use a static class with literal methods and optional static inject/dependencies metadata."));
    }
    let Some(class_source) = source_slice(source, class.span) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller class source could not be extracted",
        )
        .with_path(path)
        .with_span(class.span));
    };
    let mut methods = BTreeMap::new();
    for element in &class.body.body {
        match element {
            ClassElement::MethodDefinition(method) => {
                if method.computed || method.r#static || method.kind != MethodDefinitionKind::Method
                {
                    continue;
                }
                if let Some(name) = property_key_name(&method.key) {
                    methods.insert(
                        name.to_string(),
                        ControllerMethodDescriptor {
                            span: method.value.span,
                            requires_results_import: function_requires_results_import(
                                &method.value,
                            ),
                        },
                    );
                }
            }
            ClassElement::PropertyDefinition(property) => {
                let supported_static_metadata = property.r#static
                    && !property.computed
                    && matches!(
                        property_key_name(&property.key),
                        Some("inject") | Some("dependencies")
                    );
                if !supported_static_metadata {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                        "controller classes only support methods and static inject/dependencies metadata",
                    )
                    .with_path(path)
                    .with_span(property.span));
                }
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                    "controller classes only support methods and static inject/dependencies metadata",
                )
                .with_path(path)
                .with_span(oxc_span::GetSpan::span(element)));
            }
        }
    }
    let name = identifier.name.as_str().to_string();
    state
        .helper_sources
        .insert(name.clone(), class_source.clone());
    state.controllers.insert(
        name,
        ControllerDescriptor {
            source_name: source_name.to_string(),
            source_text: source.to_string(),
            methods,
        },
    );
    Ok(())
}

fn helper_initializer(expression: &Expression<'_>) -> Option<()> {
    match expression {
        Expression::ArrowFunctionExpression(_)
        | Expression::FunctionExpression(_)
        | Expression::StringLiteral(_)
        | Expression::NumericLiteral(_)
        | Expression::BooleanLiteral(_)
        | Expression::NullLiteral(_)
        | Expression::TemplateLiteral(_) => Some(()),
        _ if webhooks_event_initializer(expression) => Some(()),
        _ => None,
    }
}

fn webhooks_event_initializer(expression: &Expression<'_>) -> bool {
    let Expression::CallExpression(call) = expression else {
        return false;
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return false;
    };
    receiver == "Webhooks" && property == "event"
}

fn extract_expression_statement(
    path: &Path,
    source: &str,
    source_name: &str,
    graph: &mut ModuleGraph,
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

    if let Some(descriptor) = app_use_problem_details_call(path, &statement.expression, state)? {
        state.problem_details = Some(descriptor);
        return Ok(());
    }

    if app_use_request_id_call(path, source, source_name, &statement.expression, state)? {
        return Ok(());
    }

    if app_use_request_logging_call(path, source, source_name, &statement.expression, state)? {
        return Ok(());
    }

    if app_use_auth_provider_call(path, source, source_name, &statement.expression, state)? {
        return Ok(());
    }

    if app_auth_policy_call(path, source, &statement.expression, state)? {
        return Ok(());
    }

    if app_use_cors_call(path, &statement.expression, state)? {
        return Ok(());
    }

    if app_use_static_files_call(
        path,
        source,
        source_name,
        graph,
        &statement.expression,
        state,
    )? {
        return Ok(());
    }

    if let Some(routes) =
        app_map_controller_call(path, source, source_name, &statement.expression, state)?
    {
        if state.results_required_span.is_none() {
            state.results_required_span = routes.iter().find_map(|route| {
                route
                    .handler
                    .requires_results_import
                    .then_some(route.handler.span)
            });
        }
        state.routes.extend(routes);
        return Ok(());
    }

    if let Some(routes) = app_docs_call(path, source, source_name, &statement.expression, state)? {
        state.routes.extend(routes);
        return Ok(());
    }

    if app_use_middleware_call(path, source, source_name, &statement.expression, state)? {
        return Ok(());
    }

    if route_group_auth_call(path, &statement.expression, state)? {
        return Ok(());
    }

    if let Some(routes) =
        app_map_health_checks_call(path, source, source_name, &statement.expression, state)?
    {
        state.uses_health = true;
        state.routes.extend(routes);
        return Ok(());
    }

    if let Some(routes) =
        app_health_expose_call(path, source, source_name, &statement.expression, state)?
    {
        state.uses_health = true;
        state.routes.extend(routes);
        return Ok(());
    }

    if let Some(routes) =
        app_management_call(path, source, source_name, &statement.expression, state)?
    {
        state.uses_health = state.uses_health || routes.iter().any(|route| route.health.is_some());
        state.routes.extend(routes);
        return Ok(());
    }

    if let Some((module_name, span)) = app_use_module_call(&statement.expression, state) {
        state.used_modules.push((module_name, span));
        return Ok(());
    }

    if orm_relation_metadata_call(path, source, statement, state)? {
        return Ok(());
    }

    if let Some(registration) =
        app_service_registration_call(path, source, source_name, &statement.expression, state)?
    {
        state.service_registrations.push(registration);
        return Ok(());
    }

    if let Some(diagnostic) =
        unsupported_framework_feature_diagnostic(path, &statement.expression, state)
    {
        return Err(diagnostic);
    }

    let (route_expr, fluent_metadata) = route_metadata_chain(&statement.expression)
        .map_err(|diagnostic| diagnostic.with_path(path))?;

    let Some((receiver, method, kind, pattern, route_metadata, handler_arg)) =
        route_call_parts(route_expr, source, &state.static_strings)
            .map_err(|diagnostic| diagnostic.with_path(path))?
    else {
        if record_dynamic_route_if_supported(path, source, source_name, route_expr, state)? {
            return Ok(());
        }
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

    let (full_pattern, mut tags, route_middleware, inherited_auth) =
        if state.app_vars.contains(receiver) {
            (
                pattern.to_string(),
                Vec::new(),
                state.middleware.clone(),
                None,
            )
        } else if let Some(group) = state.group_vars.get(receiver) {
            let mut middleware = state.middleware.clone();
            middleware.extend(group.middleware.clone());
            (
                join_route_patterns(&group.prefix, &pattern),
                group.tags.clone(),
                middleware,
                group.auth.clone(),
            )
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
        .with_hint("Use '/', static segments, {name}, {name:str}, {name:int}, {name:uuid}, {name:alpha}, or {name:float}."));
    }

    let handler_context = HandlerExtractionContext {
        route_pattern: &full_pattern,
        route_kind: kind,
        source,
        source_name,
        allow_data_handler_body: state.data_imported,
        schema_names: &state.schema_names,
        provider_bindings: &state.provider_bindings,
        helper_effects: &state.helper_effects,
    };
    validate_handler_body_validate_schema_references(path, handler_arg, &state.schema_names)?;
    let Some(handler) = handler_from_argument(handler_arg, &handler_context) else {
        let diagnostic = handler_diagnostic(
            path,
            handler_arg,
            &full_pattern,
            &state.schema_names,
            statement.span,
        );
        if !handler_metadata_failure_can_fallback_to_dynamic(
            handler_arg,
            source,
            state,
            &diagnostic,
        ) {
            return Err(diagnostic);
        }
        state.dynamic_routes.push(DynamicRoute {
            method: Some(method),
            pattern: Some(full_pattern.clone()),
            pattern_reason: "route pattern is statically known",
            handler_known: false,
            reason: "route handler response metadata is computed at runtime",
            span: statement.span,
            source_name: source_name.to_string(),
            source: source.to_string(),
        });
        return Ok(());
    };

    let mut handler = handler;
    wrap_realtime_handler(&mut handler, kind, &route_metadata);
    apply_route_schema_metadata(
        path,
        statement.span,
        &state.schema_names,
        &fluent_metadata,
        &mut handler,
    )?;
    if handler.requires_results_import && state.results_required_span.is_none() {
        state.results_required_span = Some(handler.span);
    }
    let helper_sources =
        helper_sources_referenced_by_handler(&handler.emitted_source, &state.helper_sources);
    if !handler.effects.is_empty() || !helper_sources.is_empty() {
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
        handler.emitted_source = wrap_handler_with_providers_and_helpers(
            &handler.emitted_source,
            &providers,
            &helper_sources,
            handler.is_async,
        );
    }
    let contract_metadata = merged_route_metadata(&route_metadata, &fluent_metadata);
    tags.extend(contract_metadata.tags.clone());
    let auth = contract_metadata.auth.clone().or(inherited_auth);
    let cors = state.cors_policy.clone();
    if let Some(requirement) = &auth {
        if requirement.required {
            force_auth_required_route_v8_dispatch(&mut handler);
            ensure_auth_required_route_request_context(&mut handler);
        }
        handler.emitted_source = wrap_handler_with_auth(&handler.emitted_source, requirement);
        handler.is_async = true;
    }
    handler.emitted_source = wrap_handler_with_framework_pipeline(
        &handler.emitted_source,
        &route_middleware,
        cors.as_ref(),
    );
    if !route_middleware.is_empty() || cors.is_some() {
        handler.is_async = true;
    }

    state.routes.push(Route {
        method,
        kind,
        websocket: contract_metadata.websocket.clone(),
        realtime: realtime_route_metadata_from_contract(&contract_metadata),
        framework_path: (normalized_pattern != full_pattern).then_some(full_pattern),
        pattern: normalized_pattern,
        name: contract_metadata.name.clone(),
        tags,
        summary: contract_metadata.summary.clone(),
        description: contract_metadata.description.clone(),
        deprecated: contract_metadata.deprecated.clone(),
        consumes: contract_metadata.consumes.clone(),
        produces: contract_metadata.produces.clone(),
        headers: contract_metadata.headers.clone(),
        query_schema: contract_metadata.query_schema.clone(),
        params_schema: contract_metadata.params_schema.clone(),
        openapi_override: contract_metadata.openapi_override.clone(),
        output_cache: contract_metadata.output_cache.clone(),
        cache_headers: contract_metadata.cache_headers.clone(),
        rate_limits: contract_metadata.rate_limits.clone(),
        docs: None,
        health: None,
        middleware: route_middleware_metadata(&route_middleware),
        auth,
        cors: cors.as_ref().map(cors_policy_metadata),
        cors_preflight: false,
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
                "computed route registration method cannot be emitted as a runnable static route",
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
                .with_hint("Supported compiler methods are mapGet, mapPost, mapPut, mapPatch, mapDelete, sse, and ws."),
            );
        }
        return None;
    }

    if !matches!(call.arguments.len(), 2 | 3) {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_SHAPE",
                "route declarations require a literal pattern, optional metadata, and one handler",
            )
            .with_path(path)
            .with_span(call.span),
        );
    }

    if call.arguments.first().and_then(string_argument).is_none() {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_DYNAMIC_ROUTE_PATTERN",
                "route pattern could not be emitted as a runnable route",
            )
            .with_path(path)
            .with_span(call.span)
            .with_hint("Dynamic route strings are not part of the supported app compiler."),
        );
    }

    let handler_index = if call.arguments.len() == 3 { 2 } else { 1 };
    if call
        .arguments
        .get(handler_index)
        .and_then(|argument| {
            let context = HandlerExtractionContext {
                route_pattern: call
                    .arguments
                    .first()
                    .and_then(string_argument)
                    .unwrap_or_default(),
                route_kind: crate::slop_dsl::route_kind_from_property(property).unwrap_or("http"),
                source,
                source_name: "",
                allow_data_handler_body: state.data_imported,
                schema_names: &state.schema_names,
                provider_bindings: &state.provider_bindings,
                helper_effects: &state.helper_effects,
            };
            handler_from_argument(argument, &context)
        })
        .is_none()
    {
        let handler_argument = call.arguments.get(handler_index)?;
        return Some(handler_diagnostic(
            path,
            handler_argument,
            call.arguments
                .first()
                .and_then(string_argument)
                .unwrap_or_default(),
            &state.schema_names,
            call.span,
        ));
    }

    None
}

fn record_dynamic_route_if_supported(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(false);
    };

    let (receiver, method) = if let Some(receiver) = computed_member_receiver(&call.callee) {
        if !(state.app_vars.contains(receiver) || state.group_vars.contains_key(receiver)) {
            return Ok(false);
        }
        (receiver, None)
    } else if let Some((receiver, property)) = static_member_name(&call.callee) {
        let Some(method) = route_method_from_property(property) else {
            return Ok(false);
        };
        (receiver, Some(method))
    } else {
        return Ok(false);
    };

    if !(state.app_vars.contains(receiver) || state.group_vars.contains_key(receiver)) {
        return Ok(false);
    }
    if !matches!(call.arguments.len(), 2 | 3) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_SHAPE",
            "route declarations require a literal or dynamic pattern, optional metadata, and one handler",
        )
        .with_path(path)
        .with_span(call.span));
    }

    let handler_index = if call.arguments.len() == 3 { 2 } else { 1 };
    let handler_known = call
        .arguments
        .get(handler_index)
        .and_then(|argument| {
            let context = HandlerExtractionContext {
                route_pattern: "",
                route_kind: "http",
                source,
                source_name,
                allow_data_handler_body: state.data_imported,
                schema_names: &state.schema_names,
                provider_bindings: &state.provider_bindings,
                helper_effects: &state.helper_effects,
            };
            handler_from_argument(argument, &context)
        })
        .is_some();

    state.dynamic_routes.push(DynamicRoute {
        method,
        pattern: None,
        pattern_reason: "route pattern is computed at runtime",
        handler_known,
        reason: "route registration is runnable JavaScript but cannot be fully represented in static route metadata",
        span: call.span,
        source_name: source_name.to_string(),
        source: source.to_string(),
    });
    Ok(true)
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
    if let Some((_, _, _, _, _)) = route_call(
        init,
        source,
        source_name,
        state.data_imported,
        &state.schema_names,
        &state.provider_bindings,
        &state.helper_effects,
        &state.static_strings,
    )
    .map_err(|diagnostic| diagnostic.with_path(path))?
    {
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

mod app_features;
use app_features::*;
mod framework;
use framework::{
    app_map_controller_call, app_use_cors_call, app_use_middleware_call, app_use_request_id_call,
    app_use_request_logging_call, append_cors_preflight_routes, auth_policies_js,
    auth_schemes_json, cors_policy_metadata, route_middleware_metadata,
    wrap_handler_with_framework_pipeline,
};
mod static_files;
use static_files::app_use_static_files_call;
#[cfg(test)]
use static_files::STATIC_ASSET_INLINE_MAX_BYTES;
mod auth;
use auth::{app_auth_policy_call, app_use_auth_provider_call, route_group_auth_call};
mod ops_docs;
use ops_docs::{
    app_docs_call, app_health_expose_call, app_management_call, app_map_health_checks_call,
    apply_problem_details_to_routes,
};

fn resolve_relative_import(from_path: &Path, specifier: &str) -> Option<PathBuf> {
    resolver::resolve_relative_import(from_path, specifier)
}

mod modules;
#[cfg(test)]
use modules::source_contains_identifier;
use modules::{
    ensure_auth_required_route_request_context, extract_relative_helper_import,
    extract_relative_module, force_auth_required_route_v8_dispatch,
    helper_sources_in_dependency_order, helper_sources_referenced_by_handler,
    provider_has_generated_runtime_bridge, providers_used_by_effects, wrap_handler_with_auth,
    wrap_handler_with_providers_and_helpers,
};
fn wrap_realtime_handler(handler: &mut Handler, kind: &str, metadata: &RouteMetadata) {
    match kind {
        "sse" => {
            handler.emitted_source = format!("Realtime.sse({})", handler.emitted_source);
            handler.is_async = true;
        }
        "websocket" => {
            if let Some(channel_source) = &metadata.realtime_channel_source {
                let options_source = metadata
                    .realtime_options_source
                    .as_deref()
                    .unwrap_or("undefined");
                handler.emitted_source = format!(
                    "Realtime.__route({}, {}, {})",
                    channel_source, handler.emitted_source, options_source
                );
            } else {
                handler.emitted_source = format!("Realtime.websocket({})", handler.emitted_source);
            }
            handler.is_async = true;
        }
        _ => {}
    }
}

mod routes;
use routes::{
    anonymous_auth_requirement, apply_route_schema_metadata, auth_requirement_from_call,
    auth_requirement_from_object, duplicate_schema_diagnostic, expression_string_literal,
    merge_auth_requirement, merged_route_metadata, realtime_route_metadata_from_contract,
    route_call, route_call_parts, route_metadata_chain, route_metadata_from_options_argument,
    route_method_from_property, route_tags_from_arguments, route_tags_from_expression,
    unresolved_schema_diagnostic,
};
fn static_member_name<'a>(expression: &'a Expression<'a>) -> Option<(&'a str, &'a str)> {
    crate::slop_dsl::static_member_name(expression)
}

fn static_member_expression<'a>(
    expression: &'a Expression<'a>,
) -> Option<(&'a Expression<'a>, &'a str)> {
    let Expression::StaticMemberExpression(member) = expression else {
        return None;
    };
    Some((&member.object, member.property.name.as_str()))
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

fn object_integer_property_value(
    object: &oxc_ast::ast::ObjectExpression<'_>,
    name: &str,
) -> Option<i64> {
    object_json_property_value(object, name).and_then(|value| {
        value.as_i64().or_else(|| {
            let number = value.as_f64()?;
            if number.fract() == 0.0 {
                Some(number as i64)
            } else {
                None
            }
        })
    })
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
            .without_source_frame()
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
            .next()
            .is_some_and(|byte| byte.is_ascii_alphabetic() || byte == b'_')
        && name
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || byte == b'_')
        && matches!(kind, "str" | "int" | "uuid" | "alpha" | "float")
}

mod handler;
use handler::*;

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

impl AstSpan for Declaration<'_> {
    fn span(&self) -> Span {
        match self {
            Declaration::VariableDeclaration(node) => node.span,
            Declaration::FunctionDeclaration(node) => node.span,
            Declaration::ClassDeclaration(node) => node.span,
            Declaration::TSTypeAliasDeclaration(node) => node.span,
            Declaration::TSInterfaceDeclaration(node) => node.span,
            Declaration::TSEnumDeclaration(node) => node.span,
            Declaration::TSModuleDeclaration(node) => node.span,
            Declaration::TSGlobalDeclaration(node) => node.span,
            Declaration::TSImportEqualsDeclaration(node) => node.span,
        }
    }
}

impl AstSpan for oxc_ast::ast::ExportDefaultDeclarationKind<'_> {
    fn span(&self) -> Span {
        match self {
            oxc_ast::ast::ExportDefaultDeclarationKind::FunctionDeclaration(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::ClassDeclaration(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::TSInterfaceDeclaration(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::BooleanLiteral(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::NullLiteral(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::NumericLiteral(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::BigIntLiteral(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::RegExpLiteral(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::StringLiteral(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::TemplateLiteral(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::Identifier(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::MetaProperty(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::Super(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::ArrayExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::ArrowFunctionExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::AssignmentExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::AwaitExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::BinaryExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::CallExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::ChainExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::ClassExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::ConditionalExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::FunctionExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::ImportExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::LogicalExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::NewExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::ObjectExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::ParenthesizedExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::SequenceExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::TaggedTemplateExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::ThisExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::UnaryExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::UpdateExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::YieldExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::PrivateInExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::JSXElement(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::JSXFragment(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::TSAsExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::TSSatisfiesExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::TSTypeAssertion(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::TSNonNullExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::TSInstantiationExpression(node) => {
                node.span
            }
            oxc_ast::ast::ExportDefaultDeclarationKind::V8IntrinsicExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::ComputedMemberExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::StaticMemberExpression(node) => node.span,
            oxc_ast::ast::ExportDefaultDeclarationKind::PrivateFieldExpression(node) => node.span,
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

mod emit;
use emit::write_artifacts;
#[cfg(test)]
use emit::{emit_app_js, emit_source_map};
#[cfg(test)]
#[path = "sloppyc_tests.rs"]
mod tests;
