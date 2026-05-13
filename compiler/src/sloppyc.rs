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

fn schema_names(state: &AppState) -> BTreeSet<String> {
    state.schema_names.clone()
}

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
                "net" => self.uses_net_runtime = true,
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
            "net" => app.uses_net_runtime = true,
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
                    if source_has_sloppy_web_import(path, source)?
                        && web_error_indicates_missing_web_shape(&web_error)
                    {
                        return Err(Diagnostic::new(
                        "SLOPPYC_E_AMBIGUOUS_SOURCE_KIND",
                        "This source imports Sloppy but does not export a supported web app shape.",
                    )
                    .with_path(path)
                    .with_hint("Use --kind program to run it as a program, or export a Sloppy app."));
                    }
                    if source_has_sloppy_web_import(path, source)? {
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

fn source_has_sloppy_web_import(path: &Path, source: &str) -> Result<bool, Diagnostic> {
    let source_type = source_type_for_path(path, ParseContext::Entry)?;
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, source_type).parse();
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(
            Diagnostic::new("SLOPPYC_E_PARSE", format!("failed to parse input: {error}"))
                .with_path(path),
        );
    }
    Ok(parsed.program.body.iter().any(|statement| {
        matches!(
            statement,
            Statement::ImportDeclaration(import) if import.source.value.as_str() == "sloppy"
        )
    }))
}

fn extract_program_with_metrics(
    path: &Path,
    source: &str,
    options: &CompileOptions,
    mut metrics: Option<&mut CompileMetrics>,
) -> Result<ExtractedApp, Diagnostic> {
    let parse_start = Instant::now();
    let source_type = source_type_for_path(path, ParseContext::Entry)?;
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
    let extract_start = Instant::now();
    let mut graph = ModuleGraph::new(path, options.config_dir.as_deref());
    let mut modules = Vec::new();
    let mut visiting = BTreeSet::new();
    extract_program_module(path, source, &mut graph, &mut visiting, &mut modules)?;
    apply_dependency_includes(path, &mut graph, &mut visiting, &mut modules, options)?;
    ensure_package_global_shims(path, &mut graph, &mut modules);
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("extractMs", extract_start.elapsed());
    }
    let source_files = graph.source_files.clone();
    let capabilities = program_inferred_capabilities(&graph);
    let dependency_graph = graph.dependency_graph.clone();
    let program_entry = program_module_id(&graph, path, None);
    Ok(ExtractedApp {
        kind: ProjectKind::Program,
        program_entry: Some(program_entry),
        program_modules: modules,
        uses_data_runtime: graph.uses_data_runtime,
        uses_sql_runtime: graph.uses_sql_runtime,
        uses_orm_runtime: graph.uses_orm_runtime,
        orm_tables: Vec::new(),
        orm_relations: Vec::new(),
        orm_extraction_partial: graph.uses_orm_runtime,
        uses_migrations_runtime: graph.uses_migrations_runtime,
        uses_provider_health_runtime: graph.uses_provider_health_runtime,
        source_files,
        routes: Vec::new(),
        dynamic_routes: Vec::new(),
        dynamic_entry_source: None,
        service_registrations: Vec::new(),
        modules: Vec::new(),
        helper_sources: Vec::new(),
        capabilities,
        configuration: None,
        schemas: Vec::new(),
        config_reads: Vec::new(),
        uses_time_runtime: graph.uses_time_runtime,
        uses_fs_runtime: graph.uses_fs_runtime,
        uses_crypto_runtime: graph.uses_crypto_runtime,
        noncrypto_hash_security_context_visible: graph.noncrypto_hash_security_context_visible,
        uses_codec_runtime: graph.uses_codec_runtime,
        checksum_security_context_visible: graph.checksum_security_context_visible,
        uses_cache_runtime: graph.uses_cache_runtime,
        uses_net_runtime: graph.uses_net_runtime,
        uses_os_runtime: graph.uses_os_runtime,
        uses_http_client_runtime: graph.uses_http_client_runtime,
        uses_realtime_runtime: graph.uses_realtime_runtime,
        uses_workers_runtime: graph.uses_workers_runtime,
        uses_ffi_runtime: graph.uses_ffi_runtime,
        ffi: graph.ffi_libraries,
        ffi_structs: graph.ffi_structs,
        uses_health: false,
        auth: AuthMetadata::default(),
        problem_details: None,
        dependency_graph,
    })
}

fn ensure_package_global_shims(
    path: &Path,
    graph: &mut ModuleGraph,
    modules: &mut Vec<ProgramModule>,
) {
    if graph.dependency_graph.packages.is_empty() {
        return;
    }
    for specifier in ["node:process", "node:buffer"] {
        if let Some(builtin) = resolver::resolve_node_builtin(specifier) {
            graph.add_node_builtin(&builtin, path);
            if let Some(backing) = builtin.backing {
                ensure_node_compat_module(backing, graph, modules);
            }
        }
    }
}

fn program_inferred_capabilities(graph: &ModuleGraph) -> Vec<DatabaseCapability> {
    let mut capabilities = Vec::new();
    if graph.uses_fs_runtime {
        push_program_inferred_capability(&mut capabilities, "fs");
    }
    if graph.uses_net_runtime || graph.uses_http_client_runtime {
        push_program_inferred_capability(&mut capabilities, "net");
    }
    if graph.uses_os_runtime {
        push_program_inferred_capability(&mut capabilities, "os");
    }
    if graph.uses_time_runtime {
        push_program_inferred_capability(&mut capabilities, "time");
    }
    if graph.uses_crypto_runtime {
        push_program_inferred_capability(&mut capabilities, "crypto");
    }
    if graph.uses_codec_runtime {
        push_program_inferred_capability(&mut capabilities, "codec");
    }
    if graph.uses_workers_runtime {
        push_program_inferred_capability(&mut capabilities, "workers");
    }
    if graph.uses_ffi_runtime || !graph.ffi_libraries.is_empty() || !graph.ffi_structs.is_empty() {
        push_program_inferred_capability(&mut capabilities, "ffi");
    }
    capabilities
}

fn push_program_inferred_capability(capabilities: &mut Vec<DatabaseCapability>, token: &str) {
    let Some((kind, access)) = declared_capability_shape(token) else {
        return;
    };
    capabilities.push(DatabaseCapability {
        token: token.to_string(),
        capability_kind: kind.to_string(),
        provider: String::new(),
        config_name: None,
        config_key: None,
        access: access.to_string(),
        database: None,
        config_source: None,
        source_name: "program import".to_string(),
        source: format!("import:{token}"),
        span: Span::new(0, 0),
        from_provider_use: false,
    });
}

fn include_pattern_is_safe(pattern: &str) -> bool {
    let pattern = pattern.trim();
    if pattern.is_empty() || pattern.as_bytes().contains(&0) {
        return false;
    }
    let normalized = pattern.replace('\\', "/");
    if normalized.starts_with('/') || normalized.starts_with("//") {
        return false;
    }
    if normalized.len() >= 2 && normalized.as_bytes()[1] == b':' {
        return false;
    }
    !normalized
        .split('/')
        .any(|segment| segment == ".." || segment.is_empty())
}

fn apply_dependency_includes(
    entry_path: &Path,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    options: &CompileOptions,
) -> Result<(), Diagnostic> {
    for pattern in &options.module_include {
        let matches = collect_include_matches(&graph.entry_dir, pattern, IncludeKind::Module)
            .map_err(|error| {
                Diagnostic::new(
                    "SLOPPYC_E_INCLUDE",
                    format!("failed to evaluate moduleInclude pattern '{pattern}': {error}"),
                )
                .with_path(entry_path)
            })?;
        if matches.is_empty() {
            graph
                .dependency_graph
                .compatibility_findings
                .push(CompatibilityFinding {
                    code: "SLOPPYC_W_MODULE_INCLUDE_EMPTY".to_string(),
                    severity: "warning".to_string(),
                    message: format!("moduleInclude pattern '{pattern}' matched no modules."),
                    source: Some("sloppy.json".to_string()),
                    package: None,
                    specifier: Some(pattern.clone()),
                    hint: Some(
                        "Check the project-relative glob or remove the include.".to_string(),
                    ),
                });
            continue;
        }
        for module_path in matches {
            let source = fs::read_to_string(&module_path).map_err(|error| {
                Diagnostic::new(
                    "SLOPPYC_E_INPUT",
                    format!("failed to read moduleInclude file: {error}"),
                )
                .with_path(&module_path)
            })?;
            extract_program_module_with_context(
                &module_path,
                &source,
                graph,
                visiting,
                modules,
                None,
                Some(format!("moduleInclude:{pattern}")),
            )?;
        }
    }

    for pattern in &options.asset_include {
        let matches = collect_include_matches(&graph.entry_dir, pattern, IncludeKind::Asset)
            .map_err(|error| {
                Diagnostic::new(
                    "SLOPPYC_E_INCLUDE",
                    format!("failed to evaluate assetInclude pattern '{pattern}': {error}"),
                )
                .with_path(entry_path)
            })?;
        if matches.is_empty() {
            graph
                .dependency_graph
                .compatibility_findings
                .push(CompatibilityFinding {
                    code: "SLOPPYC_W_ASSET_INCLUDE_EMPTY".to_string(),
                    severity: "warning".to_string(),
                    message: format!("assetInclude pattern '{pattern}' matched no assets."),
                    source: Some("sloppy.json".to_string()),
                    package: None,
                    specifier: Some(pattern.clone()),
                    hint: Some(
                        "Check the project-relative glob or remove the include.".to_string(),
                    ),
                });
            continue;
        }
        for asset_path in matches {
            graph.add_dependency_asset(&asset_path, format!("assetInclude:{pattern}"));
        }
    }

    Ok(())
}

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
enum IncludeKind {
    Module,
    Asset,
}

fn collect_include_matches(
    root: &Path,
    pattern: &str,
    kind: IncludeKind,
) -> Result<Vec<PathBuf>, std::io::Error> {
    let normalized_pattern = pattern.replace('\\', "/");
    let include_node_modules =
        normalized_pattern == "node_modules" || normalized_pattern.starts_with("node_modules/");
    let mut files = Vec::new();
    collect_include_files(root, root, include_node_modules, &mut files)?;
    let mut matches = files
        .into_iter()
        .filter(|path| {
            if kind == IncludeKind::Module && !module_include_extension_supported(path) {
                return false;
            }
            let relative = resolver::normalized_artifact_id(path, root);
            glob_match(&normalized_pattern, &relative)
        })
        .collect::<Vec<_>>();
    matches.sort_by_key(|path| resolver::normalized_artifact_id(path, root));
    Ok(matches)
}

fn collect_include_files(
    root: &Path,
    dir: &Path,
    include_node_modules: bool,
    files: &mut Vec<PathBuf>,
) -> Result<(), std::io::Error> {
    let mut entries = fs::read_dir(dir)?.collect::<Result<Vec<_>, _>>()?;
    entries.sort_by_key(|entry| entry.file_name());
    for entry in entries {
        let path = entry.path();
        let file_type = entry.file_type()?;
        if file_type.is_dir() {
            let name = entry.file_name();
            let name = name.to_string_lossy();
            if matches!(name.as_ref(), ".git" | ".sloppy" | "target")
                || (name == "node_modules" && !include_node_modules)
            {
                continue;
            }
            if resolver::stays_within_source_root(&path, root) {
                collect_include_files(root, &path, include_node_modules, files)?;
            }
        } else if file_type.is_file() && resolver::stays_within_source_root(&path, root) {
            files.push(path);
        }
    }
    Ok(())
}

fn module_include_extension_supported(path: &Path) -> bool {
    matches!(
        path.extension()
            .and_then(|extension| extension.to_str())
            .unwrap_or(""),
        "js" | "mjs" | "cjs" | "ts" | "tsx" | "json"
    )
}

fn glob_match(pattern: &str, value: &str) -> bool {
    let pattern = pattern.trim_matches('/');
    let value = value.trim_matches('/');
    let pattern_segments = pattern.split('/').collect::<Vec<_>>();
    let value_segments = value.split('/').collect::<Vec<_>>();
    glob_segments_match(&pattern_segments, &value_segments)
}

fn glob_segments_match(pattern: &[&str], value: &[&str]) -> bool {
    if pattern.is_empty() {
        return value.is_empty();
    }
    if pattern[0] == "**" {
        return glob_segments_match(&pattern[1..], value)
            || (!value.is_empty() && glob_segments_match(pattern, &value[1..]));
    }
    !value.is_empty()
        && glob_segment_match(pattern[0], value[0])
        && glob_segments_match(&pattern[1..], &value[1..])
}

fn glob_segment_match(pattern: &str, value: &str) -> bool {
    fn inner(pattern: &[u8], value: &[u8]) -> bool {
        match pattern.split_first() {
            None => value.is_empty(),
            Some((&b'*', rest)) => {
                inner(rest, value) || (!value.is_empty() && inner(pattern, &value[1..]))
            }
            Some((&b'?', rest)) => !value.is_empty() && inner(rest, &value[1..]),
            Some((&literal, rest)) => {
                value.first().is_some_and(|candidate| *candidate == literal)
                    && inner(rest, &value[1..])
            }
        }
    }
    inner(pattern.as_bytes(), value.as_bytes())
}

fn extract_program_module(
    path: &Path,
    source: &str,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
) -> Result<(), Diagnostic> {
    extract_program_module_with_context(path, source, graph, visiting, modules, None, None)
}

fn extract_program_module_with_context(
    path: &Path,
    source: &str,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
    included_by: Option<String>,
) -> Result<(), Diagnostic> {
    let canonical = fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
    let package_type = if package.is_some() {
        resolver::package_type_for_path(&canonical)
    } else {
        "module".to_string()
    };
    let format = resolver::module_format_for_path(&canonical, &package_type);
    let id = if included_by.is_some() && package.is_none() {
        resolver::normalized_artifact_id(&canonical, &graph.entry_dir)
    } else {
        program_module_id(graph, &canonical, package.as_deref())
    };
    if modules.iter().any(|module| module.id == id) {
        return Ok(());
    }
    if !visiting.insert(canonical.clone()) {
        graph.dependency_graph.ensure_defaults();
        graph
            .dependency_graph
            .compatibility_findings
            .push(CompatibilityFinding {
                code: "SLOPPYC_W_MODULE_CYCLE_BASIC".to_string(),
                severity: "warning".to_string(),
                message:
                    "module cycle uses Sloppy's basic module-cache semantics; exact ESM temporal-dead-zone behavior is not guaranteed yet"
                        .to_string(),
                source: Some(id),
                package: package.clone(),
                specifier: None,
                hint: Some("Avoid depending on uninitialized ESM bindings across cycles.".to_string()),
            });
        return Ok(());
    }
    let source_name = graph.record_source(&canonical, source);
    let transformed = transform_program_source(
        &canonical,
        source,
        graph,
        visiting,
        modules,
        package.clone(),
        format,
    )?;
    visiting.remove(&canonical);
    if graph.dependency_graph.has_entries()
        || package.is_some()
        || included_by.is_some()
        || format != ModuleFormat::Esm
    {
        graph.add_dependency_module(
            id.clone(),
            resolver::normalized_artifact_id(&canonical, &graph.entry_dir),
            format,
            package.clone(),
            included_by,
        );
    }
    modules.push(ProgramModule {
        id,
        source_name,
        source: source.to_string(),
        emitted_source: transformed,
        format,
    });
    Ok(())
}

fn program_module_id(graph: &ModuleGraph, path: &Path, _package: Option<&str>) -> String {
    resolver::normalized_artifact_id(path, &graph.entry_dir)
}

fn transform_program_source(
    path: &Path,
    source: &str,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
    format: ModuleFormat,
) -> Result<String, Diagnostic> {
    if format == ModuleFormat::Json {
        let value: Value = serde_json::from_str(source).map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_PARSE",
                format!("failed to parse JSON module: {error}"),
            )
            .with_path(path)
        })?;
        return Ok(format!(
            "module.exports = {};\n",
            serde_json::to_string(&value).unwrap_or_else(|_| "null".to_string())
        ));
    }
    if format == ModuleFormat::CommonJs {
        analyze_commonjs_requires(path, source, graph, visiting, modules, package.clone())?;
        return Ok(source.to_string());
    }
    let source_type = source_type_for_path(path, ParseContext::Module)?;
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, source_type).parse();
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_PARSE",
            format!("failed to parse program module: {error}"),
        )
        .with_path(path));
    }

    let mut program = parsed.program;
    let source_name = graph.record_source(path, source);
    extract_ffi_declarations_from_statements(
        path,
        source,
        &source_name,
        &program.body,
        &mut graph.ffi_libraries,
        &mut graph.ffi_structs,
    )?;
    analyze_program_imports(
        path,
        &program.body,
        graph,
        visiting,
        modules,
        package.clone(),
    )?;
    let transformed = transpile_program_typescript(path, &allocator, &mut program)?;
    rewrite_program_module_exports(path, &transformed, graph, package)
}

fn analyze_program_imports(
    path: &Path,
    statements: &[Statement<'_>],
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    for statement in statements {
        analyze_statement_dynamic_imports(
            path,
            statement,
            graph,
            visiting,
            modules,
            package.clone(),
        )?;
        if let Statement::ImportDeclaration(import) = statement {
            analyze_program_import(path, import, graph, visiting, modules, package.clone())?;
        } else if let Statement::ExportNamedDeclaration(export) = statement {
            if export.export_kind != ImportOrExportKind::Type {
                if let Some(source) = &export.source {
                    let has_runtime_reexport = export
                        .specifiers
                        .iter()
                        .any(|specifier| specifier.export_kind != ImportOrExportKind::Type);
                    if !has_runtime_reexport {
                        continue;
                    }
                    resolve_program_dependency(
                        ProgramDependencyRequest {
                            path,
                            specifier: source.value.as_str(),
                            package: package.clone(),
                            import_mode: true,
                            span: source.span,
                        },
                        graph,
                        visiting,
                        modules,
                    )?;
                }
            }
        } else if let Statement::ExportAllDeclaration(export) = statement {
            if export.export_kind != ImportOrExportKind::Type {
                resolve_program_dependency(
                    ProgramDependencyRequest {
                        path,
                        specifier: export.source.value.as_str(),
                        package: package.clone(),
                        import_mode: true,
                        span: export.source.span,
                    },
                    graph,
                    visiting,
                    modules,
                )?;
            }
        }
    }
    Ok(())
}

fn analyze_statement_dynamic_imports(
    path: &Path,
    statement: &Statement<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    match statement {
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    analyze_expression_dynamic_imports(
                        path,
                        init,
                        graph,
                        visiting,
                        modules,
                        package.clone(),
                    )?;
                }
            }
        }
        Statement::ExpressionStatement(statement) => analyze_expression_dynamic_imports(
            path,
            &statement.expression,
            graph,
            visiting,
            modules,
            package,
        )?,
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                analyze_expression_dynamic_imports(
                    path, argument, graph, visiting, modules, package,
                )?;
            }
        }
        Statement::BlockStatement(block) => {
            for statement in &block.body {
                analyze_statement_dynamic_imports(
                    path,
                    statement,
                    graph,
                    visiting,
                    modules,
                    package.clone(),
                )?;
            }
        }
        Statement::FunctionDeclaration(function) => {
            if let Some(body) = &function.body {
                for statement in &body.statements {
                    analyze_statement_dynamic_imports(
                        path,
                        statement,
                        graph,
                        visiting,
                        modules,
                        package.clone(),
                    )?;
                }
            }
        }
        Statement::ExportNamedDeclaration(export) => {
            if let Some(declaration) = &export.declaration {
                analyze_declaration_dynamic_imports(
                    path,
                    declaration,
                    graph,
                    visiting,
                    modules,
                    package,
                )?;
            }
        }
        _ => {}
    }
    Ok(())
}

fn analyze_declaration_dynamic_imports(
    path: &Path,
    declaration: &Declaration<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    match declaration {
        Declaration::FunctionDeclaration(function) => {
            if let Some(body) = &function.body {
                for statement in &body.statements {
                    analyze_statement_dynamic_imports(
                        path,
                        statement,
                        graph,
                        visiting,
                        modules,
                        package.clone(),
                    )?;
                }
            }
        }
        Declaration::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    analyze_expression_dynamic_imports(
                        path,
                        init,
                        graph,
                        visiting,
                        modules,
                        package.clone(),
                    )?;
                }
            }
        }
        _ => {}
    }
    Ok(())
}

fn analyze_expression_dynamic_imports(
    path: &Path,
    expression: &Expression<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    match expression {
        Expression::ImportExpression(import) => {
            let from_id = program_module_id(graph, path, package.as_deref());
            if let Some(specifier) = expression_string_literal(&import.source) {
                let resolved_id = resolve_program_dependency(
                    ProgramDependencyRequest {
                        path,
                        specifier,
                        package,
                        import_mode: true,
                        span: import.span,
                    },
                    graph,
                    visiting,
                    modules,
                )?;
                graph.add_dynamic_import(
                    &from_id,
                    Some(specifier.to_string()),
                    resolved_id,
                    "string-literal",
                );
            } else {
                graph.add_dynamic_import(&from_id, None, None, "computed");
            }
        }
        Expression::AwaitExpression(await_expression) => analyze_expression_dynamic_imports(
            path,
            &await_expression.argument,
            graph,
            visiting,
            modules,
            package,
        )?,
        Expression::CallExpression(call) => {
            analyze_expression_dynamic_imports(
                path,
                &call.callee,
                graph,
                visiting,
                modules,
                package.clone(),
            )?;
            for argument in &call.arguments {
                analyze_argument_dynamic_imports(
                    path,
                    argument,
                    graph,
                    visiting,
                    modules,
                    package.clone(),
                )?;
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => analyze_expression_dynamic_imports(
            path,
            &parenthesized.expression,
            graph,
            visiting,
            modules,
            package,
        )?,
        Expression::ArrowFunctionExpression(function) => {
            for statement in &function.body.statements {
                analyze_statement_dynamic_imports(
                    path,
                    statement,
                    graph,
                    visiting,
                    modules,
                    package.clone(),
                )?;
            }
        }
        _ => {}
    }
    Ok(())
}

fn analyze_argument_dynamic_imports(
    path: &Path,
    argument: &Argument<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    match argument {
        Argument::ImportExpression(import) => {
            let from_id = program_module_id(graph, path, package.as_deref());
            if let Some(specifier) = expression_string_literal(&import.source) {
                let resolved_id = resolve_program_dependency(
                    ProgramDependencyRequest {
                        path,
                        specifier,
                        package,
                        import_mode: true,
                        span: import.span,
                    },
                    graph,
                    visiting,
                    modules,
                )?;
                graph.add_dynamic_import(
                    &from_id,
                    Some(specifier.to_string()),
                    resolved_id,
                    "string-literal",
                );
            } else {
                graph.add_dynamic_import(&from_id, None, None, "computed");
            }
        }
        Argument::CallExpression(call) => {
            for argument in &call.arguments {
                analyze_argument_dynamic_imports(
                    path,
                    argument,
                    graph,
                    visiting,
                    modules,
                    package.clone(),
                )?;
            }
        }
        Argument::ParenthesizedExpression(parenthesized) => analyze_expression_dynamic_imports(
            path,
            &parenthesized.expression,
            graph,
            visiting,
            modules,
            package,
        )?,
        _ => {}
    }
    Ok(())
}

fn analyze_program_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    let specifier = import.source.value.as_str();
    if !program_import_has_runtime_value(import) {
        return Ok(());
    }
    let import_kind = resolver::classify_import(path, specifier);
    let from_id = program_module_id(graph, path, package.as_deref());
    match import_kind {
        resolver::ImportKind::Relative(resolved) => {
            let source = fs::read_to_string(&resolved).map_err(|error| {
                Diagnostic::new(
                    "SLOPPYC_E_INPUT",
                    format!("failed to read program module: {error}"),
                )
                .with_path(&resolved)
            })?;
            let resolved_package = package.clone();
            let resolved_id = program_module_id(graph, &resolved, resolved_package.as_deref());
            graph.add_dependency_import(&from_id, specifier, &resolved_id, "relative");
            extract_program_module_with_context(
                &resolved,
                &source,
                graph,
                visiting,
                modules,
                resolved_package,
                Some(from_id),
            )?;
            Ok(())
        }
        resolver::ImportKind::SlopStdlib
        | resolver::ImportKind::SlopData
        | resolver::ImportKind::SlopTime
        | resolver::ImportKind::SlopFilesystem
        | resolver::ImportKind::SlopCrypto
        | resolver::ImportKind::SlopCodec
        | resolver::ImportKind::SlopCache
        | resolver::ImportKind::SlopNet
        | resolver::ImportKind::SlopHttp
        | resolver::ImportKind::SlopOs
        | resolver::ImportKind::SlopOrm
        | resolver::ImportKind::SlopWorkers
        | resolver::ImportKind::SlopFfi
        | resolver::ImportKind::SqliteProvider => {
            validate_program_stdlib_import(path, import, &import_kind)?;
            mark_program_import(graph, &import_kind, import);
            if matches!(import_kind, resolver::ImportKind::SqliteProvider) {
                ensure_sloppy_provider_module("sloppy/providers/sqlite", graph, modules);
                graph.add_dependency_import(
                    &from_id,
                    specifier,
                    "sloppy/providers/sqlite",
                    "sloppy-provider",
                );
            }
            Ok(())
        }
        resolver::ImportKind::NodeBuiltin(builtin) => {
            if builtin.status == "unsupported" {
                graph.add_node_builtin(&builtin, path);
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN",
                    format!(
                        "{} is not supported by Sloppy's Node compatibility registry yet.",
                        builtin.specifier
                    ),
                )
                .with_path(path)
                .with_span(import.source.span)
                .with_hint(
                    "Use a Sloppy stdlib API or a package path that avoids this Node builtin.",
                ));
            }
            graph.add_node_builtin(&builtin, path);
            if let Some(backing) = builtin.backing {
                ensure_node_compat_module(backing, graph, modules);
                graph.add_dependency_import(&from_id, specifier, backing, "node-compat-shim");
            }
            Ok(())
        }
        resolver::ImportKind::Package(package_resolution) => {
            graph.add_package_record(&package_resolution);
            let source = fs::read_to_string(&package_resolution.entry).map_err(|error| {
                Diagnostic::new(
                    "SLOPPYC_E_INPUT",
                    format!("failed to read package module: {error}"),
                )
                .with_path(&package_resolution.entry)
            })?;
            let resolved_id = program_module_id(
                graph,
                &package_resolution.entry,
                Some(&package_resolution.name),
            );
            graph.add_dependency_import(&from_id, specifier, &resolved_id, "package");
            extract_program_module_with_context(
                &package_resolution.entry,
                &source,
                graph,
                visiting,
                modules,
                Some(package_resolution.name),
                Some(from_id),
            )
        }
        resolver::ImportKind::NativeAddonUnsupported(package_resolution) => {
            graph.add_package_record(&package_resolution);
            Err(Diagnostic::new(
                "SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED",
                format!(
                    "Package \"{}\" requires a native Node addon. Sloppy does not support Node native addons yet.",
                    package_resolution.name
                ),
            )
            .with_path(path)
            .with_span(import.source.span)
            .with_hint("Use a pure-JavaScript package entry or remove the native addon dependency."))
        }
        resolver::ImportKind::PackageExportUnsupported(failure) => Err(
            package_export_unsupported_diagnostic(path, import.source.span, &failure),
        ),
        resolver::ImportKind::UnsupportedBare(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_PACKAGE_NOT_FOUND",
            format!(
                "Package \"{specifier}\" was not found from {}.",
                source_map_source_name(path)
            ),
        )
        .with_path(path)
        .with_span(import.source.span)
        .with_hint(format!(
            "Install it with your package manager, for example:\n  npm install {specifier}"
        ))),
        resolver::ImportKind::UnresolvedRelative(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_MISSING_RELATIVE_IMPORT",
            format!("relative import \"{specifier}\" could not be resolved"),
        )
        .with_path(path)
        .with_span(import.source.span)
        .with_hint("Use a relative .js/.mjs/.cjs/.ts/.tsx/.json module inside the source root.")),
        resolver::ImportKind::Remote(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT",
            format!("remote import \"{specifier}\" is outside the sealed Sloppy artifact graph"),
        )
        .with_path(path)
        .with_span(import.source.span)
        .with_hint("Use installed packages, Sloppy stdlib imports, or relative modules.")),
    }
}

fn analyze_commonjs_requires(
    path: &Path,
    source: &str,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    let source_type = SourceType::from_path(path).unwrap_or_else(|_| SourceType::mjs());
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, source_type).parse();
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_PARSE",
            format!("failed to parse CommonJS module: {error}"),
        )
        .with_path(path));
    }
    for statement in &parsed.program.body {
        analyze_commonjs_statement_requires(
            path,
            statement,
            graph,
            visiting,
            modules,
            package.clone(),
        )?;
    }
    Ok(())
}

fn analyze_commonjs_statement_requires(
    path: &Path,
    statement: &Statement<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    match statement {
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    analyze_commonjs_expression_requires(
                        path,
                        init,
                        graph,
                        visiting,
                        modules,
                        package.clone(),
                    )?;
                }
            }
        }
        Statement::ExpressionStatement(statement) => analyze_commonjs_expression_requires(
            path,
            &statement.expression,
            graph,
            visiting,
            modules,
            package,
        )?,
        Statement::BlockStatement(block) => {
            for statement in &block.body {
                analyze_commonjs_statement_requires(
                    path,
                    statement,
                    graph,
                    visiting,
                    modules,
                    package.clone(),
                )?;
            }
        }
        Statement::IfStatement(statement) => {
            analyze_commonjs_expression_requires(
                path,
                &statement.test,
                graph,
                visiting,
                modules,
                package.clone(),
            )?;
            analyze_commonjs_statement_requires(
                path,
                &statement.consequent,
                graph,
                visiting,
                modules,
                package.clone(),
            )?;
            if let Some(alternate) = &statement.alternate {
                analyze_commonjs_statement_requires(
                    path, alternate, graph, visiting, modules, package,
                )?;
            }
        }
        _ => {}
    }
    Ok(())
}

fn analyze_commonjs_expression_requires(
    path: &Path,
    expression: &Expression<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    match expression {
        Expression::CallExpression(call) => {
            if let Some(specifier) = call_static_require_specifier(call) {
                resolve_program_dependency(
                    ProgramDependencyRequest {
                        path,
                        specifier,
                        package: package.clone(),
                        import_mode: false,
                        span: call.span,
                    },
                    graph,
                    visiting,
                    modules,
                )?;
            }
            for argument in &call.arguments {
                analyze_commonjs_argument_requires(
                    path,
                    argument,
                    graph,
                    visiting,
                    modules,
                    package.clone(),
                )?;
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => analyze_commonjs_expression_requires(
            path,
            &parenthesized.expression,
            graph,
            visiting,
            modules,
            package,
        )?,
        Expression::ArrayExpression(array) => {
            for element in &array.elements {
                if let Some(expression) = element.as_expression() {
                    analyze_commonjs_expression_requires(
                        path,
                        expression,
                        graph,
                        visiting,
                        modules,
                        package.clone(),
                    )?;
                }
            }
        }
        Expression::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    analyze_commonjs_expression_requires(
                        path,
                        &property.value,
                        graph,
                        visiting,
                        modules,
                        package.clone(),
                    )?;
                }
            }
        }
        Expression::AssignmentExpression(assignment) => analyze_commonjs_expression_requires(
            path,
            &assignment.right,
            graph,
            visiting,
            modules,
            package,
        )?,
        _ => {}
    }
    Ok(())
}

fn analyze_commonjs_argument_requires(
    path: &Path,
    argument: &Argument<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    match argument {
        Argument::CallExpression(call) => {
            if let Some(specifier) = call_static_require_specifier(call) {
                resolve_program_dependency(
                    ProgramDependencyRequest {
                        path,
                        specifier,
                        package: package.clone(),
                        import_mode: false,
                        span: call.span,
                    },
                    graph,
                    visiting,
                    modules,
                )?;
            }
            for nested in &call.arguments {
                analyze_commonjs_argument_requires(
                    path,
                    nested,
                    graph,
                    visiting,
                    modules,
                    package.clone(),
                )?;
            }
        }
        Argument::ParenthesizedExpression(parenthesized) => analyze_commonjs_expression_requires(
            path,
            &parenthesized.expression,
            graph,
            visiting,
            modules,
            package,
        )?,
        _ => {}
    }
    Ok(())
}

fn call_static_require_specifier<'a>(call: &'a CallExpression<'a>) -> Option<&'a str> {
    let Expression::Identifier(callee) = &call.callee else {
        return None;
    };
    if callee.name.as_str() != "require" || call.arguments.len() != 1 {
        return None;
    }
    call.arguments.first().and_then(string_argument)
}

fn ensure_node_compat_module(
    backing: &str,
    graph: &mut ModuleGraph,
    modules: &mut Vec<ProgramModule>,
) {
    if backing == "sloppy/node/fs" {
        ensure_node_compat_module("sloppy/node/fs/promises", graph, modules);
    }
    if backing == "sloppy/node/stream" {
        ensure_node_compat_module("sloppy/node/events", graph, modules);
    }
    if backing == "sloppy/node/diagnostics_channel" {
        ensure_node_compat_module("sloppy/node/events", graph, modules);
    }
    if backing == "sloppy/node/stream/promises" {
        ensure_node_compat_module("sloppy/node/stream", graph, modules);
    }
    if backing == "sloppy/node/assert/strict" {
        ensure_node_compat_module("sloppy/node/assert", graph, modules);
    }
    if modules.iter().any(|module| module.id == backing) {
        return;
    }
    let Some(source) = node_compat_module_source(backing) else {
        return;
    };
    graph.dependency_graph.ensure_defaults();
    graph.add_dependency_module(
        backing.to_string(),
        backing.to_string(),
        ModuleFormat::CommonJs,
        None,
        Some("node-compat-registry".to_string()),
    );
    modules.push(ProgramModule {
        id: backing.to_string(),
        source_name: backing.to_string(),
        source: source.to_string(),
        emitted_source: source.to_string(),
        format: ModuleFormat::CommonJs,
    });
}

const SLOPPY_SQLITE_PROVIDER_PROGRAM_MODULE: &str = r#"function validateSqliteProviderName(name){if(typeof name!=="string"||name.length===0){throw new TypeError("Sloppy sqlite provider name must be a non-empty string.");}if(name.trim()!==name||!/^[A-Za-z0-9_.-]+$/u.test(name)){throw new TypeError("Sloppy sqlite provider name must contain only letters, digits, dots, underscores, or hyphens.");}}function validateSqliteProviderOptions(options){if(options===undefined){return Object.freeze({});}if(options===null||typeof options!=="object"||Array.isArray(options)){throw new TypeError("Sloppy sqlite provider options must be a plain object.");}if(Object.prototype.hasOwnProperty.call(options,"database")&&typeof options.database!=="string"){throw new TypeError("Sloppy sqlite provider database option must be a string.");}return Object.freeze({...options});}function sqlite(name,options){validateSqliteProviderName(name);return Object.freeze({__sloppyProvider:true,kind:"sqlite",name,token:name.includes(".")?name:`data.${name}`,options:validateSqliteProviderOptions(options)});}module.exports={sqlite,Sqlite:sqlite,default:null};module.exports.default=module.exports;"#;

fn ensure_sloppy_provider_module(
    backing: &str,
    graph: &mut ModuleGraph,
    modules: &mut Vec<ProgramModule>,
) {
    if modules.iter().any(|module| module.id == backing) {
        return;
    }
    let source = match backing {
        "sloppy/providers/sqlite" => SLOPPY_SQLITE_PROVIDER_PROGRAM_MODULE,
        _ => return,
    };
    graph.dependency_graph.ensure_defaults();
    graph.add_dependency_module(
        backing.to_string(),
        backing.to_string(),
        ModuleFormat::CommonJs,
        None,
        Some("sloppy-stdlib-provider".to_string()),
    );
    modules.push(ProgramModule {
        id: backing.to_string(),
        source_name: backing.to_string(),
        source: source.to_string(),
        emitted_source: source.to_string(),
        format: ModuleFormat::CommonJs,
    });
}

fn node_compat_module_source(backing: &str) -> Option<&'static str> {
    match backing {
        "sloppy/node/path" => Some(NODE_PATH_SHIM),
        "sloppy/node/events" => Some(NODE_EVENTS_SHIM),
        "sloppy/node/url" => Some(NODE_URL_SHIM),
        "sloppy/node/querystring" => Some(NODE_QUERYSTRING_SHIM),
        "sloppy/node/buffer" => Some(NODE_BUFFER_SHIM),
        "sloppy/node/console" => Some(NODE_CONSOLE_SHIM),
        "sloppy/node/constants" => Some(NODE_CONSTANTS_SHIM),
        "sloppy/node/util" => Some(NODE_UTIL_SHIM),
        "sloppy/node/timers" => Some(NODE_TIMERS_SHIM),
        "sloppy/node/fs" => Some(NODE_FS_SHIM),
        "sloppy/node/fs/promises" => Some(NODE_FS_PROMISES_SHIM),
        "sloppy/node/os" => Some(NODE_OS_SHIM),
        "sloppy/node/process" => Some(NODE_PROCESS_SHIM),
        "sloppy/node/crypto" => Some(NODE_CRYPTO_SHIM),
        "sloppy/node/diagnostics_channel" => Some(NODE_DIAGNOSTICS_CHANNEL_SHIM),
        "sloppy/node/http" => Some(NODE_HTTP_SHIM),
        "sloppy/node/https" => Some(NODE_HTTPS_SHIM),
        "sloppy/node/module" => Some(NODE_MODULE_SHIM),
        "sloppy/node/perf_hooks" => Some(NODE_PERF_HOOKS_SHIM),
        "sloppy/node/assert" => Some(NODE_ASSERT_SHIM),
        "sloppy/node/assert/strict" => Some(NODE_ASSERT_STRICT_SHIM),
        "sloppy/node/stream" => Some(NODE_STREAM_SHIM),
        "sloppy/node/stream/promises" => Some(NODE_STREAM_PROMISES_SHIM),
        "sloppy/node/string_decoder" => Some(NODE_STRING_DECODER_SHIM),
        "sloppy/node/tty" => Some(NODE_TTY_SHIM),
        "sloppy/node/zlib" => Some(NODE_ZLIB_SHIM),
        _ => None,
    }
}

const NODE_PATH_SHIM: &str = r#"function parts(values){return values.filter((value)=>value!==undefined&&value!==null).map(String).filter(Boolean);}
function normalize(path){const input=String(path||"");const absolute=input.startsWith("/");const out=[];for(const part of input.replace(/\\/g,"/").split("/")){if(!part||part===".")continue;if(part===".."){if(out.length&&out[out.length-1]!==".."){out.pop();}else if(!absolute){out.push("..");}}else{out.push(part);}}return `${absolute?"/":""}${out.join("/")}`||".";}
function join(){return normalize(parts(Array.prototype.slice.call(arguments)).join("/"));}
function resolve(){let output="";for(const part of parts(Array.prototype.slice.call(arguments))){output=part.startsWith("/")?part:`${output}/${part}`;}return normalize(output||".");}
function basename(path,ext){let base=String(path).replace(/\\/g,"/").split("/").filter(Boolean).pop()||"";if(ext&&base.endsWith(ext)){base=base.slice(0,-String(ext).length);}return base;}
function dirname(path){const text=normalize(path);const index=text.lastIndexOf("/");if(index<=0)return text.startsWith("/")?"/":".";return text.slice(0,index);}
function extname(path){const base=basename(path);const index=base.lastIndexOf(".");return index>0?base.slice(index):"";}
function isAbsolute(path){return String(path).startsWith("/")||/^[A-Za-z]:[\\/]/.test(String(path));}
const posix=Object.freeze({join,resolve,basename,dirname,extname,normalize,isAbsolute,sep:"/",delimiter:":"});
const win32=Object.freeze({...posix,sep:"\\",delimiter:";"});
module.exports={join,resolve,basename,dirname,extname,normalize,isAbsolute,posix,win32,sep:"/",delimiter:":",default:null};module.exports.default=module.exports;"#;

const NODE_EVENTS_SHIM: &str = r#"const onceOriginal=Symbol("onceOriginal");class EventEmitter{constructor(){this._events=Object.create(null);}on(name,fn){if(typeof fn!=="function")throw new TypeError("listener must be a function");(this._events[name]??=[]).push(fn);return this;}addListener(name,fn){return this.on(name,fn);}off(name,fn){return this.removeListener(name,fn);}removeListener(name,fn){const list=this._events[name];if(!list)return this;this._events[name]=list.filter((item)=>item!==fn&&item[onceOriginal]!==fn);return this;}once(name,fn){if(typeof fn!=="function")throw new TypeError("listener must be a function");const wrapped=(...args)=>{this.removeListener(name,wrapped);return fn.apply(this,args);};wrapped[onceOriginal]=fn;return this.on(name,wrapped);}emit(name,...args){const list=(this._events[name]||[]).slice();for(const fn of list){fn.apply(this,args);}return list.length>0;}listenerCount(name){return (this._events[name]||[]).length;}removeAllListeners(name){if(name===undefined){this._events=Object.create(null);}else{delete this._events[name];}return this;}}
module.exports={EventEmitter,default:EventEmitter};"#;

const NODE_URL_SHIM: &str = r#"module.exports={URL:globalThis.URL,URLSearchParams:globalThis.URLSearchParams,pathToFileURL(path){return new URL(`file://${String(path).replace(/\\/g,"/")}`);},fileURLToPath(value){return new URL(value).pathname;},default:null};module.exports.default=module.exports;"#;

const NODE_QUERYSTRING_SHIM: &str = r#"function parse(text){const out=Object.create(null);for(const part of String(text||"").split("&")){if(!part)continue;const [k,v=""]=part.split("=");out[decodeURIComponent(k.replace(/\+/g," "))]=decodeURIComponent(v.replace(/\+/g," "));}return out;}function stringify(value){return Object.entries(value||{}).map(([k,v])=>`${encodeURIComponent(k)}=${encodeURIComponent(String(v))}`).join("&");}module.exports={parse,stringify,escape:encodeURIComponent,unescape:decodeURIComponent,default:null};module.exports.default=module.exports;"#;

const NODE_BUFFER_SHIM: &str = r#"const rt=globalThis.__sloppy_runtime||{};function enc(e="utf8"){const n=String(e).toLowerCase();if(n==="utf-8")return"utf8";if(["utf8","hex","base64","base64url"].includes(n))return n;throw new TypeError(`Sloppy Buffer encoding is not supported: ${e}`);}function fromString(value,e){switch(enc(e)){case"utf8":return rt.Text.utf8.encode(value);case"hex":return rt.Hex.decode(value);case"base64":return rt.Base64.decode(value);case"base64url":return rt.Base64Url.decode(value,{padding:"optional"});}}function toStringValue(value,e){switch(enc(e)){case"utf8":return rt.Text.utf8.decode(value);case"hex":return rt.Hex.encode(value);case"base64":return rt.Base64.encode(value);case"base64url":return rt.Base64Url.encode(value);}}class Buffer extends Uint8Array{static from(value,encoding="utf8"){if(typeof value==="string")return new Buffer(fromString(value,encoding));if(value instanceof ArrayBuffer||ArrayBuffer.isView(value)||Array.isArray(value))return new Buffer(value);throw new TypeError("Buffer.from only supports string, ArrayBuffer, Uint8Array, or byte arrays in Sloppy.");}static alloc(size,fill=0){const buffer=new Buffer(size);buffer.fill(fill);return buffer;}static allocUnsafe(size){return Buffer.alloc(size);}static isBuffer(value){return value instanceof Buffer;}static isEncoding(value){if(value===undefined||value===null)return false;try{enc(value);return true;}catch{return false;}}static byteLength(value,encoding="utf8"){return Buffer.from(value,encoding).byteLength;}static compare(left,right){left=Buffer.from(left);right=Buffer.from(right);const length=Math.min(left.byteLength,right.byteLength);for(let i=0;i<length;i+=1){if(left[i]!==right[i])return left[i]<right[i]?-1:1;}return left.byteLength===right.byteLength?0:left.byteLength<right.byteLength?-1:1;}static concat(list,totalLength=undefined){if(!Array.isArray(list))throw new TypeError("Sloppy Buffer.concat expects an array.");const buffers=list.map((entry)=>Buffer.from(entry));const length=totalLength===undefined?buffers.reduce((sum,entry)=>sum+entry.byteLength,0):Number(totalLength);if(!Number.isInteger(length)||length<0)throw new TypeError("Sloppy Buffer.concat totalLength must be a non-negative integer.");const output=new Buffer(length);let offset=0;for(const buffer of buffers){output.set(buffer.subarray(0,Math.max(0,length-offset)),offset);offset+=buffer.byteLength;if(offset>=length)break;}return output;}equals(other){return Buffer.compare(this,other)===0;}compare(other){return Buffer.compare(this,other);}subarray(start=0,end=undefined){const view=super.subarray(start,end);Object.setPrototypeOf(view,Buffer.prototype);return view;}slice(start=0,end=undefined){return this.subarray(start,end);}toString(encoding="utf8"){return toStringValue(this,encoding);}write(value,offset=0,length=undefined,encoding="utf8"){if(typeof offset==="string"){encoding=offset;offset=0;length=undefined;}else if(typeof length==="string"){encoding=length;length=undefined;}const bytes=Buffer.from(String(value),encoding);const start=Number(offset);const count=Math.min(length===undefined?bytes.byteLength:Number(length),this.byteLength-start);if(!Number.isInteger(start)||start<0||!Number.isInteger(count)||count<0)throw new RangeError("Buffer.write offset and length are out of range.");this.set(bytes.subarray(0,count),start);return count;}readUInt8(offset=0){return this._read(offset,1,true);}readUInt16LE(offset=0){return this._read(offset,2,true);}readUInt16BE(offset=0){return this._read(offset,2,false);}readUInt32LE(offset=0){return this._read(offset,4,true);}readUInt32BE(offset=0){return this._read(offset,4,false);}writeUInt8(value,offset=0){return this._write(value,offset,1,true);}writeUInt16LE(value,offset=0){return this._write(value,offset,2,true);}writeUInt16BE(value,offset=0){return this._write(value,offset,2,false);}writeUInt32LE(value,offset=0){return this._write(value,offset,4,true);}writeUInt32BE(value,offset=0){return this._write(value,offset,4,false);}_bounds(offset,width){offset=Number(offset);if(!Number.isInteger(offset)||offset<0||offset+width>this.byteLength)throw new RangeError("Buffer offset is outside bounds.");return offset;}_read(offset,width,little){offset=this._bounds(offset,width);let value=0;for(let i=0;i<width;i+=1){value+=this[offset+(little?i:width-1-i)]*2**(8*i);}return value;}_write(value,offset,width,little){offset=this._bounds(offset,width);value=Number(value);const max=2**(8*width);if(!Number.isInteger(value)||value<0||value>=max)throw new RangeError("Buffer unsigned integer value is outside range.");for(let i=0;i<width;i+=1){this[offset+(little?i:width-1-i)]=Math.floor(value/2**(8*i))&255;}return offset+width;}}module.exports={Buffer,default:Buffer};"#;

const NODE_UTIL_SHIM: &str = r#"function inspect(value){if(typeof value==="string")return value;if(typeof value==="function")return value.name?`[Function: ${value.name}]`:"[Function]";try{return JSON.stringify(value,null,2);}catch(_){return String(value);}}function format(first="",...rest){if(typeof first!=="string")return[first,...rest].map(inspect).join(" ");let index=0;const text=first.replace(/%[sdjifoO%]/g,(token)=>{if(token==="%%")return"%";if(index>=rest.length)return token;const value=rest[index++];if(token==="%s")return String(value);if(token==="%d")return String(Number(value));if(token==="%i")return String(Number.parseInt(value,10));if(token==="%f")return String(Number.parseFloat(value));if(token==="%j"){try{return JSON.stringify(value);}catch{return"[Circular]";}}return inspect(value);});return[text,...rest.slice(index).map(inspect)].join(" ");}function promisify(fn){if(typeof fn!=="function")throw new TypeError("promisify expects a function");return function(){const args=Array.prototype.slice.call(arguments);return new Promise((resolve,reject)=>fn.call(this,...args,(error,value)=>error?reject(error):resolve(value)));};}function callbackify(fn){if(typeof fn!=="function")throw new TypeError("callbackify expects a function");return function(){const args=Array.prototype.slice.call(arguments);const cb=args.pop();Promise.resolve().then(()=>fn.apply(this,args)).then((value)=>cb(null,value),(error)=>cb(error));};}function inherits(ctor,superCtor){if(typeof ctor!=="function"||typeof superCtor!=="function")throw new TypeError("inherits expects constructor functions.");Object.setPrototypeOf(ctor.prototype,superCtor.prototype);Object.defineProperty(ctor.prototype,"constructor",{configurable:true,enumerable:false,value:ctor,writable:true});}module.exports={callbackify,format,inherits,inspect,promisify,types:{isArrayBufferView:ArrayBuffer.isView,isDate:(value)=>value instanceof Date,isRegExp:(value)=>value instanceof RegExp,isUint8Array(value){return value instanceof Uint8Array;}},default:null};module.exports.default=module.exports;"#;

const NODE_TIMERS_SHIM: &str = r#"function unsupported(name){return function(){throw new Error(`node:timers.${name} is not implemented by Sloppy's node:timers compatibility shim.`);};}module.exports={setTimeout:globalThis.setTimeout||unsupported("setTimeout"),clearTimeout:globalThis.clearTimeout||unsupported("clearTimeout"),setInterval:globalThis.setInterval||unsupported("setInterval"),clearInterval:globalThis.clearInterval||unsupported("clearInterval"),setImmediate:unsupported("setImmediate"),clearImmediate:unsupported("clearImmediate"),default:null};module.exports.default=module.exports;"#;

const NODE_FS_SHIM: &str = r#"const promises=require("sloppy/node/fs/promises");function unsupported(name){return function(){throw new Error(`node:fs.${name} is not implemented by Sloppy's node:fs compatibility shim. Use sloppy/fs when available, or avoid this package path.`);};}function callbackify(method,name){return function(){const args=Array.prototype.slice.call(arguments);const callback=args.pop();if(typeof callback!=="function")throw new TypeError(`node:fs.${name} requires a callback.`);Promise.resolve().then(()=>method(...args)).then((value)=>callback(null,value),(error)=>callback(error));};}const api={access:callbackify(promises.access,"access"),appendFile:callbackify(promises.appendFile,"appendFile"),copyFile:callbackify(promises.copyFile,"copyFile"),existsSync:unsupported("existsSync"),lstat:callbackify(promises.lstat,"lstat"),mkdir:callbackify(promises.mkdir,"mkdir"),mkdtemp:callbackify(promises.mkdtemp,"mkdtemp"),promises,readdir:callbackify(promises.readdir,"readdir"),readFile:callbackify(promises.readFile,"readFile"),readlink:callbackify(promises.readlink,"readlink"),realpath:callbackify(promises.realpath,"realpath"),rename:callbackify(promises.rename,"rename"),rm:callbackify(promises.rm,"rm"),stat:callbackify(promises.stat,"stat"),symlink:callbackify(promises.symlink,"symlink"),unlink:callbackify(promises.unlink,"unlink"),watch:unsupported("watch"),writeFile:callbackify(promises.writeFile,"writeFile"),default:null};module.exports=api;module.exports.default=api;"#;

const NODE_FS_PROMISES_SHIM: &str = r#"const rt=globalThis.__sloppy_runtime||{};function encoding(options,op){const e=typeof options==="string"?options:options?.encoding;if(e===undefined||e===null)return undefined;const n=String(e).toLowerCase();if(n==="utf8"||n==="utf-8")return"utf8";throw new TypeError(`SLOPPY_E_NODE_FS_UNSUPPORTED_ENCODING: ${op} only supports utf8 text encoding.`);}function recursiveOption(options,op){const value=options?.recursive;if(value===undefined)return false;if(typeof value!=="boolean")throw new TypeError(`${op} recursive option must be a boolean.`);return value;}function sloppyPath(path){if(typeof path!=="string")return path;const value=path.replace(/\\/g,"/");if(value.startsWith("/")||/^[A-Za-z]:\//.test(value)||value.startsWith("./")||value.startsWith("../")||value.includes("://"))return value;return `./${value}`;}function stats(stat){return Object.freeze({...stat,isFile(){return stat.kind==="file"||(stat.exists===true&&stat.kind!=="directory");},isDirectory(){return stat.kind==="directory";},isSymbolicLink(){return stat.kind==="symlink"||stat.symlink===true;}});}async function unsupported(name){throw new Error(`SLOPPY_E_NODE_FS_UNSUPPORTED: node:fs.promises.${name} is not implemented by Sloppy's Node compatibility shim.`);}async function readFile(path,options=undefined){const e=encoding(options,"node:fs.readFile");const target=sloppyPath(path);return e==="utf8"?rt.File.readText(target):rt.File.readBytes(target);}async function writeFile(path,data,options=undefined){encoding(options,"node:fs.writeFile");const target=sloppyPath(path);if(typeof data==="string")return rt.File.writeText(target,data);return rt.File.writeBytes(target,data);}async function appendFile(path,data,options=undefined){encoding(options,"node:fs.appendFile");const target=sloppyPath(path);if(typeof data==="string")return rt.File.appendText(target,data);return rt.File.appendBytes(target,data);}async function access(path){const stat=await rt.File.stat(sloppyPath(path));if(!stat.exists)throw new Error("SLOPPY_E_NODE_FS_ACCESS: path is not accessible.");}async function readdir(path){return(await rt.Directory.list(sloppyPath(path))).map((entry)=>entry.name);}async function rm(path,options=undefined){const target=sloppyPath(path);const stat=await rt.File.stat(target);if(stat.kind==="directory")return rt.Directory.delete(target,{recursive:recursiveOption(options,"node:fs.rm")});return rt.File.delete(target);}async function unlink(path){return rt.File.delete(sloppyPath(path));}async function copyFile(fromPath,toPath){return rt.File.copy(sloppyPath(fromPath),sloppyPath(toPath));}async function rename(fromPath,toPath){return rt.File.move(sloppyPath(fromPath),sloppyPath(toPath));}async function readlink(path){return rt.File.readLink(sloppyPath(path));}async function symlink(targetPath,linkPath,type=undefined){return rt.File.createSymlink(sloppyPath(targetPath),sloppyPath(linkPath),{directory:type==="dir"||type==="junction"});}const api={access,appendFile,copyFile,lstat:()=>unsupported("lstat"),mkdir:(path,options)=>rt.Directory.create(sloppyPath(path),{recursive:recursiveOption(options,"node:fs.mkdir")}),mkdtemp:()=>unsupported("mkdtemp"),readdir,readFile,readlink,realpath:()=>unsupported("realpath"),rename,rm,stat:async(path)=>stats(await rt.File.stat(sloppyPath(path))),symlink,unlink,writeFile};module.exports={...api,default:api};"#;

const NODE_OS_SHIM: &str = r#"function runtime(){return globalThis.__sloppy_runtime||{};}function platform(){return runtime().System?.platform||"sloppy";}function tmpdir(){return ".";}function homedir(){return ".";}module.exports={platform,tmpdir,homedir,EOL:"\n",default:null};module.exports.default=module.exports;"#;

const NODE_PROCESS_SHIM: &str = r#"const rt=globalThis.__sloppy_runtime||{};function monotonicMs(){const bridge=globalThis.__sloppy?.time;if(bridge&&typeof bridge.monotonicMs==="function")return bridge.monotonicMs();if(globalThis.performance&&typeof globalThis.performance.now==="function")return globalThis.performance.now();return Date.now();}const startedAt=monotonicMs();const events=Object.create(null);const env=new Proxy(Object.create(null),{get(target,key){if(typeof key!=="string")return undefined;return Object.prototype.hasOwnProperty.call(target,key)?target[key]:rt.Environment?.get(key);},has(target,key){return typeof key==="string"&&(Object.prototype.hasOwnProperty.call(target,key)||rt.Environment?.has?.(key)===true);},set(target,key,value){if(typeof key!=="string")return false;target[key]=String(value);return true;},ownKeys(target){return[...new Set([...Object.keys(target),...(rt.Environment?.list?.()??[])])];},getOwnPropertyDescriptor(target,key){const value=typeof key==="string"?(Object.prototype.hasOwnProperty.call(target,key)?target[key]:rt.Environment?.get(key)):undefined;return value===undefined?undefined:{enumerable:true,configurable:true,value};}});const sloppyVersion=String(rt.System?.version??"0.1.0");const version=sloppyVersion.startsWith("sloppy-")?sloppyVersion:`sloppy-${sloppyVersion}`;let exitCode=undefined;function on(name,fn){if(typeof fn!=="function")throw new TypeError("process listener must be a function");(events[name]??=[]).push(fn);return process;}function off(name,fn){events[name]=(events[name]||[]).filter((item)=>item!==fn);return process;}const addListener=on;const removeListener=off;function emit(name,...args){const list=(events[name]||[]).slice();for(const fn of list)fn(...args);return list.length>0;}function once(name,fn){const wrapped=(...args)=>{off(name,wrapped);fn(...args);};return on(name,wrapped);}function stream(name){return Object.freeze({isTTY:false,write(){throw new Error(`SLOPPY_E_PROCESS_STREAM_WRITE_UNSUPPORTED: process.${name}.write is not available in Sloppy's Node compatibility shim.`);}});}function memoryUsage(){return Object.freeze({rss:0,heapTotal:0,heapUsed:0,external:0,arrayBuffers:0});}function uptime(){return Math.max(0,(monotonicMs()-startedAt)/1000);}function hrtimeNanos(){return BigInt(Math.max(0,Math.floor(monotonicMs()*1000000)));}function hrtime(previous=undefined){const now=hrtimeNanos();const sec=Number(now/1000000000n);const ns=Number(now%1000000000n);if(previous===undefined)return[sec,ns];const prev=BigInt(previous[0])*1000000000n+BigInt(previous[1]);const diff=now-prev;return[Number(diff/1000000000n),Number(diff%1000000000n)];}hrtime.bigint=hrtimeNanos;const process={env,get argv(){return Array.isArray(globalThis.__sloppy_program_args)?globalThis.__sloppy_program_args.slice():[];},browser:false,cwd(){return globalThis.__sloppy_program_context?.cwd??".";},get platform(){return rt.System?.platform??"sloppy";},get arch(){return rt.System?.arch??"unknown";},version,versions:Object.freeze({sloppy:version.replace(/^sloppy-/,"")}),release:Object.freeze({name:"sloppy"}),stdin:Object.freeze({isTTY:false}),stdout:stream("stdout"),stderr:stream("stderr"),memoryUsage,uptime,hrtime,on,off,addListener,removeListener,emit,once,get exitCode(){return exitCode;},set exitCode(value){exitCode=value;},nextTick(fn,...args){Promise.resolve().then(()=>fn(...args));},exit(code=0){throw new Error(`SLOPPY_E_PROCESS_EXIT_UNSUPPORTED: process.exit(${code}) is not supported.`);}};module.exports=process;module.exports.default=process;"#;

const NODE_CRYPTO_SHIM: &str = r#"const rt=globalThis.__sloppy_runtime||{};function alg(value,op){const normalized=String(value).toLowerCase().replace(/-/g,"");if(["sha256","sha384","sha512"].includes(normalized))return normalized;throw new TypeError(`SLOPPY_E_NODE_CRYPTO_UNSUPPORTED: node:crypto.${op} only supports sha256, sha384, and sha512.`);}function randomBytes(size){return rt.Random.bytes(size);}function randomUUID(){return rt.Random.uuid();}function createHash(algorithm){return rt.Hash.create(alg(algorithm,"createHash"));}function bytes(value,op){if(typeof value==="string")return rt.Text.utf8.encode(value);if(value instanceof ArrayBuffer)return new Uint8Array(value).slice();if(ArrayBuffer.isView(value))return new Uint8Array(value.buffer,value.byteOffset,value.byteLength).slice();throw new TypeError(`${op} requires a string, ArrayBuffer, or typed-array bytes.`);}function secret(value){return typeof value==="string"?rt.Secret.fromUtf8(value):rt.Secret.fromBytes(bytes(value,"node:crypto.createHmac key"));}function createHmac(algorithm,key){const normalized=alg(algorithm,"createHmac");if(normalized!=="sha256")throw new TypeError("SLOPPY_E_NODE_CRYPTO_UNSUPPORTED: node:crypto.createHmac currently supports sha256.");const keySecret=secret(key);const chunks=[];return{update(value){chunks.push(bytes(value,"node:crypto.Hmac.update"));return this;},async digest(encoding=undefined){const input=new Uint8Array(chunks.reduce((total,chunk)=>total+chunk.byteLength,0));let offset=0;for(const chunk of chunks){input.set(chunk,offset);offset+=chunk.byteLength;}const out=await rt.Hmac.sha256(keySecret,input);if(encoding==="hex")return rt.Hex.encode(out);if(encoding!==undefined&&encoding!=="bytes")throw new TypeError("Sloppy node:crypto Hmac digest encoding must be bytes or hex.");return out;}};}function timingSafeEqual(left,right){if(left.byteLength!==right.byteLength)throw new RangeError("Input buffers must have the same byte length.");return rt.ConstantTime.equals(left,right);}module.exports={createHash,createHmac,randomBytes,randomUUID,timingSafeEqual,default:null};module.exports.default=module.exports;"#;

const NODE_ASSERT_SHIM: &str = r#"class AssertionError extends Error{constructor(options={}){super(options.message??"Assertion failed");this.name="AssertionError";this.actual=options.actual;this.expected=options.expected;this.operator=options.operator;this.code="ERR_ASSERTION";}}function internalFail(options){throw new AssertionError(options);}function fail(actual=undefined,expected=undefined,message=undefined,operator="!="){if(arguments.length===0)internalFail({message:"Failed",operator:"fail"});if(arguments.length===1){if(actual instanceof Error)throw actual;internalFail({message:actual===undefined?"Failed":actual,operator:"fail"});}internalFail({actual,expected,message:message??`${actual} ${operator} ${expected}`,operator});}function ok(value,message=undefined){if(!value)internalFail({actual:value,expected:true,operator:"ok",message});}function equal(actual,expected,message=undefined){if(actual!=expected)internalFail({actual,expected,operator:"==",message});}function strictEqual(actual,expected,message=undefined){if(!Object.is(actual,expected))internalFail({actual,expected,operator:"strictEqual",message});}function notStrictEqual(actual,expected,message=undefined){if(Object.is(actual,expected))internalFail({actual,expected,operator:"notStrictEqual",message});}function stable(value){if(value===null||typeof value!=="object")return JSON.stringify(value);if(Array.isArray(value))return`[${value.map(stable).join(",")}]`;return`{${Object.keys(value).sort().map((key)=>`${JSON.stringify(key)}:${stable(value[key])}`).join(",")}}`;}function deepStrictEqual(actual,expected,message=undefined){if(stable(actual)!==stable(expected))internalFail({actual,expected,operator:"deepStrictEqual",message});}const deepEqual=deepStrictEqual;function matchExpected(error,expected){if(expected===undefined)return true;if(typeof expected==="function"){try{if(error instanceof expected)return true;}catch{}try{return expected(error)===true;}catch{return false;}}if(expected instanceof RegExp)return expected.test(String(error?.message??error));if(expected!==null&&typeof expected==="object")return Object.entries(expected).every(([key,value])=>Object.is(error?.[key],value));return false;}function mismatch(error,expected,operator,message){internalFail({actual:error,expected,operator,message:message??`${operator} validation failed.`});}function isPromiseLike(value){return value!==null&&(typeof value==="object"||typeof value==="function")&&typeof value.then==="function";}function requirePromiseLike(value,operator){if(!isPromiseLike(value))throw new TypeError(`${operator} expects a Promise or a function returning a Promise.`);return value;}function throws(fn,expected=undefined,message=undefined){if(typeof fn!=="function")throw new TypeError("assert.throws expects a function.");try{fn();}catch(error){if(!matchExpected(error,expected))mismatch(error,expected,"throws",message);return error;}internalFail({actual:undefined,expected,operator:"throws",message});}function doesNotThrow(fn,expected=undefined,message=undefined){if(typeof fn!=="function")throw new TypeError("assert.doesNotThrow expects a function.");if(typeof expected==="string"&&message===undefined){message=expected;expected=undefined;}try{fn();}catch(error){if(expected===undefined||matchExpected(error,expected))mismatch(error,expected,"doesNotThrow",message);throw error;}}async function rejects(fn,expected=undefined,message=undefined){const promise=requirePromiseLike(typeof fn==="function"?fn():fn,"assert.rejects");try{await promise;}catch(error){if(!matchExpected(error,expected))mismatch(error,expected,"rejects",message);return error;}internalFail({actual:undefined,expected,operator:"rejects",message});}async function doesNotReject(fn,expected=undefined,message=undefined){if(typeof expected==="string"&&message===undefined){message=expected;expected=undefined;}const promise=requirePromiseLike(typeof fn==="function"?fn():fn,"assert.doesNotReject");try{await promise;}catch(error){if(expected===undefined||matchExpected(error,expected))mismatch(error,expected,"doesNotReject",message);throw error;}}function ifError(value){if(value!==null&&value!==undefined)internalFail({actual:value,expected:null,operator:"ifError",message:value?.message});}function assert(value,message=undefined){return ok(value,message);}Object.assign(assert,{AssertionError,deepEqual,deepStrictEqual,doesNotReject,doesNotThrow,equal,fail,ifError,notStrictEqual,ok,rejects,strictEqual,throws});module.exports=assert;module.exports.default=assert;"#;
const NODE_ASSERT_STRICT_SHIM: &str = r#"const base=require("sloppy/node/assert");function strict(value,message=undefined){return base.ok(value,message);}Object.assign(strict,base,{equal:base.strictEqual});module.exports=strict;module.exports.default=strict;"#;

const NODE_STREAM_SHIM: &str = r#"const {EventEmitter}=require("sloppy/node/events");class Stream extends EventEmitter{}class Readable extends Stream{static from(iterable){const readable=new Readable();readable._iterable=iterable;return readable;}async *[Symbol.asyncIterator](){for await(const chunk of this._iterable??[])yield chunk;}}class Writable extends Stream{constructor(options={}){super();this._write=typeof options.write==="function"?options.write:undefined;this.chunks=[];this._pendingWrites=new Set();this._finished=Promise.resolve(this);}_invokeWrite(chunk){return new Promise((resolve,reject)=>{try{if(this._write){if(this._write.length>=3)this._write(chunk,undefined,(error)=>error?reject(error):resolve());else Promise.resolve(this._write(chunk)).then(resolve,reject);}else{this.chunks.push(chunk);resolve();}}catch(error){reject(error);}});}_trackWrite(promise){const tracked=Promise.resolve(promise).finally(()=>this._pendingWrites.delete(tracked));this._pendingWrites.add(tracked);return tracked;}write(chunk,encodingOrCallback=undefined,callback=undefined){const done=typeof encodingOrCallback==="function"?encodingOrCallback:callback;this._trackWrite(this._invokeWrite(chunk)).then(()=>{if(typeof done==="function")done();this.emit("drain");},(error)=>{if(typeof done==="function")done(error);this.emit("error",error);});return true;}end(chunk=undefined,encodingOrCallback=undefined,callback=undefined){if(typeof chunk==="function"){callback=chunk;chunk=undefined;}else if(typeof encodingOrCallback==="function")callback=encodingOrCallback;if(chunk!==undefined)this.write(chunk);if(typeof callback==="function")this.once("finish",callback);this._finished=Promise.all([...this._pendingWrites]).then(()=>{this.emit("finish");return this;},(error)=>{this.emit("error",error);throw error;});return this;}get finished(){return this._finished;}}class PassThrough extends Writable{write(chunk,encodingOrCallback=undefined,callback=undefined){super.write(chunk,encodingOrCallback,callback);this.emit("data",chunk);return true;}}async function pipeline(source,destination,callback=undefined){try{for await(const chunk of source)destination.write(chunk);destination.end();await destination.finished;if(typeof callback==="function")callback();return destination;}catch(error){if(typeof callback==="function"){callback(error);return destination;}throw error;}}module.exports={PassThrough,Readable,Stream,Writable,pipeline,default:null};module.exports.default=module.exports;"#;
const NODE_STREAM_PROMISES_SHIM: &str = r#"const stream=require("sloppy/node/stream");module.exports={pipeline:stream.pipeline,default:{pipeline:stream.pipeline}};"#;

const NODE_CONSOLE_SHIM: &str = r#"module.exports=globalThis.console||{log(){},error(){},warn(){},info(){},debug(){}};function Console(){};Console.prototype.log=module.exports.log;Console.prototype.error=module.exports.error;Console.prototype.warn=module.exports.warn;Console.prototype.info=module.exports.info;Console.prototype.debug=module.exports.debug;module.exports.Console=module.exports.Console||Console;module.exports.default=module.exports;"#;
const NODE_CONSTANTS_SHIM: &str = r#"module.exports={O_RDONLY:0,O_WRONLY:1,O_RDWR:2,S_IFMT:61440,S_IFREG:32768,S_IFDIR:16384,default:null};module.exports.default=module.exports;"#;
const NODE_STRING_DECODER_SHIM: &str = r#"const rt=globalThis.__sloppy_runtime||{};class StringDecoder{constructor(encoding="utf8"){const n=String(encoding).toLowerCase();if(n!=="utf8"&&n!=="utf-8")throw new TypeError("Sloppy string_decoder only supports utf8.");this._decoder=rt.Text.utf8.decoder();}write(buffer){const bytes=buffer instanceof Uint8Array?buffer:new Uint8Array(buffer);return this._decoder.decode(bytes,{stream:true});}end(buffer=undefined){const text=buffer===undefined?"":this.write(buffer);return text+this._decoder.finish();}}module.exports={StringDecoder,default:null};module.exports.default=module.exports;"#;
const NODE_MODULE_SHIM: &str = r#"const builtinModules=Object.freeze(["assert","assert/strict","buffer","console","constants","crypto","diagnostics_channel","events","fs","fs/promises","http","https","module","os","path","perf_hooks","process","querystring","stream","stream/promises","string_decoder","timers","tty","url","util","zlib"]);function createRequire(source){if(typeof globalThis.__sloppy_program_create_require==="function")return globalThis.__sloppy_program_create_require(source);throw new Error("SLOPPY_E_MODULE_REQUIRE_UNAVAILABLE: createRequire is only available in bundled Sloppy programs.");}const Module=Object.freeze({builtinModules,createRequire});module.exports={Module,builtinModules,createRequire,default:null};module.exports.default=module.exports;"#;
const NODE_PERF_HOOKS_SHIM: &str = r#"const fallbackTimeOrigin=Date.now()-(typeof process!=="undefined"&&typeof process.uptime==="function"?process.uptime()*1000:0);const performance=globalThis.performance||Object.freeze({timeOrigin:fallbackTimeOrigin,now:()=>Date.now()-fallbackTimeOrigin});module.exports={performance,default:null};module.exports.default=module.exports;"#;
const NODE_DIAGNOSTICS_CHANNEL_SHIM: &str = r#"const {EventEmitter}=require("sloppy/node/events");const channels=new Map();class Channel extends EventEmitter{constructor(name){super();this.name=name;}publish(message){this.emit("message",message,this.name);}subscribe(listener){this.on("message",listener);}unsubscribe(listener){this.removeListener("message",listener);}hasSubscribers(){return this.listenerCount("message")>0;}}function channel(name){name=String(name);if(!channels.has(name))channels.set(name,new Channel(name));return channels.get(name);}function hasSubscribers(name){return channels.get(String(name))?.hasSubscribers()??false;}module.exports={channel,hasSubscribers,default:null};module.exports.default=module.exports;"#;
const NODE_TTY_SHIM: &str = r#"function isatty(){return false;}class ReadStream{constructor(){this.isTTY=false;}}class WriteStream{constructor(){this.isTTY=false;}}module.exports={isatty,ReadStream,WriteStream,default:null};module.exports.default=module.exports;"#;
const NODE_HTTP_SHIM: &str = r#"function unsupported(name){return function(){throw new Error(`SLOPPY_E_NODE_HTTP_UNSUPPORTED: node:http.${name} is not implemented by Sloppy's Node compatibility shim.`);};}module.exports={request:unsupported("request"),get:unsupported("get"),default:null};module.exports.default=module.exports;"#;
const NODE_HTTPS_SHIM: &str = r#"function unsupported(name){return function(){throw new Error(`SLOPPY_E_NODE_HTTPS_UNSUPPORTED: node:https.${name} is not implemented by Sloppy's Node compatibility shim.`);};}module.exports={request:unsupported("request"),get:unsupported("get"),default:null};module.exports.default=module.exports;"#;
const NODE_ZLIB_SHIM: &str = r#"const rt=globalThis.__sloppy_runtime||{};function bytes(value){if(typeof value==="string")return rt.Text.utf8.encode(value);if(value instanceof Uint8Array)return value;if(value instanceof ArrayBuffer)return new Uint8Array(value);if(ArrayBuffer.isView(value))return new Uint8Array(value.buffer,value.byteOffset,value.byteLength);throw new TypeError("Sloppy node:zlib input must be a string or bytes.");}function cb(factory,callback){if(typeof callback!=="function")throw new TypeError("Sloppy node:zlib callback API requires a callback.");factory().then((value)=>callback(null,value),(error)=>callback(error));}function gzip(input,options,callback){if(typeof options==="function"){callback=options;options=undefined;}cb(()=>rt.Compression.gzip(bytes(input),options),callback);}function gunzip(input,options,callback){if(typeof options==="function"){callback=options;options=undefined;}cb(()=>rt.Compression.gunzip(bytes(input),options),callback);}function unsupportedAsync(name,input,options,callback){if(typeof options==="function"){callback=options;options=undefined;}cb(()=>Promise.reject(new Error(`SLOPPY_E_NODE_ZLIB_UNSUPPORTED: node:zlib.${name} is not implemented by Sloppy's Node compatibility shim.`)),callback);}function deflate(input,options,callback){unsupportedAsync("deflate",input,options,callback);}function inflate(input,options,callback){unsupportedAsync("inflate",input,options,callback);}function unsupported(name){return function(){throw new Error(`SLOPPY_E_NODE_ZLIB_SYNC_UNSUPPORTED: node:zlib.${name} is not available because Sloppy compression is async.`);};}module.exports={gzip,gunzip,deflate,inflate,gzipSync:unsupported("gzipSync"),gunzipSync:unsupported("gunzipSync"),deflateSync:unsupported("deflateSync"),inflateSync:unsupported("inflateSync"),default:null};module.exports.default=module.exports;"#;

struct ProgramDependencyRequest<'a> {
    path: &'a Path,
    specifier: &'a str,
    package: Option<String>,
    import_mode: bool,
    span: Span,
}

fn package_export_unsupported_diagnostic(
    path: &Path,
    span: Span,
    failure: &resolver::PackageExportFailure,
) -> Diagnostic {
    Diagnostic::new(
        "SLOPPYC_E_PACKAGE_EXPORT_UNSUPPORTED",
        format!(
            "Package \"{}\" {} entry at \"{}\" is not supported: {}.",
            failure.subject, failure.field, failure.subpath, failure.reason
        ),
    )
    .with_path(path)
    .with_span(span)
    .with_hint(
        "Use a supported package.json exports/imports condition or fall back to a main entry.",
    )
}

fn resolve_program_dependency(
    request: ProgramDependencyRequest<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
) -> Result<Option<String>, Diagnostic> {
    let path = request.path;
    let specifier = request.specifier;
    let package = request.package;
    let span = request.span;
    let from_id = program_module_id(graph, path, package.as_deref());
    match resolver::classify_import_with_mode(path, specifier, request.import_mode) {
        resolver::ImportKind::Relative(resolved) => {
            let source = fs::read_to_string(&resolved).map_err(|error| {
                Diagnostic::new(
                    "SLOPPYC_E_INPUT",
                    format!("failed to read module: {error}"),
                )
                .with_path(&resolved)
            })?;
            let resolved_id = program_module_id(graph, &resolved, package.as_deref());
            graph.add_dependency_import(&from_id, specifier, &resolved_id, "relative");
            extract_program_module_with_context(
                &resolved,
                &source,
                graph,
                visiting,
                modules,
                package,
                Some(from_id),
            )?;
            Ok(Some(resolved_id))
        }
        resolver::ImportKind::Package(package_resolution) => {
            graph.add_package_record(&package_resolution);
            let source = fs::read_to_string(&package_resolution.entry).map_err(|error| {
                Diagnostic::new(
                    "SLOPPYC_E_INPUT",
                    format!("failed to read package module: {error}"),
                )
                .with_path(&package_resolution.entry)
            })?;
            let resolved_id =
                program_module_id(graph, &package_resolution.entry, Some(&package_resolution.name));
            graph.add_dependency_import(&from_id, specifier, &resolved_id, "package");
            extract_program_module_with_context(
                &package_resolution.entry,
                &source,
                graph,
                visiting,
                modules,
                Some(package_resolution.name),
                Some(from_id),
            )?;
            Ok(Some(resolved_id))
        }
        resolver::ImportKind::NodeBuiltin(builtin) if builtin.status != "unsupported" => {
            graph.add_node_builtin(&builtin, path);
            if let Some(backing) = builtin.backing {
                ensure_node_compat_module(backing, graph, modules);
                graph.add_dependency_import(&from_id, specifier, backing, "node-compat-shim");
                Ok(Some(backing.to_string()))
            } else {
                Ok(None)
            }
        }
        resolver::ImportKind::SqliteProvider => {
            ensure_sloppy_provider_module("sloppy/providers/sqlite", graph, modules);
            graph.add_dependency_import(
                &from_id,
                specifier,
                "sloppy/providers/sqlite",
                "sloppy-provider",
            );
            Ok(Some("sloppy/providers/sqlite".to_string()))
        }
        resolver::ImportKind::NodeBuiltin(builtin) => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN",
            format!(
                "{} is not supported by Sloppy's Node compatibility registry yet.",
                builtin.specifier
            ),
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use a Sloppy stdlib API or a package path that avoids this Node builtin.")),
        resolver::ImportKind::NativeAddonUnsupported(package_resolution) => Err(Diagnostic::new(
            "SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED",
            format!(
                "Package \"{}\" requires a native Node addon. Sloppy does not support Node native addons yet.",
                package_resolution.name
            ),
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use a pure-JavaScript package entry or remove the native addon dependency.")),
        resolver::ImportKind::PackageExportUnsupported(failure) => {
            Err(package_export_unsupported_diagnostic(path, span, &failure))
        }
        resolver::ImportKind::UnsupportedBare(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_PACKAGE_NOT_FOUND",
            format!("Package \"{specifier}\" was not found from {}.", source_map_source_name(path)),
        )
        .with_path(path)
        .with_span(span)
        .with_hint(format!(
            "Install it with your package manager, for example:\n  npm install {specifier}"
        ))),
        resolver::ImportKind::UnresolvedRelative(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_MISSING_RELATIVE_IMPORT",
            format!("relative import \"{specifier}\" could not be resolved"),
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use a relative .js/.mjs/.cjs/.ts/.tsx/.json module inside the source root.")),
        resolver::ImportKind::Remote(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT",
            format!("remote import \"{specifier}\" is outside the sealed Sloppy artifact graph"),
        )
        .with_path(path)
        .with_span(span)),
        _ => Ok(None),
    }
}

fn program_import_has_runtime_value(import: &ImportDeclaration<'_>) -> bool {
    if import.import_kind == ImportOrExportKind::Type {
        return false;
    }
    let Some(specifiers) = &import.specifiers else {
        return true;
    };
    specifiers.iter().any(|specifier| match specifier {
        ImportDeclarationSpecifier::ImportSpecifier(specifier) => {
            specifier.import_kind != ImportOrExportKind::Type
        }
        ImportDeclarationSpecifier::ImportDefaultSpecifier(_)
        | ImportDeclarationSpecifier::ImportNamespaceSpecifier(_) => true,
    })
}

fn validate_program_stdlib_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
    import_kind: &resolver::ImportKind,
) -> Result<(), Diagnostic> {
    match import_kind {
        resolver::ImportKind::SlopFilesystem => validate_module_sloppy_fs_import(path, import),
        resolver::ImportKind::SlopData => validate_module_sloppy_data_import(path, import),
        resolver::ImportKind::SlopTime => validate_module_sloppy_time_import(path, import),
        resolver::ImportKind::SlopCrypto => validate_module_sloppy_crypto_import(path, import),
        resolver::ImportKind::SlopCodec => validate_module_sloppy_codec_import(path, import),
        resolver::ImportKind::SlopCache => validate_module_sloppy_cache_import(path, import),
        resolver::ImportKind::SlopNet => validate_module_sloppy_net_import(path, import),
        resolver::ImportKind::SlopHttp => validate_module_sloppy_http_import(path, import),
        resolver::ImportKind::SlopOs => validate_module_sloppy_os_import(path, import),
        resolver::ImportKind::SlopOrm => validate_module_sloppy_orm_import(path, import),
        resolver::ImportKind::SlopWorkers => validate_module_sloppy_workers_import(path, import),
        resolver::ImportKind::SlopFfi => validate_module_sloppy_ffi_import(path, import),
        resolver::ImportKind::SqliteProvider => {
            validate_module_sloppy_sqlite_provider_import(path, import)
        }
        resolver::ImportKind::SlopStdlib => {
            validate_module_sloppy_root_import(path, import).map(|_| ())
        }
        _ => Ok(()),
    }
}

fn transpile_program_typescript<'a>(
    path: &Path,
    allocator: &'a Allocator,
    program: &mut oxc_ast::ast::Program<'a>,
) -> Result<String, Diagnostic> {
    let semantic = SemanticBuilder::new()
        .with_excess_capacity(2.0)
        .with_enum_eval(true)
        .build(program);
    if let Some(error) = semantic.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT",
            format!("unsupported Program Mode syntax: {error}"),
        )
        .with_path(path)
        .with_hint(
            "Program Mode uses the Oxc TypeScript transform and does not type-check code.",
        ));
    }
    let mut options = TransformOptions::default();
    options.env.module = Module::Preserve;
    let transform = Transformer::new(allocator, path, &options)
        .build_with_scoping(semantic.semantic.into_scoping(), program);
    if let Some(error) = transform.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT",
            format!("unsupported Program Mode transform: {error}"),
        )
        .with_path(path));
    }
    Ok(Codegen::new().build(program).code)
}

fn rewrite_program_module_exports(
    path: &Path,
    source: &str,
    graph: &ModuleGraph,
    package: Option<String>,
) -> Result<String, Diagnostic> {
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, SourceType::mjs()).parse();
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_TRANSFORM",
            format!("failed to parse transformed Program Mode JavaScript: {error}"),
        )
        .with_path(path));
    }

    let mut replacements = Vec::<ProgramReplacement>::new();
    let mut is_esm = false;
    for statement in &parsed.program.body {
        match statement {
            Statement::ImportDeclaration(import) => {
                is_esm = true;
                replacements.push(ProgramReplacement {
                    span: import.span,
                    text: program_import_replacement(path, import, graph, package.as_deref())?,
                });
            }
            Statement::ExportNamedDeclaration(export) => {
                is_esm = true;
                replacements.push(ProgramReplacement {
                    span: export.span,
                    text: program_named_export_replacement(
                        path,
                        source,
                        export,
                        graph,
                        package.as_deref(),
                    )?,
                });
            }
            Statement::ExportDefaultDeclaration(export) => {
                is_esm = true;
                replacements.push(ProgramReplacement {
                    span: export.span,
                    text: program_default_export_replacement(source, export),
                });
            }
            Statement::ExportAllDeclaration(export) => {
                is_esm = true;
                replacements.push(ProgramReplacement {
                    span: export.span,
                    text: program_export_all_replacement(path, export, graph, package.as_deref())?,
                });
            }
            _ => {}
        }
        if !matches!(statement, Statement::ExportNamedDeclaration(_)) {
            collect_dynamic_import_replacements(
                path,
                source,
                statement,
                graph,
                package.as_deref(),
                &mut replacements,
            );
        }
    }
    let output = apply_program_replacements(source, &replacements);
    if is_esm {
        Ok(format!(
            "Object.defineProperty(exports,\"__esModule\",{{value:true}});\n{output}"
        ))
    } else {
        Ok(output)
    }
}

fn collect_dynamic_import_replacements(
    path: &Path,
    source: &str,
    statement: &Statement<'_>,
    graph: &ModuleGraph,
    package: Option<&str>,
    replacements: &mut Vec<ProgramReplacement>,
) {
    match statement {
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_dynamic_import_expression_replacements(
                        path,
                        source,
                        init,
                        graph,
                        package,
                        replacements,
                    );
                }
            }
        }
        Statement::ExpressionStatement(statement) => {
            collect_dynamic_import_expression_replacements(
                path,
                source,
                &statement.expression,
                graph,
                package,
                replacements,
            )
        }
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                collect_dynamic_import_expression_replacements(
                    path,
                    source,
                    argument,
                    graph,
                    package,
                    replacements,
                );
            }
        }
        Statement::BlockStatement(block) => {
            for statement in &block.body {
                collect_dynamic_import_replacements(
                    path,
                    source,
                    statement,
                    graph,
                    package,
                    replacements,
                );
            }
        }
        Statement::FunctionDeclaration(function) => {
            if let Some(body) = &function.body {
                for statement in &body.statements {
                    collect_dynamic_import_replacements(
                        path,
                        source,
                        statement,
                        graph,
                        package,
                        replacements,
                    );
                }
            }
        }
        Statement::ExportNamedDeclaration(export) => {
            if let Some(declaration) = &export.declaration {
                collect_dynamic_import_declaration_replacements(
                    path,
                    source,
                    declaration,
                    graph,
                    package,
                    replacements,
                );
            }
        }
        _ => {}
    }
}

fn collect_dynamic_import_declaration_replacements(
    path: &Path,
    source: &str,
    declaration: &Declaration<'_>,
    graph: &ModuleGraph,
    package: Option<&str>,
    replacements: &mut Vec<ProgramReplacement>,
) {
    match declaration {
        Declaration::FunctionDeclaration(function) => {
            if let Some(body) = &function.body {
                for statement in &body.statements {
                    collect_dynamic_import_replacements(
                        path,
                        source,
                        statement,
                        graph,
                        package,
                        replacements,
                    );
                }
            }
        }
        Declaration::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_dynamic_import_expression_replacements(
                        path,
                        source,
                        init,
                        graph,
                        package,
                        replacements,
                    );
                }
            }
        }
        _ => {}
    }
}

fn collect_dynamic_import_expression_replacements(
    path: &Path,
    source: &str,
    expression: &Expression<'_>,
    graph: &ModuleGraph,
    package: Option<&str>,
    replacements: &mut Vec<ProgramReplacement>,
) {
    match expression {
        Expression::ImportExpression(import) => {
            let argument =
                source_slice(source, import.source.span()).unwrap_or_else(|| "\"\"".to_string());
            replacements.push(ProgramReplacement {
                span: import.span,
                text: format!(
                    "__sloppy_program_import_from({}, {argument})",
                    json_string(&program_module_id(graph, path, package))
                ),
            });
        }
        Expression::AwaitExpression(await_expression) => {
            collect_dynamic_import_expression_replacements(
                path,
                source,
                &await_expression.argument,
                graph,
                package,
                replacements,
            )
        }
        Expression::CallExpression(call) => {
            collect_dynamic_import_expression_replacements(
                path,
                source,
                &call.callee,
                graph,
                package,
                replacements,
            );
            for argument in &call.arguments {
                collect_dynamic_import_argument_replacements(
                    path,
                    source,
                    argument,
                    graph,
                    package,
                    replacements,
                );
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            collect_dynamic_import_expression_replacements(
                path,
                source,
                &parenthesized.expression,
                graph,
                package,
                replacements,
            )
        }
        Expression::ArrowFunctionExpression(function) => {
            for statement in &function.body.statements {
                collect_dynamic_import_replacements(
                    path,
                    source,
                    statement,
                    graph,
                    package,
                    replacements,
                );
            }
        }
        _ => {}
    }
}

fn collect_dynamic_import_argument_replacements(
    path: &Path,
    source: &str,
    argument: &Argument<'_>,
    graph: &ModuleGraph,
    package: Option<&str>,
    replacements: &mut Vec<ProgramReplacement>,
) {
    match argument {
        Argument::ImportExpression(import) => {
            let argument =
                source_slice(source, import.source.span()).unwrap_or_else(|| "\"\"".to_string());
            replacements.push(ProgramReplacement {
                span: import.span,
                text: format!(
                    "__sloppy_program_import_from({}, {argument})",
                    json_string(&program_module_id(graph, path, package))
                ),
            });
        }
        Argument::CallExpression(call) => {
            for argument in &call.arguments {
                collect_dynamic_import_argument_replacements(
                    path,
                    source,
                    argument,
                    graph,
                    package,
                    replacements,
                );
            }
        }
        Argument::ParenthesizedExpression(parenthesized) => {
            collect_dynamic_import_expression_replacements(
                path,
                source,
                &parenthesized.expression,
                graph,
                package,
                replacements,
            )
        }
        _ => {}
    }
}

struct ProgramReplacement {
    span: Span,
    text: String,
}

fn apply_program_replacements(source: &str, replacements: &[ProgramReplacement]) -> String {
    let mut output = String::with_capacity(source.len() + replacements.len() * 32);
    let mut cursor = 0usize;
    let mut sorted = replacements.iter().collect::<Vec<_>>();
    sorted.sort_by_key(|replacement| (replacement.span.start, replacement.span.end));
    for replacement in sorted {
        let start = replacement.span.start as usize;
        let end = replacement.span.end as usize;
        if start < cursor {
            continue;
        }
        output.push_str(&source[cursor..start]);
        output.push_str(&replacement.text);
        cursor = end;
    }
    output.push_str(&source[cursor..]);
    if !output.ends_with('\n') {
        output.push('\n');
    }
    output
}

fn apply_program_replacements_in_span(
    source: &str,
    span: Span,
    replacements: &[ProgramReplacement],
) -> String {
    let mut output = String::new();
    let mut cursor = span.start as usize;
    let end = span.end as usize;
    let mut sorted = replacements.iter().collect::<Vec<_>>();
    sorted.sort_by_key(|replacement| (replacement.span.start, replacement.span.end));
    for replacement in sorted {
        let start = replacement.span.start as usize;
        let replacement_end = replacement.span.end as usize;
        if start < cursor || replacement_end > end {
            continue;
        }
        output.push_str(&source[cursor..start]);
        output.push_str(&replacement.text);
        cursor = replacement_end;
    }
    output.push_str(&source[cursor..end]);
    output
}

fn transform_dynamic_web_entry(path: &Path, source: &str) -> Result<String, Diagnostic> {
    let source_type = source_type_for_path(path, ParseContext::Entry)?;
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, source_type).parse();
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_PARSE",
            format!("failed to parse dynamic web input: {error}"),
        )
        .with_path(path));
    }
    let mut program = parsed.program;
    let transformed = transpile_program_typescript(path, &allocator, &mut program)?;
    rewrite_dynamic_web_entry(path, &transformed)
}

fn rewrite_dynamic_web_entry(path: &Path, source: &str) -> Result<String, Diagnostic> {
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, SourceType::mjs()).parse();
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_TRANSFORM",
            format!("failed to parse transformed dynamic web JavaScript: {error}"),
        )
        .with_path(path));
    }

    let mut replacements = Vec::<ProgramReplacement>::new();
    for statement in &parsed.program.body {
        match statement {
            Statement::ImportDeclaration(import) => {
                replacements.push(ProgramReplacement {
                    span: import.span,
                    text: String::new(),
                });
            }
            Statement::ExportDefaultDeclaration(export) => {
                let value = source_slice(source, export.declaration.span()).unwrap_or_default();
                replacements.push(ProgramReplacement {
                    span: export.span,
                    text: format!("globalThis.__sloppy_dynamic_default_app = {value};"),
                });
            }
            _ => {}
        }
    }

    Ok(apply_program_replacements(source, &replacements))
}

fn program_import_replacement(
    path: &Path,
    import: &ImportDeclaration<'_>,
    graph: &ModuleGraph,
    package: Option<&str>,
) -> Result<String, Diagnostic> {
    if import.import_kind == ImportOrExportKind::Type {
        return Ok(String::new());
    }
    let specifier = import.source.value.as_str();
    let require_expr = match resolver::classify_import(path, specifier) {
        resolver::ImportKind::Relative(resolved) => {
            format!(
                "__sloppy_program_require({})",
                json_string(&program_module_id(graph, &resolved, package))
            )
        }
        resolver::ImportKind::SlopStdlib
        | resolver::ImportKind::SlopData
        | resolver::ImportKind::SlopTime
        | resolver::ImportKind::SlopFilesystem
        | resolver::ImportKind::SlopCrypto
        | resolver::ImportKind::SlopCodec
        | resolver::ImportKind::SlopCache
        | resolver::ImportKind::SlopNet
        | resolver::ImportKind::SlopHttp
        | resolver::ImportKind::SlopOs
        | resolver::ImportKind::SlopOrm
        | resolver::ImportKind::SlopWorkers
        | resolver::ImportKind::SlopFfi => "globalThis.__sloppy_runtime".to_string(),
        resolver::ImportKind::SqliteProvider => {
            "__sloppy_program_require(\"sloppy/providers/sqlite\")".to_string()
        }
        resolver::ImportKind::NodeBuiltin(builtin) if builtin.status != "unsupported" => {
            let backing = builtin.backing.unwrap_or("sloppy/node/unsupported");
            format!("__sloppy_program_require({})", json_string(backing))
        }
        resolver::ImportKind::Package(package) => {
            let entry_id = program_module_id(graph, &package.entry, Some(&package.name));
            format!("__sloppy_program_require({})", json_string(&entry_id))
        }
        resolver::ImportKind::NativeAddonUnsupported(package) => {
            return Err(Diagnostic::new(
                "SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED",
                format!(
                    "Package \"{}\" requires a native Node addon. Sloppy does not support Node native addons yet.",
                    package.name
                ),
            )
            .with_path(path)
            .with_span(import.source.span)
            .with_hint("Use a pure-JavaScript package entry or remove the native addon dependency."));
        }
        resolver::ImportKind::PackageExportUnsupported(failure) => {
            return Err(package_export_unsupported_diagnostic(
                path,
                import.source.span,
                &failure,
            ));
        }
        resolver::ImportKind::UnsupportedBare(specifier) => {
            return Err(Diagnostic::new(
                "SLOPPYC_E_PACKAGE_NOT_FOUND",
                format!(
                    "Package \"{specifier}\" was not found from {}.",
                    source_map_source_name(path)
                ),
            )
            .with_path(path)
            .with_span(import.source.span)
            .with_hint(format!(
                "Install it with your package manager, for example:\n  npm install {specifier}"
            )));
        }
        resolver::ImportKind::NodeBuiltin(builtin) => {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN",
                format!(
                    "{} is not supported by Sloppy's Node compatibility registry yet.",
                    builtin.specifier
                ),
            )
            .with_path(path)
            .with_span(import.source.span)
            .with_hint(
                "Use a Sloppy stdlib API or a package path that avoids this Node builtin.",
            ));
        }
        _ => {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_IMPORT",
                format!("Program Mode cannot resolve import \"{specifier}\""),
            )
            .with_path(path)
            .with_span(import.source.span)
            .with_hint(
                "Use Sloppy stdlib imports, relative modules, or installed compatible packages.",
            ));
        }
    };
    let Some(specifiers) = &import.specifiers else {
        return Ok(format!("{require_expr};"));
    };
    if specifiers.is_empty() {
        return Ok(format!("{require_expr};"));
    }
    let mut defaults = Vec::new();
    let mut namespaces = Vec::new();
    let mut named = Vec::new();
    for specifier in specifiers {
        match specifier {
            ImportDeclarationSpecifier::ImportDefaultSpecifier(specifier) => {
                defaults.push(specifier.local.name.as_str().to_string());
            }
            ImportDeclarationSpecifier::ImportNamespaceSpecifier(specifier) => {
                namespaces.push(specifier.local.name.as_str().to_string());
            }
            ImportDeclarationSpecifier::ImportSpecifier(specifier) => {
                if specifier.import_kind == ImportOrExportKind::Type {
                    continue;
                }
                let imported = module_export_name_text(&specifier.imported);
                let local = specifier.local.name.as_str();
                if imported == local {
                    named.push(imported);
                } else {
                    named.push(format!("{imported}: {local}"));
                }
            }
        }
    }
    let mut output = String::new();
    if !named.is_empty() {
        output.push_str("const { ");
        output.push_str(&named.join(", "));
        output.push_str(" } = ");
        output.push_str(&require_expr);
        output.push(';');
    }
    for default in defaults {
        if !output.is_empty() {
            output.push(' ');
        }
        output.push_str("const ");
        output.push_str(&default);
        output.push_str(" = ");
        output.push_str("(()=>{const __sloppy_default_module=");
        output.push_str(&require_expr);
        output.push_str(";return __sloppy_default_module&&__sloppy_default_module.__esModule===true&&Object.prototype.hasOwnProperty.call(__sloppy_default_module,\"default\")?__sloppy_default_module.default:__sloppy_default_module;})();");
    }
    for namespace in namespaces {
        if !output.is_empty() {
            output.push(' ');
        }
        output.push_str("const ");
        output.push_str(&namespace);
        output.push_str(" = ");
        output.push_str(&require_expr);
        output.push(';');
    }
    Ok(output)
}

fn program_named_export_replacement(
    path: &Path,
    source: &str,
    export: &ExportNamedDeclaration<'_>,
    graph: &ModuleGraph,
    package: Option<&str>,
) -> Result<String, Diagnostic> {
    if export.export_kind == ImportOrExportKind::Type {
        return Ok(String::new());
    }
    if let Some(export_source) = &export.source {
        let has_runtime_reexport = export
            .specifiers
            .iter()
            .any(|specifier| specifier.export_kind != ImportOrExportKind::Type);
        if !has_runtime_reexport {
            return Ok(String::new());
        }
        let require_expr = program_reexport_require_expr(
            path,
            export_source.value.as_str(),
            export_source.span,
            graph,
            package,
        )?;
        let module_var = format!("__sloppy_reexport_{}", export.span.start);
        let mut output = format!("const {module_var} = {require_expr};\n");
        for specifier in &export.specifiers {
            if specifier.export_kind == ImportOrExportKind::Type {
                continue;
            }
            let local = module_export_name_text(&specifier.local);
            let exported = module_export_name_text(&specifier.exported);
            output.push_str(&export_assignment(
                &exported,
                &member_access_expr(&module_var, &local),
            ));
            output.push('\n');
        }
        return Ok(output);
    }
    let mut output = String::new();
    if let Some(declaration) = &export.declaration {
        let mut dynamic_replacements = Vec::new();
        collect_dynamic_import_declaration_replacements(
            path,
            source,
            declaration,
            graph,
            package,
            &mut dynamic_replacements,
        );
        output.push_str(&apply_program_replacements_in_span(
            source,
            declaration.span(),
            &dynamic_replacements,
        ));
        for name in declaration_export_names(declaration) {
            output.push('\n');
            output.push_str(&export_assignment(&name, &name));
        }
        return Ok(output);
    }
    for specifier in &export.specifiers {
        if specifier.export_kind == ImportOrExportKind::Type {
            continue;
        }
        let local = module_export_name_text(&specifier.local);
        let exported = module_export_name_text(&specifier.exported);
        if !identifier_like(&local) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_EXPORT",
                "program mode named exports must reference local identifiers",
            )
            .with_path(path)
            .with_span(specifier.span));
        }
        output.push_str(&export_assignment(&exported, &local));
        output.push('\n');
    }
    Ok(output)
}

fn program_export_all_replacement(
    path: &Path,
    export: &ExportAllDeclaration<'_>,
    graph: &ModuleGraph,
    package: Option<&str>,
) -> Result<String, Diagnostic> {
    if export.export_kind == ImportOrExportKind::Type {
        return Ok(String::new());
    }
    let require_expr = program_reexport_require_expr(
        path,
        export.source.value.as_str(),
        export.source.span,
        graph,
        package,
    )?;
    let module_var = format!("__sloppy_reexport_{}", export.span.start);
    if let Some(exported) = &export.exported {
        return Ok(export_assignment(
            &module_export_name_text(exported),
            &require_expr,
        ));
    }
    Ok(format!(
        "const {module_var} = {require_expr};\nfor (const __sloppy_key of Object.keys({module_var})) {{ if (__sloppy_key !== \"default\" && __sloppy_key !== \"__esModule\" && !Object.prototype.hasOwnProperty.call(exports, __sloppy_key)) {{ exports[__sloppy_key] = {module_var}[__sloppy_key]; }} }}"
    ))
}

fn program_reexport_require_expr(
    path: &Path,
    specifier: &str,
    span: Span,
    graph: &ModuleGraph,
    package: Option<&str>,
) -> Result<String, Diagnostic> {
    match resolver::classify_import(path, specifier) {
        resolver::ImportKind::Relative(resolved) => Ok(format!(
            "__sloppy_program_require({})",
            json_string(&program_module_id(graph, &resolved, package))
        )),
        resolver::ImportKind::SlopStdlib
        | resolver::ImportKind::SlopData
        | resolver::ImportKind::SlopTime
        | resolver::ImportKind::SlopFilesystem
        | resolver::ImportKind::SlopCrypto
        | resolver::ImportKind::SlopCodec
        | resolver::ImportKind::SlopCache
        | resolver::ImportKind::SlopNet
        | resolver::ImportKind::SlopHttp
        | resolver::ImportKind::SlopOs
        | resolver::ImportKind::SlopOrm
        | resolver::ImportKind::SlopWorkers
        | resolver::ImportKind::SlopFfi => Ok("globalThis.__sloppy_runtime".to_string()),
        resolver::ImportKind::SqliteProvider => {
            Ok("__sloppy_program_require(\"sloppy/providers/sqlite\")".to_string())
        }
        resolver::ImportKind::NodeBuiltin(builtin) if builtin.status != "unsupported" => {
            let backing = builtin.backing.unwrap_or("sloppy/node/unsupported");
            Ok(format!("__sloppy_program_require({})", json_string(backing)))
        }
        resolver::ImportKind::Package(package) => {
            let entry_id = program_module_id(graph, &package.entry, Some(&package.name));
            Ok(format!("__sloppy_program_require({})", json_string(&entry_id)))
        }
        resolver::ImportKind::NativeAddonUnsupported(package) => Err(Diagnostic::new(
            "SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED",
            format!(
                "Package \"{}\" requires a native Node addon. Sloppy does not support Node native addons yet.",
                package.name
            ),
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use a pure-JavaScript package entry or remove the native addon dependency.")),
        resolver::ImportKind::PackageExportUnsupported(failure) => {
            Err(package_export_unsupported_diagnostic(path, span, &failure))
        }
        resolver::ImportKind::UnsupportedBare(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_PACKAGE_NOT_FOUND",
            format!(
                "Package \"{specifier}\" was not found from {}.",
                source_map_source_name(path)
            ),
        )
        .with_path(path)
        .with_span(span)
        .with_hint(format!(
            "Install it with your package manager, for example:\n  npm install {specifier}"
        ))),
        resolver::ImportKind::UnresolvedRelative(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_MISSING_RELATIVE_IMPORT",
            format!("relative import \"{specifier}\" could not be resolved"),
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use a relative .js/.mjs/.cjs/.ts/.tsx/.json module inside the source root.")),
        resolver::ImportKind::Remote(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT",
            format!("remote import \"{specifier}\" is outside the sealed Sloppy artifact graph"),
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use installed packages, Sloppy stdlib imports, or relative modules.")),
        resolver::ImportKind::NodeBuiltin(builtin) => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN",
            format!(
                "{} is not supported by Sloppy's Node compatibility registry yet.",
                builtin.specifier
            ),
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use a Sloppy stdlib API or a package path that avoids this Node builtin.")),
    }
}

fn program_default_export_replacement(
    source: &str,
    export: &ExportDefaultDeclaration<'_>,
) -> String {
    let declaration = span_source(source, export.declaration.span()).trim_end_matches(';');
    format!("exports.default = {declaration};")
}

fn declaration_export_names(declaration: &Declaration<'_>) -> Vec<String> {
    match declaration {
        Declaration::FunctionDeclaration(function) => function
            .id
            .as_ref()
            .map(|id| vec![id.name.as_str().to_string()])
            .unwrap_or_default(),
        Declaration::ClassDeclaration(class) => class
            .id
            .as_ref()
            .map(|id| vec![id.name.as_str().to_string()])
            .unwrap_or_default(),
        Declaration::VariableDeclaration(declaration) => variable_declaration_names(declaration),
        _ => Vec::new(),
    }
}

fn variable_declaration_names(declaration: &VariableDeclaration<'_>) -> Vec<String> {
    let mut names = Vec::new();
    for declarator in &declaration.declarations {
        binding_pattern_names(&declarator.id, &mut names);
    }
    names
}

fn binding_pattern_names(pattern: &BindingPattern<'_>, names: &mut Vec<String>) {
    match pattern {
        BindingPattern::BindingIdentifier(identifier) => {
            names.push(identifier.name.as_str().to_string());
        }
        BindingPattern::ObjectPattern(pattern) => {
            for property in &pattern.properties {
                binding_pattern_names(&property.value, names);
            }
            if let Some(rest) = &pattern.rest {
                binding_pattern_names(&rest.argument, names);
            }
        }
        BindingPattern::ArrayPattern(pattern) => {
            for element in pattern.elements.iter().flatten() {
                binding_pattern_names(element, names);
            }
            if let Some(rest) = &pattern.rest {
                binding_pattern_names(&rest.argument, names);
            }
        }
        BindingPattern::AssignmentPattern(pattern) => {
            binding_pattern_names(&pattern.left, names);
        }
    }
}

fn module_export_name_text(name: &ModuleExportName<'_>) -> String {
    name.name().as_str().to_string()
}

fn export_assignment(exported: &str, local: &str) -> String {
    if identifier_like(exported) {
        format!("exports.{exported} = {local};")
    } else {
        format!("exports[{}] = {local};", json_string(exported))
    }
}

fn member_access_expr(object: &str, property: &str) -> String {
    if identifier_like(property) {
        format!("{object}.{property}")
    } else {
        format!("{object}[{}]", json_string(property))
    }
}

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
    helper_sources.extend(
        state
            .helper_sources
            .iter()
            .filter(|(name, _)| {
                helper_source_is_safe_for_top_level(state.helper_effects.get(*name))
            })
            .map(|(_, source)| source.clone()),
    );
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
        uses_net_runtime: state.net_imported || graph.uses_net_runtime,
        uses_os_runtime: state.os_imported
            || graph.uses_os_runtime
            || framework_needs_os_runtime
            || !state.auth.schemes.is_empty(),
        uses_http_client_runtime: state.http_client_imported || graph.uses_http_client_runtime,
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

fn sloppy_data_import_name_supported(name: &str) -> bool {
    matches!(name, "sql" | "Migrations" | "ProviderHealth")
}

fn sloppy_orm_import_name_supported(name: &str) -> bool {
    matches!(
        name,
        "orm"
            | "table"
            | "column"
            | "relation"
            | "sql"
            | "SloppyOrmError"
            | "SloppyOrmConcurrencyError"
    )
}

fn sloppy_sqlite_provider_import_name_supported(name: &str) -> bool {
    matches!(name, "sqlite" | "Sqlite")
}

fn sloppy_fs_import_name_supported(name: &str) -> bool {
    matches!(
        name,
        "File" | "Directory" | "Path" | "FileHandle" | "FileWatcher"
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

fn sloppy_cache_import_name_supported(name: &str) -> bool {
    matches!(name, "Cache" | "SloppyCacheError")
}

fn sloppy_net_import_name_supported(name: &str) -> bool {
    matches!(
        name,
        "HttpClient"
            | "TcpClient"
            | "TcpListener"
            | "TcpConnection"
            | "LocalEndpoint"
            | "UnixSocket"
            | "NamedPipe"
            | "NetworkAddress"
            | "SloppyNetError"
    )
}

fn sloppy_http_import_name_supported(name: &str) -> bool {
    matches!(
        name,
        "Http" | "HttpClientFactory" | "HttpError" | "SloppyHttpClientError" | "TestHttp"
    )
}

fn sloppy_os_import_name_supported(name: &str) -> bool {
    matches!(
        name,
        "System" | "Environment" | "Process" | "ProcessHandle" | "Signals" | "OsError"
    )
}

fn sloppy_workers_import_name_supported(name: &str) -> bool {
    WORKER_EXPORTS.contains(&name)
}

fn sloppy_ffi_import_name_supported(name: &str) -> bool {
    matches!(name, "unsafeFfi" | "t")
}

#[derive(Debug, Clone, Copy)]
enum SloppyStdlibImport {
    Fs,
    Time,
    Crypto,
    Codec,
    Cache,
    Net,
    Http,
    Os,
    Orm,
    Workers,
    Ffi,
}

impl SloppyStdlibImport {
    fn from_source(source: &str) -> Option<Self> {
        match source {
            "sloppy/fs" => Some(Self::Fs),
            "sloppy/time" => Some(Self::Time),
            "sloppy/crypto" => Some(Self::Crypto),
            "sloppy/codec" => Some(Self::Codec),
            "sloppy/cache" => Some(Self::Cache),
            "sloppy/net" => Some(Self::Net),
            "sloppy/http" => Some(Self::Http),
            "sloppy/os" => Some(Self::Os),
            "sloppy/orm" => Some(Self::Orm),
            "sloppy/workers" => Some(Self::Workers),
            "sloppy/ffi" => Some(Self::Ffi),
            _ => None,
        }
    }

    fn name_supported(self, name: &str) -> bool {
        match self {
            Self::Fs => sloppy_fs_import_name_supported(name),
            Self::Time => sloppy_time_import_name_supported(name),
            Self::Crypto => sloppy_crypto_import_name_supported(name),
            Self::Codec => sloppy_codec_import_name_supported(name),
            Self::Cache => sloppy_cache_import_name_supported(name),
            Self::Net => sloppy_net_import_name_supported(name),
            Self::Http => sloppy_http_import_name_supported(name),
            Self::Os => sloppy_os_import_name_supported(name),
            Self::Orm => sloppy_orm_import_name_supported(name),
            Self::Workers => sloppy_workers_import_name_supported(name),
            Self::Ffi => sloppy_ffi_import_name_supported(name),
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

fn validate_module_sloppy_root_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<bool, Diagnostic> {
    let Some(specifiers) = &import.specifiers else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
            "unsupported import specifier \"sloppy\"",
        )
        .with_path(path)
        .with_span(import.source.span));
    };
    if specifiers.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
            "unsupported import specifier \"sloppy\"",
        )
        .with_path(path)
        .with_span(import.source.span));
    }

    let mut results_imported = false;
    for specifier in specifiers {
        let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
                "unsupported import specifier \"sloppy\"",
            )
            .with_path(path)
            .with_span(import.source.span));
        };
        let imported = specifier.imported.name().as_str();
        let local = specifier.local.name.as_str();
        if matches!(
            imported,
            "Testing" | "TestHost" | "TestServices" | "FakeClock" | "TestData"
        ) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_TESTING_IMPORT",
                "Sloppy testing helpers cannot be imported by compiled app source",
            )
            .with_path(path)
            .with_span(specifier.span)
            .with_hint("Use Sloppy testing helpers from JavaScript tests around the generated app, not inside compiler input."));
        }
        if !sloppy_root_import_name_supported(imported) || imported != local {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_IMPORT",
                format!("unsupported sloppy import \"{imported}\""),
            )
            .with_path(path)
            .with_span(specifier.span)
            .with_hint("Use documented unaliased imports from \"sloppy\"."));
        }
        if imported == "Results" {
            results_imported = true;
        }
    }
    Ok(results_imported)
}

fn mark_sloppy_root_runtime_usage(graph: &mut ModuleGraph, import: &ImportDeclaration<'_>) {
    let Some(specifiers) = &import.specifiers else {
        return;
    };
    for specifier in specifiers {
        let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
            continue;
        };
        if !import_specifier_is_runtime_value(import, specifier) {
            continue;
        }
        match specifier.imported.name().as_str() {
            "Cache" | "SloppyCacheError" => graph.uses_cache_runtime = true,
            "data" => graph.uses_data_runtime = true,
            "sql" => {
                graph.uses_data_runtime = true;
                graph.uses_sql_runtime = true;
            }
            "Migrations" => {
                graph.uses_data_runtime = true;
                graph.uses_migrations_runtime = true;
                graph.uses_fs_runtime = true;
            }
            "ProviderHealth" => {
                graph.uses_data_runtime = true;
                graph.uses_provider_health_runtime = true;
            }
            "orm" | "table" | "column" | "relation" => {
                graph.uses_orm_runtime = true;
                graph.uses_data_runtime = true;
                graph.uses_sql_runtime = true;
            }
            "Http" | "HttpClientFactory" | "HttpError" | "SloppyHttpClientError" | "TestHttp" => {
                graph.uses_http_client_runtime = true;
            }
            _ => {}
        }
    }
}

fn missing_results_import_diagnostic(path: &Path, span: Span) -> Diagnostic {
    Diagnostic::new(
        "SLOPPYC_E_UNSUPPORTED_IMPORT",
        "route handlers that call Results must import Results from \"sloppy\" in the same source file",
    )
    .with_path(path)
    .with_span(span)
    .with_hint("Add `import { Results } from \"sloppy\";` to the file that contains the handler.")
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

fn validate_module_sloppy_cache_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(
        path,
        import,
        "sloppy/cache",
        sloppy_cache_import_name_supported,
    )
}

fn validate_module_sloppy_fs_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(path, import, "sloppy/fs", sloppy_fs_import_name_supported)
}

fn validate_module_sloppy_data_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(
        path,
        import,
        "sloppy/data",
        sloppy_data_import_name_supported,
    )
}

fn validate_module_sloppy_orm_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(path, import, "sloppy/orm", sloppy_orm_import_name_supported)
}

fn validate_module_sloppy_sqlite_provider_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(
        path,
        import,
        "sloppy/providers/sqlite",
        sloppy_sqlite_provider_import_name_supported,
    )
}

fn validate_module_sloppy_net_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(path, import, "sloppy/net", sloppy_net_import_name_supported)
}

fn validate_module_sloppy_http_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(
        path,
        import,
        "sloppy/http",
        sloppy_http_import_name_supported,
    )
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

fn validate_module_sloppy_ffi_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    let Some(specifiers) = &import.specifiers else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
            "unsupported import specifier \"sloppy/ffi\"",
        )
        .with_path(path)
        .with_span(import.source.span));
    };
    if specifiers.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
            "unsupported import specifier \"sloppy/ffi\"",
        )
        .with_path(path)
        .with_span(import.source.span));
    }
    for specifier in specifiers {
        let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
                "unsupported import specifier \"sloppy/ffi\"",
            )
            .with_path(path)
            .with_span(import.source.span));
        };
        let imported = specifier.imported.name().as_str();
        let local = specifier.local.name.as_str();
        match imported {
            "unsafeFfi" if identifier_like(local) => {}
            "t" if identifier_like(local) => {}
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_IMPORT",
                    format!("unsupported sloppy import \"{imported}\""),
                )
                .with_path(path)
                .with_span(specifier.span)
                .with_hint("Use named imports from \"sloppy/ffi\": unsafeFfi and t."));
            }
        }
    }
    Ok(())
}

#[derive(Default)]
struct FfiImportBindings {
    ffi_names: BTreeSet<String>,
    type_names: BTreeSet<String>,
}

struct FfiSourceContext<'a> {
    path: &'a Path,
    source: &'a str,
    source_name: &'a str,
}

impl FfiImportBindings {
    fn has_bindings(&self) -> bool {
        !self.ffi_names.is_empty() || !self.type_names.is_empty()
    }
}

fn ffi_import_bindings(statements: &[Statement<'_>]) -> FfiImportBindings {
    let mut bindings = FfiImportBindings::default();

    for statement in statements {
        let Statement::ImportDeclaration(import) = statement else {
            continue;
        };
        if import.source.value.as_str() != "sloppy/ffi" {
            continue;
        }
        let Some(specifiers) = &import.specifiers else {
            continue;
        };
        for specifier in specifiers {
            let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
                continue;
            };
            if !import_specifier_is_runtime_value(import, specifier) {
                continue;
            }
            let imported = specifier.imported.name().as_str();
            let local = specifier.local.name.as_str();
            match imported {
                "unsafeFfi" => {
                    bindings.ffi_names.insert(local.to_string());
                }
                "t" => {
                    bindings.type_names.insert(local.to_string());
                }
                _ => {}
            }
        }
    }

    bindings
}

fn extract_ffi_declarations_from_statements(
    path: &Path,
    source: &str,
    source_name: &str,
    statements: &[Statement<'_>],
    libraries: &mut Vec<FfiLibraryMetadata>,
    structs: &mut Vec<FfiStructMetadata>,
) -> Result<(), Diagnostic> {
    let bindings = ffi_import_bindings(statements);
    if !bindings.has_bindings() {
        return Ok(());
    }
    let context = FfiSourceContext {
        path,
        source,
        source_name,
    };

    for statement in statements {
        let Some(declaration) = ffi_variable_declaration_from_statement(statement) else {
            continue;
        };
        for declarator in &declaration.declarations {
            let Some(init) = &declarator.init else {
                continue;
            };
            let Some(call) = ffi_static_member_call(init, &bindings.ffi_names) else {
                continue;
            };
            match call.0 {
                "library" => {
                    let library = extract_ffi_library_declaration(&context, call.1, &bindings)?;
                    libraries.push(library);
                }
                "struct" => {
                    let layout = extract_ffi_struct_declaration(&context, call.1, &bindings)?;
                    structs.push(layout);
                }
                _ => {}
            }
        }
    }

    Ok(())
}

fn ffi_variable_declaration_from_statement<'a>(
    statement: &'a Statement<'a>,
) -> Option<&'a VariableDeclaration<'a>> {
    match statement {
        Statement::VariableDeclaration(declaration) => Some(declaration),
        Statement::ExportNamedDeclaration(export)
            if export.export_kind != ImportOrExportKind::Type && export.source.is_none() =>
        {
            match &export.declaration {
                Some(Declaration::VariableDeclaration(declaration)) => Some(declaration),
                _ => None,
            }
        }
        _ => None,
    }
}

fn ffi_static_member_call<'a>(
    expression: &'a Expression<'a>,
    ffi_names: &BTreeSet<String>,
) -> Option<(&'a str, &'a CallExpression<'a>)> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let Expression::StaticMemberExpression(member) = &call.callee else {
        return None;
    };
    let Expression::Identifier(object) = &member.object else {
        return None;
    };
    if !ffi_names.contains(object.name.as_str()) {
        return None;
    }
    Some((member.property.name.as_str(), call))
}

fn ffi_function_descriptor_call<'a>(
    expression: &'a Expression<'a>,
    bindings: &FfiImportBindings,
) -> Option<&'a CallExpression<'a>> {
    let (member, call) = ffi_static_member_call(expression, &bindings.ffi_names)?;
    (member == "fn").then_some(call)
}

fn extract_ffi_library_declaration(
    context: &FfiSourceContext<'_>,
    call: &CallExpression<'_>,
    bindings: &FfiImportBindings,
) -> Result<FfiLibraryMetadata, Diagnostic> {
    let Some(library_name) = call.arguments.first().and_then(string_argument) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.library requires a static non-empty library name",
        ));
    };
    if library_name.is_empty() {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.library requires a non-empty library name",
        ));
    }
    let Some(functions_object) = call.arguments.get(1).and_then(object_argument) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.library requires a static object literal of functions",
        ));
    };
    let options = ffi_options_object(context, call, 2, "unsafeFfi.library")?;
    let convention = match options {
        Some(object) => {
            ffi_string_option(context, call.span, object, "convention")?.unwrap_or("system")
        }
        None => "system",
    };
    validate_ffi_convention(context.path, call.span, convention)?;

    let mut functions = Vec::new();
    for property in &functions_object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                call.span,
                "unsafeFfi.library function declarations do not support spreads",
            ));
        };
        if property.computed {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                property.span,
                "unsafeFfi.library function names must be static properties",
            ));
        }
        let Some(function_name) = property_key_name(&property.key) else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                property.span,
                "unsafeFfi.library function names must be static properties",
            ));
        };
        let Some(fn_call) = ffi_function_descriptor_call(&property.value, bindings) else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                property.span,
                "unsafeFfi.library values must be static unsafeFfi.fn(...) descriptors",
            ));
        };
        functions.push(extract_ffi_function_declaration(
            context,
            library_name,
            function_name,
            convention,
            fn_call,
            bindings,
        )?);
    }
    if functions.is_empty() {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.library requires at least one function",
        ));
    }

    Ok(FfiLibraryMetadata {
        name: library_name.to_string(),
        convention: convention.to_string(),
        functions,
        source_name: context.source_name.to_string(),
        source: context.source.to_string(),
        span: call.span,
    })
}

fn extract_ffi_function_declaration(
    context: &FfiSourceContext<'_>,
    library_name: &str,
    function_name: &str,
    library_convention: &str,
    call: &CallExpression<'_>,
    bindings: &FfiImportBindings,
) -> Result<FfiFunctionMetadata, Diagnostic> {
    let Some(return_expression) = call.arguments.first().and_then(Argument::as_expression) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.fn requires a static return type",
        ));
    };
    let return_type = ffi_type_name_from_expression(context.path, return_expression, bindings)?;
    if ffi_return_type_unsupported(&return_type) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_FFI_UNSUPPORTED_TYPE",
            format!("unsupported FFI return type \"{return_type}\""),
        )
        .with_path(context.path)
        .with_span(return_expression.span())
        .with_hint("Return cstring, utf16, bytes, and mutBytes through caller-owned buffers."));
    }
    let Some(Argument::ArrayExpression(parameters)) = call.arguments.get(1) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.fn requires a static array of parameter types",
        ));
    };
    let mut parameter_types = Vec::new();
    for element in &parameters.elements {
        let Some(expression) = element.as_expression() else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                oxc_span::GetSpan::span(element),
                "unsafeFfi.fn parameter types must be static type aliases",
            ));
        };
        let parameter_type = ffi_type_name_from_expression(context.path, expression, bindings)?;
        if parameter_type == "void" {
            return Err(Diagnostic::new(
                "SLOPPYC_E_FFI_UNSUPPORTED_TYPE",
                "FFI parameters cannot use void",
            )
            .with_path(context.path)
            .with_span(expression.span()));
        }
        parameter_types.push(parameter_type);
    }
    let options = ffi_options_object(context, call, 2, "unsafeFfi.fn")?;
    if options
        .map(|object| ffi_bool_option(context, call.span, object, "callback"))
        .transpose()?
        .flatten()
        .unwrap_or(false)
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_FFI_UNSUPPORTED_CALLBACK",
            "FFI callbacks are not supported",
        )
        .with_path(context.path)
        .with_span(call.span));
    }
    if options
        .map(|object| ffi_bool_option(context, call.span, object, "variadic"))
        .transpose()?
        .flatten()
        .unwrap_or(false)
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_FFI_UNSUPPORTED_VARIADIC",
            "FFI variadic functions are not supported",
        )
        .with_path(context.path)
        .with_span(call.span));
    }
    let symbol = match options {
        Some(object) => {
            ffi_string_option(context, call.span, object, "symbol")?.unwrap_or(function_name)
        }
        None => function_name,
    };
    if symbol.is_empty() {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.fn symbol must be a non-empty string",
        ));
    }
    let convention = match options {
        Some(object) => ffi_string_option(context, call.span, object, "convention")?
            .unwrap_or(library_convention),
        None => library_convention,
    };
    validate_ffi_convention(context.path, call.span, convention)?;

    Ok(FfiFunctionMetadata {
        id: format!("ffi:{library_name}:{function_name}"),
        name: function_name.to_string(),
        symbol: symbol.to_string(),
        convention: convention.to_string(),
        return_type,
        parameters: parameter_types,
        source_name: context.source_name.to_string(),
        source: context.source.to_string(),
        span: call.span,
    })
}

fn extract_ffi_struct_declaration(
    context: &FfiSourceContext<'_>,
    call: &CallExpression<'_>,
    bindings: &FfiImportBindings,
) -> Result<FfiStructMetadata, Diagnostic> {
    let Some(struct_name) = call.arguments.first().and_then(string_argument) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.struct requires a static non-empty struct name",
        ));
    };
    if struct_name.is_empty() {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.struct requires a non-empty struct name",
        ));
    }
    let Some(fields_object) = call.arguments.get(1).and_then(object_argument) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.struct requires a static object literal of fields",
        ));
    };
    let mut fields = Vec::new();
    for property in &fields_object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                call.span,
                "unsafeFfi.struct field declarations do not support spreads",
            ));
        };
        if property.computed {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                property.span,
                "unsafeFfi.struct field names must be static properties",
            ));
        }
        let Some(field_name) = property_key_name(&property.key) else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                property.span,
                "unsafeFfi.struct field names must be static properties",
            ));
        };
        let type_name = ffi_type_name_from_expression(context.path, &property.value, bindings)?;
        if !ffi_struct_field_type_supported(&type_name) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_FFI_UNSUPPORTED_STRUCT_BY_VALUE",
                format!("unsupported FFI struct field type \"{type_name}\""),
            )
            .with_path(context.path)
            .with_span(property.value.span())
            .with_hint(
                "Struct layouts support fixed-size primitive fields; pass structs by pointer.",
            ));
        }
        fields.push(FfiStructFieldMetadata {
            name: field_name.to_string(),
            type_name,
        });
    }
    if fields.is_empty() {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.struct requires at least one field",
        ));
    }
    let (layout, pack) = extract_ffi_struct_options(context.path, call)?;
    Ok(FfiStructMetadata {
        name: struct_name.to_string(),
        layout,
        pack,
        fields,
        source_name: context.source_name.to_string(),
        source: context.source.to_string(),
        span: call.span,
    })
}

fn extract_ffi_struct_options(
    path: &Path,
    call: &CallExpression<'_>,
) -> Result<(String, Option<u32>), Diagnostic> {
    let Some(options) = call.arguments.get(2).and_then(object_argument) else {
        return Ok(("sequential".to_string(), None));
    };
    let mut layout = "sequential".to_string();
    let mut pack = None;
    for property in &options.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(ffi_dynamic_declaration_diag(
                path,
                call.span,
                "unsafeFfi.struct options do not support spreads",
            ));
        };
        if property.computed {
            return Err(ffi_dynamic_declaration_diag(
                path,
                property.span,
                "unsafeFfi.struct option names must be static properties",
            ));
        }
        let Some(name) = property_key_name(&property.key) else {
            return Err(ffi_dynamic_declaration_diag(
                path,
                property.span,
                "unsafeFfi.struct option names must be static properties",
            ));
        };
        match name {
            "layout" => {
                let Expression::StringLiteral(value) = &property.value else {
                    return Err(ffi_dynamic_declaration_diag(
                        path,
                        property.span,
                        "unsafeFfi.struct layout must be a string literal",
                    ));
                };
                if value.value.as_str() != "sequential" {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_FFI_UNSUPPORTED_STRUCT_BY_VALUE",
                        "FFI structs only support sequential layout",
                    )
                    .with_path(path)
                    .with_span(property.value.span())
                    .with_hint(
                        "Unions, bitfields, and struct-by-value layouts are not supported.",
                    ));
                }
                layout = value.value.as_str().to_string();
            }
            "pack" => {
                let Expression::NumericLiteral(value) = &property.value else {
                    return Err(ffi_dynamic_declaration_diag(
                        path,
                        property.span,
                        "unsafeFfi.struct pack must be a numeric literal",
                    ));
                };
                if value.value.fract() != 0.0
                    || !matches!(value.value as u32, 1 | 2 | 4 | 8 | 16)
                    || value.value < 1.0
                    || value.value > 16.0
                {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_FFI_UNSUPPORTED_STRUCT_BY_VALUE",
                        "FFI struct pack must be 1, 2, 4, 8, or 16",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                }
                pack = Some(value.value as u32);
            }
            _ => {
                return Err(ffi_dynamic_declaration_diag(
                    path,
                    property.span,
                    format!("unsupported unsafeFfi.struct option \"{name}\""),
                ));
            }
        }
    }
    Ok((layout, pack))
}

fn ffi_type_name_from_expression(
    path: &Path,
    expression: &Expression<'_>,
    bindings: &FfiImportBindings,
) -> Result<String, Diagnostic> {
    let Expression::StaticMemberExpression(member) = expression else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_FFI_INVALID_TYPE",
            "FFI types must be static properties from the t namespace",
        )
        .with_path(path)
        .with_span(expression.span()));
    };
    let Expression::Identifier(object) = &member.object else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_FFI_INVALID_TYPE",
            "FFI types must be static properties from the t namespace",
        )
        .with_path(path)
        .with_span(expression.span()));
    };
    if !bindings.type_names.contains(object.name.as_str()) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_FFI_INVALID_TYPE",
            "FFI type declarations must use the imported t namespace",
        )
        .with_path(path)
        .with_span(expression.span()));
    }
    let name = member.property.name.as_str();
    if !ffi_type_supported(name) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_FFI_INVALID_TYPE",
            format!("unsupported FFI type \"{name}\""),
        )
        .with_path(path)
        .with_span(expression.span()));
    }
    Ok(name.to_string())
}

fn ffi_type_supported(name: &str) -> bool {
    matches!(
        name,
        "void"
            | "bool"
            | "bool32"
            | "i8"
            | "u8"
            | "i16"
            | "u16"
            | "i32"
            | "u32"
            | "i64"
            | "u64"
            | "isize"
            | "usize"
            | "f32"
            | "f64"
            | "ptr"
            | "handle"
            | "hwnd"
            | "hmodule"
            | "ntstatus"
            | "cstring"
            | "lpcstr"
            | "utf16"
            | "lpcwstr"
            | "bytes"
            | "mutBytes"
    )
}

fn ffi_return_type_unsupported(name: &str) -> bool {
    matches!(
        name,
        "cstring" | "lpcstr" | "utf16" | "lpcwstr" | "bytes" | "mutBytes"
    )
}

fn ffi_struct_field_type_supported(name: &str) -> bool {
    matches!(
        name,
        "bool"
            | "bool32"
            | "i8"
            | "u8"
            | "i16"
            | "u16"
            | "i32"
            | "u32"
            | "i64"
            | "u64"
            | "isize"
            | "usize"
            | "f32"
            | "f64"
            | "ptr"
            | "handle"
            | "hwnd"
            | "hmodule"
            | "ntstatus"
    )
}

fn validate_ffi_convention(path: &Path, span: Span, convention: &str) -> Result<(), Diagnostic> {
    if matches!(convention, "system" | "cdecl" | "stdcall") {
        return Ok(());
    }
    Err(Diagnostic::new(
        "SLOPPYC_E_FFI_UNSUPPORTED_CALLING_CONVENTION",
        format!("unsupported FFI calling convention \"{convention}\""),
    )
    .with_path(path)
    .with_span(span)
    .with_hint("Supported FFI conventions are system, cdecl, and stdcall."))
}

fn ffi_options_object<'a>(
    context: &FfiSourceContext<'_>,
    call: &'a CallExpression<'a>,
    index: usize,
    operation: &str,
) -> Result<Option<&'a oxc_ast::ast::ObjectExpression<'a>>, Diagnostic> {
    let Some(argument) = call.arguments.get(index) else {
        return Ok(None);
    };
    object_argument(argument).map(Some).ok_or_else(|| {
        ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            format!("{operation} options must be a static object literal"),
        )
    })
}

fn ffi_string_option<'a>(
    context: &FfiSourceContext<'_>,
    span: Span,
    object: &'a oxc_ast::ast::ObjectExpression<'a>,
    name: &str,
) -> Result<Option<&'a str>, Diagnostic> {
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                span,
                "FFI options do not support spreads",
            ));
        };
        if property.computed || property_key_name(&property.key) != Some(name) {
            continue;
        }
        let Expression::StringLiteral(value) = &property.value else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                property.span,
                format!("FFI option \"{name}\" must be a string literal"),
            ));
        };
        return Ok(Some(value.value.as_str()));
    }
    Ok(None)
}

fn ffi_bool_option(
    context: &FfiSourceContext<'_>,
    span: Span,
    object: &oxc_ast::ast::ObjectExpression<'_>,
    name: &str,
) -> Result<Option<bool>, Diagnostic> {
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                span,
                "FFI options do not support spreads",
            ));
        };
        if property.computed || property_key_name(&property.key) != Some(name) {
            continue;
        }
        let Expression::BooleanLiteral(value) = &property.value else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                property.span,
                format!("FFI option \"{name}\" must be a boolean literal"),
            ));
        };
        return Ok(Some(value.value));
    }
    Ok(None)
}

fn ffi_dynamic_declaration_diag(path: &Path, span: Span, message: impl Into<String>) -> Diagnostic {
    Diagnostic::new("SLOPPYC_E_FFI_DYNAMIC_DECLARATION", message)
        .with_path(path)
        .with_span(span)
        .with_hint("FFI declarations must be static so the compiler can emit Plan metadata.")
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
        SloppyStdlibImport::Cache => state.cache_imported = true,
        SloppyStdlibImport::Net => state.net_imported = true,
        SloppyStdlibImport::Http => state.http_client_imported = true,
        SloppyStdlibImport::Os => state.os_imported = true,
        SloppyStdlibImport::Orm => {
            state.orm_imported = true;
            state.data_imported = true;
            state.sql_imported = true;
        }
        SloppyStdlibImport::Workers => state.workers_imported = true,
        SloppyStdlibImport::Ffi => state.ffi_imported = true,
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

fn mark_sloppy_data_runtime_usage(graph: &mut ModuleGraph, import: &ImportDeclaration<'_>) {
    graph.uses_data_runtime = true;
    if let Some(specifiers) = &import.specifiers {
        for specifier in specifiers {
            if let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier {
                if !import_specifier_is_runtime_value(import, specifier) {
                    continue;
                }
                let imported = specifier.imported.name().as_str();
                if imported == "sql" {
                    graph.uses_sql_runtime = true;
                }
                if imported == "Migrations" {
                    graph.uses_migrations_runtime = true;
                    graph.uses_fs_runtime = true;
                }
                if imported == "ProviderHealth" {
                    graph.uses_provider_health_runtime = true;
                }
            }
        }
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
        let local_allowed = imported == local || matches!(kind, SloppyStdlibImport::Ffi);
        if kind.name_supported(imported) && local_allowed {
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
    graph: &mut ModuleGraph,
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
        graph.add_relative_dependency_import(path, import_source, &resolved);
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
                for helper in extract_relative_helper_import(
                    graph,
                    &ImportedModule {
                        local_name: specifier.local.name.as_str().to_string(),
                        export_name: specifier.imported.name().as_str().to_string(),
                        path: resolved.clone(),
                        span: specifier.span,
                    },
                )? {
                    state
                        .helper_sources
                        .entry(helper.name.clone())
                        .or_insert(helper.source);
                    state
                        .helper_effects
                        .entry(helper.name)
                        .or_insert(helper.summary);
                }
                resolve_helper_effect_callgraph(&mut state.helper_effects);
            }
        }
        return Ok(());
    }

    match resolver::classify_import(path, import_source) {
        resolver::ImportKind::Package(package_resolution) => {
            extract_package_helper_imports(path, graph, state, import, &package_resolution)?;
            return Ok(());
        }
        resolver::ImportKind::NativeAddonUnsupported(package_resolution) => {
            graph.add_package_record(&package_resolution);
            state.unsupported_import_specifier =
                Some((import.source.value.as_str().to_string(), import.source.span));
            return Ok(());
        }
        resolver::ImportKind::PackageExportUnsupported(_)
        | resolver::ImportKind::UnsupportedBare(_) => {}
        _ => {}
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
                    if import_specifier_is_runtime_value(import, specifier) {
                        state.sql_imported = true;
                        state.data_imported = true;
                    }
                } else if imported == "Migrations" && local == "Migrations" {
                    if import_specifier_is_runtime_value(import, specifier) {
                        state.data_imported = true;
                        state.migrations_imported = true;
                        state.fs_imported = true;
                    }
                } else if imported == "ProviderHealth" && local == "ProviderHealth" {
                    if import_specifier_is_runtime_value(import, specifier) {
                        state.data_imported = true;
                        state.provider_health_imported = true;
                    }
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
            if matches!(
                imported,
                "Testing" | "TestHost" | "TestServices" | "FakeClock" | "TestData"
            ) {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_TESTING_IMPORT",
                    "Sloppy testing helpers cannot be imported by compiled app source",
                )
                .with_path(path)
                .with_span(specifier.span)
                .with_hint("Use Sloppy testing helpers from JavaScript tests around the generated app, not inside compiler input."));
            }
            if sloppy_root_import_name_supported(imported) && imported != local {
                state.unsupported_import_alias = true;
                state.unsupported_import_name = Some((imported.to_string(), specifier.span));
            }
            match (imported, local) {
                ("Sloppy", "Sloppy") => state.sloppy_imported = true,
                ("Results", "Results") => state.results_imported = true,
                ("Auth", "Auth") => state.auth_imported = true,
                ("Realtime", "Realtime") => state.realtime_imported = true,
                ("SloppyRealtimeError", "SloppyRealtimeError") => state.realtime_imported = true,
                ("Config", "Config") => state.config_imported = true,
                ("Cache", "Cache") => state.cache_imported = true,
                ("SloppyCacheError", "SloppyCacheError") => state.cache_imported = true,
                ("ProblemDetails", "ProblemDetails") => state.problem_details_imported = true,
                ("RequestId", "RequestId") => state.request_id_imported = true,
                ("RequestLogging", "RequestLogging") => state.request_logging_imported = true,
                ("data", "data") => state.data_imported = true,
                ("sql", "sql") => {
                    if import_specifier_is_runtime_value(import, specifier) {
                        state.data_imported = true;
                        state.sql_imported = true;
                    }
                }
                ("Migrations", "Migrations") => {
                    if import_specifier_is_runtime_value(import, specifier) {
                        state.data_imported = true;
                        state.migrations_imported = true;
                        state.fs_imported = true;
                    }
                }
                ("ProviderHealth", "ProviderHealth") => {
                    if import_specifier_is_runtime_value(import, specifier) {
                        state.data_imported = true;
                        state.provider_health_imported = true;
                    }
                }
                ("orm", "orm")
                | ("table", "table")
                | ("column", "column")
                | ("relation", "relation") => {
                    if import_specifier_is_runtime_value(import, specifier) {
                        state.orm_imported = true;
                        state.data_imported = true;
                        state.sql_imported = true;
                    }
                }
                ("schema", "schema") => state.schema_imported = true,
                ("Schema", "Schema") => state.schema_imported = true,
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

fn extract_package_helper_imports(
    path: &Path,
    graph: &mut ModuleGraph,
    state: &mut AppState,
    import: &oxc_ast::ast::ImportDeclaration<'_>,
    package: &resolver::PackageResolution,
) -> Result<(), Diagnostic> {
    graph.add_package_record(package);
    let package_id = program_module_id(graph, &package.entry, Some(&package.name));
    graph.add_dependency_module(
        package_id.clone(),
        package_id.clone(),
        package.format,
        Some(package.name.clone()),
        Some(resolver::normalized_artifact_id(path, &graph.entry_dir)),
    );
    graph.add_package_dependency_import(path, import.source.value.as_str(), &package_id);
    let Some(specifiers) = &import.specifiers else {
        state.unsupported_import_specifier =
            Some((import.source.value.as_str().to_string(), import.source.span));
        return Ok(());
    };
    for specifier in specifiers {
        let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
            state.unsupported_import_specifier =
                Some((import.source.value.as_str().to_string(), import.source.span));
            return Ok(());
        };
        let imported = ImportedModule {
            local_name: specifier.local.name.as_str().to_string(),
            export_name: specifier.imported.name().as_str().to_string(),
            path: package.entry.clone(),
            span: specifier.span,
        };
        for helper in extract_relative_helper_import(graph, &imported)? {
            state
                .helper_sources
                .entry(helper.name.clone())
                .or_insert(helper.source);
            state
                .helper_effects
                .entry(helper.name)
                .or_insert(helper.summary);
        }
        resolve_helper_effect_callgraph(&mut state.helper_effects);
    }
    Ok(())
}

fn sloppy_root_import_name_supported(name: &str) -> bool {
    matches!(
        name,
        "Sloppy"
            | "Results"
            | "Auth"
            | "Cache"
            | "SloppyCacheError"
            | "Realtime"
            | "SloppyRealtimeError"
            | "ProblemDetails"
            | "RequestId"
            | "RequestLogging"
            | "Health"
            | "Metrics"
            | "Http"
            | "HttpClientFactory"
            | "HttpError"
            | "SloppyHttpClientError"
            | "TestHttp"
            | "Testing"
            | "TestHost"
            | "TestServices"
            | "FakeClock"
            | "TestData"
            | "data"
            | "sql"
            | "Migrations"
            | "ProviderHealth"
            | "orm"
            | "table"
            | "column"
            | "relation"
            | "schema"
            | "Schema"
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
        _ => None,
    }
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

    let schema_names = schema_names(state);
    let handler_context = HandlerExtractionContext {
        route_pattern: &full_pattern,
        route_kind: kind,
        source,
        source_name,
        allow_data_handler_body: state.data_imported,
        schema_names: &schema_names,
        provider_bindings: &state.provider_bindings,
        helper_effects: &state.helper_effects,
    };
    validate_handler_body_validate_schema_references(path, handler_arg, &schema_names)?;
    let Some(handler) = handler_from_argument(handler_arg, &handler_context) else {
        let diagnostic = handler_diagnostic(
            path,
            handler_arg,
            &full_pattern,
            &schema_names,
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
        &schema_names,
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
                schema_names: &schema_names(state),
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
            &schema_names(state),
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
            let schema_names = schema_names(state);
            let context = HandlerExtractionContext {
                route_pattern: "",
                route_kind: "http",
                source,
                source_name,
                allow_data_handler_body: state.data_imported,
                schema_names: &schema_names,
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
        &schema_names(state),
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
        config_key: optional_object_string_property(path, options, "configKey")?
            .map(ToOwned::to_owned),
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
            "conditional route registration cannot be represented as complete static metadata",
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Register compiler-extracted routes unconditionally at the top level.");
    }
    if top_level_statement_is_loop(statement) && text.contains(".map") {
        return Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_LOOP_ROUTE_REGISTRATION",
            "loop-based route registration cannot be represented as complete static metadata",
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

fn record_dynamic_top_level_route_statement(
    source: &str,
    source_name: &str,
    statement: &Statement<'_>,
    state: &mut AppState,
) -> bool {
    if !(top_level_statement_is_conditional(statement) || top_level_statement_is_loop(statement)) {
        return false;
    }
    let text = source_slice(source, statement.span()).unwrap_or_default();
    if !text.contains(".get(")
        && !text.contains(".post(")
        && !text.contains(".put(")
        && !text.contains(".patch(")
        && !text.contains(".delete(")
        && !text.contains(".map")
    {
        return false;
    }
    state.dynamic_routes.push(DynamicRoute {
        method: None,
        pattern: None,
        pattern_reason: "route registration is inside dynamic control flow",
        handler_known: false,
        reason: if top_level_statement_is_conditional(statement) {
            "conditional route registration cannot be represented as complete static metadata"
        } else {
            "loop-based route registration cannot be represented as complete static metadata"
        },
        span: statement.span(),
        source_name: source_name.to_string(),
        source: source.to_string(),
    });
    true
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

fn app_group_call<'a>(
    expression: &'a Expression<'a>,
) -> Result<Option<(&'a str, &'a str, RouteMetadata)>, Diagnostic> {
    let mut current = expression;
    let mut metadata = RouteMetadata::default();
    let mut tag_groups = Vec::new();
    loop {
        let Expression::CallExpression(call) = current else {
            return Ok(None);
        };
        let Expression::StaticMemberExpression(member) = &call.callee else {
            break;
        };
        match member.property.name.as_str() {
            "withTags" => {
                tag_groups.push(route_tags_from_arguments(call)?);
                current = &member.object;
            }
            "requireAuth" | "requiresAuth" => {
                merge_auth_requirement(&mut metadata.auth, auth_requirement_from_call(call)?);
                current = &member.object;
            }
            "allowAnonymous" => {
                merge_auth_requirement(&mut metadata.auth, anonymous_auth_requirement());
                current = &member.object;
            }
            _ => break,
        }
    }
    for tags in tag_groups.into_iter().rev() {
        metadata.tags.extend(tags);
    }

    let Expression::CallExpression(call) = current else {
        return Ok(None);
    };
    let Some((object, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if !matches!(property, "mapGroup" | "group") || call.arguments.len() != 1 {
        return Ok(None);
    }
    let Some(prefix) = call.arguments.first().and_then(string_argument) else {
        return Ok(None);
    };
    Ok(Some((object, prefix, metadata)))
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
        config_key: None,
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

fn app_use_problem_details_call(
    path: &Path,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<ProblemDetailsDescriptor>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if property != "use" || !state.app_vars.contains(receiver) {
        return Ok(None);
    }
    let Some(Argument::CallExpression(descriptor_call)) = call.arguments.first() else {
        return Ok(None);
    };
    if call.arguments.len() != 1 {
        return Ok(None);
    }
    let Some((descriptor, method)) = static_member_name(&descriptor_call.callee) else {
        return Ok(None);
    };
    if descriptor != "ProblemDetails" || method != "defaults" {
        return Ok(None);
    }
    if !state.problem_details_imported {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT",
            "ProblemDetails.defaults() requires importing ProblemDetails from \"sloppy\"",
        )
        .with_path(path)
        .with_span(descriptor_call.span));
    }
    if descriptor_call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_PROBLEM_DETAILS",
            "ProblemDetails.defaults accepts at most one options object",
        )
        .with_path(path)
        .with_span(descriptor_call.span));
    }
    let mut detail = "never".to_string();
    if let Some(argument) = descriptor_call.arguments.first() {
        let Some(object) = object_argument(argument) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_PROBLEM_DETAILS",
                "ProblemDetails.defaults options must be an object literal",
            )
            .with_path(path)
            .with_span(argument_span(argument).unwrap_or(descriptor_call.span)));
        };
        if let Some(value) = object_string_property_value(object, "detail") {
            if value != "never" && value != "development" && value != "always" {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_PROBLEM_DETAILS",
                    "ProblemDetails detail policy must be \"never\", \"development\", or \"always\"",
                )
                .with_path(path)
                .with_span(object.span));
            }
            detail = value.to_string();
        }
    }
    Ok(Some(ProblemDetailsDescriptor { detail }))
}

fn app_use_request_id_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let Some((call, descriptor_call)) = app_use_descriptor_call(expression, state, "RequestId")
    else {
        return Ok(false);
    };
    if !state.request_id_imported {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
            "RequestId.defaults() requires importing RequestId from \"sloppy\"",
        )
        .with_path(path)
        .with_span(descriptor_call.span));
    }
    let options = request_id_options_from_call(path, descriptor_call)?;
    let source_json = serde_json::to_string(&json!({
        "header": options.header,
        "responseHeader": options.response_header,
        "trustIncoming": options.trust_incoming
    }))
    .unwrap_or_else(|_| "{}".to_string());
    let middleware = FrameworkMiddleware {
        kind: FrameworkMiddlewareKind::RequestId,
        source: format!("__sloppy_request_id({source_json})"),
        sequence: state.next_middleware_sequence,
        source_name: source_name.to_string(),
        source_text: source.to_string(),
        span: call.span,
    };
    state.next_middleware_sequence += 1;
    state.middleware.push(middleware);
    Ok(true)
}

fn app_use_request_logging_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let Some((call, descriptor_call)) =
        app_use_descriptor_call(expression, state, "RequestLogging")
    else {
        return Ok(false);
    };
    if !state.request_logging_imported {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
            "RequestLogging.defaults() requires importing RequestLogging from \"sloppy\"",
        )
        .with_path(path)
        .with_span(descriptor_call.span));
    }
    let options = request_logging_options_from_call(path, descriptor_call)?;
    let source_json = serde_json::to_string(&json!({
        "includeRoute": options.include_route,
        "includeDuration": options.include_duration,
        "includeRequestId": options.include_request_id
    }))
    .unwrap_or_else(|_| "{}".to_string());
    let middleware = FrameworkMiddleware {
        kind: FrameworkMiddlewareKind::RequestLogging,
        source: format!("__sloppy_request_logging({source_json})"),
        sequence: state.next_middleware_sequence,
        source_name: source_name.to_string(),
        source_text: source.to_string(),
        span: call.span,
    };
    state.next_middleware_sequence += 1;
    state.middleware.push(middleware);
    Ok(true)
}

fn app_use_descriptor_call<'a>(
    expression: &'a Expression<'a>,
    state: &AppState,
    descriptor: &str,
) -> Option<(&'a CallExpression<'a>, &'a CallExpression<'a>)> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if property != "use" || !state.app_vars.contains(receiver) || call.arguments.len() != 1 {
        return None;
    }
    let Some(Argument::CallExpression(descriptor_call)) = call.arguments.first() else {
        return None;
    };
    let (object, method) = static_member_name(&descriptor_call.callee)?;
    (object == descriptor && method == "defaults").then_some((call, descriptor_call))
}

fn request_id_options_from_call(
    path: &Path,
    call: &CallExpression<'_>,
) -> Result<RequestIdOptions, Diagnostic> {
    if call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
            "RequestId.defaults accepts at most one literal options object",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let mut options = RequestIdOptions {
        header: "x-request-id".to_string(),
        response_header: true,
        trust_incoming: false,
    };
    let Some(argument) = call.arguments.first() else {
        return Ok(options);
    };
    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
            "dynamic RequestId options are not supported by compiler extraction",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                "RequestId options must use literal properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        if property.kind != PropertyKind::Init || property.method || property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                "RequestId options must use simple literal properties",
            )
            .with_path(path)
            .with_span(property.span));
        }
        let Some(name) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                "RequestId option names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        match name {
            "header" => {
                let Expression::StringLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                        "RequestId header must be a string literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                if !compiler_header_name_allowed(value.value.as_str()) {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                        "RequestId header must be a safe unmanaged HTTP header name",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                }
                options.header = value.value.as_str().to_string();
            }
            "responseHeader" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                        "RequestId responseHeader option must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                options.response_header = value.value;
            }
            "trustIncoming" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                        "RequestId trustIncoming option must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                options.trust_incoming = value.value;
            }
            "generator" => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                    "RequestId generator callbacks are unsupported in compiler source input",
                )
                .with_path(path)
                .with_span(property.value.span())
                .with_hint("Use static RequestId options in compiler input; generator callbacks remain an app-host test helper."));
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                    format!("unsupported RequestId option '{name}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }
    Ok(options)
}

fn request_logging_options_from_call(
    path: &Path,
    call: &CallExpression<'_>,
) -> Result<RequestLoggingOptions, Diagnostic> {
    if call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
            "RequestLogging.defaults accepts at most one literal options object",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let mut options = RequestLoggingOptions {
        include_route: true,
        include_duration: true,
        include_request_id: true,
    };
    let Some(argument) = call.arguments.first() else {
        return Ok(options);
    };
    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
            "dynamic RequestLogging options are not supported by compiler extraction",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
                "RequestLogging options must use literal properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        if property.kind != PropertyKind::Init || property.method || property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
                "RequestLogging options must use simple literal properties",
            )
            .with_path(path)
            .with_span(property.span));
        }
        let Some(name) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
                "RequestLogging option names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        let Expression::BooleanLiteral(value) = &property.value else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
                "RequestLogging options must be boolean literals",
            )
            .with_path(path)
            .with_span(property.value.span()));
        };
        match name {
            "includeRoute" => options.include_route = value.value,
            "includeDuration" => options.include_duration = value.value,
            "includeRequestId" => options.include_request_id = value.value,
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
                    format!("unsupported RequestLogging option '{name}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }
    Ok(options)
}

#[derive(Debug, Clone)]
struct StaticFilesOptions {
    request_path: String,
    root: String,
    max_age_seconds: Option<u64>,
}

struct StaticFilesRouteContext<'a> {
    path: &'a Path,
    source: &'a str,
    source_name: &'a str,
    span: Span,
    middleware: &'a [FrameworkMiddleware],
    cors: Option<&'a CorsPolicy>,
}

fn app_use_static_files_call(
    path: &Path,
    source: &str,
    source_name: &str,
    graph: &mut ModuleGraph,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(false);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(false);
    };
    if property != "useStaticFiles" || !state.app_vars.contains(receiver) {
        return Ok(false);
    }
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
            "app.useStaticFiles requires one literal options object",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let options = static_files_options_from_call(path, call)?;
    let context = StaticFilesRouteContext {
        path,
        source,
        source_name,
        span: call.span,
        middleware: &state.middleware,
        cors: state.cors_policy.as_ref(),
    };
    let routes = static_file_routes_from_options(&context, graph, &options)?;
    state.static_asset_routes.extend(routes);
    Ok(true)
}

fn static_files_options_from_call(
    path: &Path,
    call: &CallExpression<'_>,
) -> Result<StaticFilesOptions, Diagnostic> {
    let Some(argument) = call.arguments.first() else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
            "app.useStaticFiles requires one literal options object",
        )
        .with_path(path)
        .with_span(call.span));
    };
    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
            "app.useStaticFiles options must be a literal object",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };

    let mut request_path = None;
    let mut root = None;
    let mut max_age_seconds = None;
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(static_files_options_diagnostic(path, object.span));
        };
        if property.kind != PropertyKind::Init || property.method || property.computed {
            return Err(static_files_options_diagnostic(path, property.span));
        }
        let Some(name) = property_key_name(&property.key) else {
            return Err(static_files_options_diagnostic(path, property.span));
        };
        match name {
            "requestPath" => {
                let Expression::StringLiteral(value) = &property.value else {
                    return Err(static_files_options_diagnostic(path, property.value.span()));
                };
                request_path = Some(value.value.as_str().to_string());
            }
            "root" => {
                let Expression::StringLiteral(value) = &property.value else {
                    return Err(static_files_options_diagnostic(path, property.value.span()));
                };
                root = Some(value.value.as_str().to_string());
            }
            "cache" => {
                max_age_seconds = static_files_cache_max_age(path, &property.value)?;
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
                    format!("unsupported app.useStaticFiles option '{name}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }

    let request_path = request_path.ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
            "app.useStaticFiles options must include requestPath",
        )
        .with_path(path)
        .with_span(object.span)
    })?;
    let root = root.ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
            "app.useStaticFiles options must include root",
        )
        .with_path(path)
        .with_span(object.span)
    })?;

    if !static_files_request_path_supported(&request_path) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
            "app.useStaticFiles requestPath must be an absolute static route prefix",
        )
        .with_path(path)
        .with_span(object.span)
        .with_hint("Use a value like \"/public\" without route parameters or a trailing slash."));
    }
    if !static_files_root_supported(&root) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
            "app.useStaticFiles root must be a safe project-relative directory",
        )
        .with_path(path)
        .with_span(object.span)
        .with_hint(
            "Use a relative directory like \"public\"; traversal and absolute paths are rejected.",
        ));
    }

    Ok(StaticFilesOptions {
        request_path,
        root,
        max_age_seconds,
    })
}

fn static_files_options_diagnostic(path: &Path, span: Span) -> Diagnostic {
    Diagnostic::new(
        "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
        "app.useStaticFiles options must use simple literal properties",
    )
    .with_path(path)
    .with_span(span)
}

fn static_files_cache_max_age(
    path: &Path,
    expression: &Expression<'_>,
) -> Result<Option<u64>, Diagnostic> {
    let Expression::ObjectExpression(object) = expression else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
            "app.useStaticFiles cache must be a literal object",
        )
        .with_path(path)
        .with_span(expression.span()));
    };
    let mut max_age_seconds = None;
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(static_files_options_diagnostic(path, object.span));
        };
        if property.kind != PropertyKind::Init || property.method || property.computed {
            return Err(static_files_options_diagnostic(path, property.span));
        }
        let Some(name) = property_key_name(&property.key) else {
            return Err(static_files_options_diagnostic(path, property.span));
        };
        match name {
            "maxAgeSeconds" => {
                let Expression::NumericLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
                        "app.useStaticFiles cache.maxAgeSeconds must be a non-negative integer literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                if value.value < 0.0 || value.value.fract() != 0.0 || value.value > u64::MAX as f64
                {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
                        "app.useStaticFiles cache.maxAgeSeconds must be a non-negative integer literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                }
                max_age_seconds = Some(value.value as u64);
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
                    format!("unsupported app.useStaticFiles cache option '{name}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }
    Ok(max_age_seconds)
}

fn static_files_request_path_supported(request_path: &str) -> bool {
    request_path == "/"
        || (route_pattern_supported(request_path)
            && !request_path.contains('{')
            && !request_path.contains('}')
            && !request_path.ends_with('/'))
}

fn static_files_root_supported(root: &str) -> bool {
    include_pattern_is_safe(root) && !root.contains('*') && !root.contains('?')
}

const STATIC_ASSET_INLINE_MAX_BYTES: u64 = 1024 * 1024;

fn static_file_routes_from_options(
    context: &StaticFilesRouteContext<'_>,
    graph: &mut ModuleGraph,
    options: &StaticFilesOptions,
) -> Result<Vec<Route>, Diagnostic> {
    let root = graph.entry_dir.join(&options.root);
    let canonical_root = fs::canonicalize(&root).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_STATIC_FILES",
            format!("failed to read app.useStaticFiles root: {error}"),
        )
        .with_path(&root)
        .with_span(context.span)
    })?;
    if !canonical_root.is_dir() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_STATIC_FILES",
            "app.useStaticFiles root must be a directory",
        )
        .with_path(&root)
        .with_span(context.span));
    }
    if !canonical_root.starts_with(&graph.entry_dir) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_STATIC_FILES",
            "app.useStaticFiles root must stay inside the project source root",
        )
        .with_path(&root)
        .with_span(context.span));
    }

    let mut files = Vec::new();
    collect_static_asset_files(&canonical_root, &canonical_root, &mut files).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_STATIC_FILES",
            format!("failed to enumerate app.useStaticFiles assets: {error}"),
        )
        .with_path(&canonical_root)
        .with_span(context.span)
    })?;
    if files.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_STATIC_FILES",
            "app.useStaticFiles root contains no supported static assets",
        )
        .with_path(&canonical_root)
        .with_span(context.span));
    }

    let mut routes = Vec::new();
    for file in files {
        let relative = file.strip_prefix(&canonical_root).map_err(|_| {
            Diagnostic::new(
                "SLOPPYC_E_STATIC_FILES",
                "app.useStaticFiles asset escaped the configured root",
            )
            .with_path(&file)
            .with_span(context.span)
        })?;
        let route_path =
            static_asset_route_path(&options.request_path, relative).ok_or_else(|| {
                Diagnostic::new(
                    "SLOPPYC_E_STATIC_FILES",
                    "static asset path cannot be represented as a Sloppy alpha route",
                )
                .with_path(&file)
                .with_span(context.span)
            })?;
        let metadata = fs::metadata(&file).map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_STATIC_FILES",
                format!("failed to read static asset metadata: {error}"),
            )
            .with_path(&file)
            .with_span(context.span)
        })?;
        if metadata.len() > STATIC_ASSET_INLINE_MAX_BYTES {
            return Err(Diagnostic::new(
                "SLOPPYC_E_STATIC_FILES",
                format!(
                    "app.useStaticFiles asset exceeds the alpha inline limit of {STATIC_ASSET_INLINE_MAX_BYTES} bytes"
                ),
            )
            .with_path(&file)
            .with_span(context.span));
        }
        let bytes = fs::read(&file).map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_STATIC_FILES",
                format!("failed to read static asset: {error}"),
            )
            .with_path(&file)
            .with_span(context.span)
        })?;
        let content_type = static_asset_content_type(&file).ok_or_else(|| {
            Diagnostic::new(
                "SLOPPYC_E_STATIC_FILES",
                "static asset content type is not supported",
            )
            .with_path(&file)
            .with_span(context.span)
        })?;
        graph.add_dependency_asset(
            &file,
            format!(
                "app.useStaticFiles:{}:{}",
                options.request_path, options.root
            ),
        );
        routes.push(static_asset_route(
            context,
            route_path,
            content_type,
            &bytes,
            options.max_age_seconds,
        ));
    }
    Ok(routes)
}

fn collect_static_asset_files(
    root: &Path,
    dir: &Path,
    files: &mut Vec<PathBuf>,
) -> Result<(), std::io::Error> {
    let mut entries = fs::read_dir(dir)?.collect::<Result<Vec<_>, _>>()?;
    entries.sort_by_key(|entry| entry.file_name());
    for entry in entries {
        let path = entry.path();
        let file_type = entry.file_type()?;
        if file_type.is_dir() {
            collect_static_asset_files(root, &path, files)?;
        } else if file_type.is_file() && static_asset_content_type(&path).is_some() {
            files.push(path);
        }
    }
    files.sort_by_key(|path| resolver::normalized_artifact_id(path, root));
    Ok(())
}

fn static_asset_route_path(request_path: &str, relative: &Path) -> Option<String> {
    let relative = resolver::normalized_artifact_id(relative, Path::new(""));
    if relative.is_empty()
        || relative.starts_with('/')
        || relative.contains("//")
        || relative.split('/').any(|segment| {
            segment.is_empty()
                || segment == "."
                || segment == ".."
                || segment.contains('{')
                || segment.contains('}')
        })
    {
        return None;
    }
    let path = if request_path == "/" {
        format!("/{relative}")
    } else {
        format!("{request_path}/{relative}")
    };
    route_pattern_supported(&path).then_some(path)
}

fn static_asset_content_type(path: &Path) -> Option<&'static str> {
    match path.extension()?.to_str()?.to_ascii_lowercase().as_str() {
        "txt" => Some("text/plain; charset=utf-8"),
        "html" => Some("text/html; charset=utf-8"),
        "json" => Some("application/json; charset=utf-8"),
        "css" => Some("text/css; charset=utf-8"),
        "js" | "mjs" => Some("text/javascript; charset=utf-8"),
        "svg" => Some("image/svg+xml"),
        "png" => Some("image/png"),
        "jpg" | "jpeg" => Some("image/jpeg"),
        "wasm" => Some("application/wasm"),
        _ => None,
    }
}

fn static_asset_route(
    context: &StaticFilesRouteContext<'_>,
    route_path: String,
    content_type: &str,
    bytes: &[u8],
    max_age_seconds: Option<u64>,
) -> Route {
    let headers = static_asset_headers(bytes, max_age_seconds);
    let byte_array = bytes
        .iter()
        .map(u8::to_string)
        .collect::<Vec<_>>()
        .join(",");
    let handler_source = format!(
        "function() {{ return Results.bytes(new Uint8Array([{byte_array}]), {{ contentType: {}, headers: {} }}); }}",
        json_string(content_type),
        headers,
    );
    let handler_source =
        wrap_handler_with_framework_pipeline(&handler_source, context.middleware, context.cors);
    Route {
        method: "GET",
        kind: "http",
        websocket: None,
        realtime: None,
        framework_path: None,
        pattern: route_path,
        name: None,
        tags: vec!["static".to_string()],
        summary: None,
        description: None,
        deprecated: None,
        consumes: Vec::new(),
        produces: vec![content_type.to_string()],
        headers: Vec::new(),
        query_schema: None,
        params_schema: None,
        openapi_override: None,
        output_cache: None,
        cache_headers: None,
        docs: None,
        health: None,
        middleware: route_middleware_metadata(context.middleware),
        auth: None,
        cors: context.cors.map(cors_policy_metadata),
        cors_preflight: false,
        span: context.span,
        source_path: context.path.to_path_buf(),
        source_name: context.source_name.to_string(),
        source: context.source.to_string(),
        module: None,
        handler: Handler {
            source: source_slice(context.source, context.span)
                .unwrap_or_else(|| "app.useStaticFiles()".to_string()),
            emitted_source: handler_source,
            span: context.span,
            requires_results_import: false,
            is_async: !context.middleware.is_empty() || context.cors.is_some(),
            runtime_deferred: false,
            source_name: context.source_name.to_string(),
            source_text: context.source.to_string(),
            source_map_line_offset: 0,
            source_map_column_offset: 0,
            bindings: Vec::new(),
            response: Some(ResponseMetadata {
                helper: "bytes".to_string(),
                status: 200,
                kind: "bytes".to_string(),
                body_schema: None,
                native_body: None,
                source_name: Some(context.source_name.to_string()),
                source_text: Some(context.source.to_string()),
                span: Some(context.span),
                partial: false,
            }),
            responses: Vec::new(),
            effects: Vec::new(),
            schema_metadata_conflict: false,
        },
    }
}

fn static_asset_headers(bytes: &[u8], max_age_seconds: Option<u64>) -> String {
    let mut headers = serde_json::Map::new();
    headers.insert(
        "ETag".to_string(),
        json!(format!("\"{}\"", sha256_bytes_hex(bytes))),
    );
    if let Some(max_age_seconds) = max_age_seconds {
        headers.insert(
            "Cache-Control".to_string(),
            json!(format!("public, max-age={max_age_seconds}")),
        );
    }
    Value::Object(headers).to_string()
}

fn app_use_middleware_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(false);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(false);
    };
    if property != "use" {
        return Ok(false);
    }
    let target_is_app = state.app_vars.contains(receiver);
    let target_is_group = state.group_vars.contains_key(receiver);
    if !target_is_app && !target_is_group {
        return Ok(false);
    }
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
            "app.use/group.use accepts exactly one static middleware function in compiler input",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let Some(argument) = call.arguments.first() else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
            "app.use/group.use accepts exactly one static middleware function in compiler input",
        )
        .with_path(path)
        .with_span(call.span));
    };
    let middleware = middleware_from_argument(path, source, source_name, argument, state)?;
    if target_is_app {
        state.middleware.push(middleware);
    } else if let Some(group) = state.group_vars.get_mut(receiver) {
        group.middleware.push(middleware);
    }
    state.next_middleware_sequence += 1;
    Ok(true)
}

fn app_use_auth_provider_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(false);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(false);
    };
    if property != "use" || !state.app_vars.contains(receiver) {
        return Ok(false);
    }
    let Some(argument) = call.arguments.first() else {
        return Ok(false);
    };
    let Some((kind, options)) = auth_provider_argument(argument) else {
        return Ok(false);
    };
    if !state.auth_imported {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            "Auth providers require importing Auth from \"sloppy\"",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    }
    match kind {
        "jwtBearer" => {
            let issuer = object_string_property_value(options, "issuer").map(str::to_string);
            let audience = object_string_property_value(options, "audience").map(str::to_string);
            let clock_skew_seconds = object_integer_property_value(options, "clockSkewSeconds")
                .or_else(|| object_integer_property_value(options, "clockSkew"))
                .unwrap_or(0);
            let secret_config_key = object_property_expression(options, "secret")
                .and_then(config_required_key_from_expression)
                .ok_or_else(|| {
                    Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "Auth.jwtBearer requires secret: Config.required(\"...\") for compiler extraction",
                    )
                    .with_path(path)
                    .with_span(call.span)
                })?
                .to_string();
            state.config_reads.push(config_required_read(
                &secret_config_key,
                "secret",
                true,
                source,
                source_name,
                call.span,
            ));
            state.auth.schemes.push(AuthSchemeMetadata::JwtBearer {
                name: "bearerAuth".to_string(),
                issuer,
                audience,
                clock_skew_seconds,
                secret_config_key: Some(secret_config_key),
            });
        }
        "apiKey" => {
            let header = object_string_property_value(options, "header")
                .unwrap_or("x-api-key")
                .to_string();
            let config_keys = if let Some(key) = object_string_property_value(options, "configKey")
            {
                vec![key.to_string()]
            } else {
                config_required_keys_from_source(&source_slice(source, call.span).unwrap_or_default())
                    .ok_or_else(|| {
                        Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_AUTH",
                            "Auth.apiKey requires a literal Config.required(\"...\") validator reference or configKey for compiler extraction",
                        )
                        .with_path(path)
                        .with_span(call.span)
                    })?
            };
            let [config_key] = config_keys.as_slice() else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_AUTH",
                    "Auth.apiKey requires exactly one config key for compiler extraction",
                )
                .with_path(path)
                .with_span(call.span));
            };
            state.config_reads.push(config_required_read(
                config_key,
                "secret",
                true,
                source,
                source_name,
                call.span,
            ));
            state.auth.schemes.push(AuthSchemeMetadata::ApiKey {
                name: "apiKeyAuth".to_string(),
                header,
                config_key: Some(config_key.clone()),
            });
        }
        "cookieSession" => {
            let cookie = object_string_property_value(options, "name")
                .unwrap_or("sloppy.session")
                .to_string();
            let secure = object_bool_property_value(options, "secure").unwrap_or(true);
            let http_only = object_bool_property_value(options, "httpOnly").unwrap_or(true);
            let same_site = object_string_property_value(options, "sameSite")
                .unwrap_or("lax")
                .to_string();
            let cookie_path = object_string_property_value(options, "path")
                .unwrap_or("/")
                .to_string();
            let max_age_seconds = object_integer_property_value(options, "maxAgeSeconds")
                .or_else(|| object_integer_property_value(options, "maxAge"));
            let store = if let Some(store_expr) = object_property_expression(options, "store") {
                if cookie_session_store_kind(store_expr) == Some("memory") {
                    Some("memory".to_string())
                } else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "Auth.cookieSession compiler extraction supports Auth.sessionStore.memory() for generated session stores; DB session stores remain stdlib runtime objects",
                    )
                    .with_path(path)
                    .with_span(store_expr.span()));
                }
            } else {
                None
            };
            let idle_timeout_ms = object_integer_property_value(options, "idleTimeoutMs")
                .or_else(|| object_integer_property_value(options, "idleTimeout"));
            let absolute_timeout_ms = object_integer_property_value(options, "absoluteTimeoutMs")
                .or_else(|| object_integer_property_value(options, "absoluteTimeout"));
            let rotation = object_bool_property_value(options, "rotation")
                .or_else(|| object_bool_property_value(options, "rotate"))
                .unwrap_or(false);
            let secret_config_key = object_property_expression(options, "secret")
                .and_then(config_required_key_from_expression)
                .ok_or_else(|| {
                    Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "Auth.cookieSession requires secret: Config.required(\"...\") for compiler extraction",
                    )
                    .with_path(path)
                    .with_span(call.span)
                })?
                .to_string();
            state.config_reads.push(config_required_read(
                &secret_config_key,
                "secret",
                true,
                source,
                source_name,
                call.span,
            ));
            state.auth.schemes.push(AuthSchemeMetadata::CookieSession {
                name: "cookieSessionAuth".to_string(),
                cookie,
                secure,
                http_only,
                same_site,
                path: cookie_path,
                max_age_seconds,
                store,
                idle_timeout_ms,
                absolute_timeout_ms,
                rotation,
                secret_config_key: Some(secret_config_key),
            });
        }
        _ => {}
    }
    Ok(true)
}

fn app_auth_policy_call(
    path: &Path,
    source: &str,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(false);
    };
    let Expression::StaticMemberExpression(method) = &call.callee else {
        return Ok(false);
    };
    if method.property.name.as_str() != "addPolicy" {
        return Ok(false);
    }
    let Expression::StaticMemberExpression(auth_member) = &method.object else {
        return Ok(false);
    };
    if auth_member.property.name.as_str() != "auth" {
        return Ok(false);
    }
    let Expression::Identifier(app) = &auth_member.object else {
        return Ok(false);
    };
    if !state.app_vars.contains(app.name.as_str()) {
        return Ok(false);
    }
    let Some(name) = call.arguments.first().and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            "app.auth.addPolicy requires a literal policy name",
        )
        .with_path(path)
        .with_span(call.span));
    };
    let Some(policy_argument) = call.arguments.get(1) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            "app.auth.addPolicy requires an inline policy function",
        )
        .with_path(path)
        .with_span(call.span));
    };
    let policy_span = argument_span(policy_argument).unwrap_or(call.span);
    let policy_source = source_slice(source, policy_span).ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            "app.auth.addPolicy policy source could not be extracted",
        )
        .with_path(path)
        .with_span(policy_span)
    })?;
    let policy_source = if policy_source.trim_start().starts_with("Auth.policy") {
        policy_source.replacen("Auth.policy", "__sloppy_auth_policy", 1)
    } else {
        validate_auth_policy_source(path, &policy_source, policy_span)?;
        policy_source
    };
    if !state.auth.policies.iter().any(|policy| policy.name == name) {
        state.auth.policies.push(AuthPolicyMetadata {
            name: name.to_string(),
            source: Some(policy_source),
        });
    }
    Ok(true)
}

fn route_group_auth_call(
    path: &Path,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(false);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(false);
    };
    if !state.group_vars.contains_key(receiver) {
        return Ok(false);
    }
    let requirement = match property {
        "requireAuth" | "requiresAuth" => auth_requirement_from_call(call)
            .map_err(|diagnostic| diagnostic.with_path(path).with_span(call.span))?,
        "allowAnonymous" => anonymous_auth_requirement(),
        _ => return Ok(false),
    };
    if let Some(group) = state.group_vars.get_mut(receiver) {
        group.auth = Some(requirement);
    }
    Ok(true)
}

fn auth_provider_argument<'a>(
    argument: &'a Argument<'a>,
) -> Option<(&'a str, &'a oxc_ast::ast::ObjectExpression<'a>)> {
    let Argument::CallExpression(call) = argument else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if receiver != "Auth"
        || !matches!(property, "jwtBearer" | "apiKey" | "cookieSession")
        || call.arguments.len() != 1
    {
        return None;
    }
    let options = object_argument(call.arguments.first()?)?;
    Some((property, options))
}

fn object_property_expression<'a>(
    object: &'a oxc_ast::ast::ObjectExpression<'a>,
    name: &str,
) -> Option<&'a Expression<'a>> {
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            continue;
        };
        if property.computed || property_key_name(&property.key) != Some(name) {
            continue;
        }
        return Some(&property.value);
    }
    None
}

fn config_required_key_from_expression<'a>(expression: &'a Expression<'a>) -> Option<&'a str> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if receiver != "Config" || property != "required" || call.arguments.len() != 1 {
        return None;
    }
    call.arguments.first().and_then(string_argument)
}

fn cookie_session_store_kind(expression: &Expression<'_>) -> Option<&'static str> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let chain = static_member_chain(&call.callee)?;
    if chain == ["Auth", "sessionStore", "memory"] {
        return Some("memory");
    }
    None
}

fn config_required_keys_from_source(source: &str) -> Option<Vec<String>> {
    let mut keys = Vec::new();
    let mut rest = source;
    while let Some(index) = rest.find("Config.required") {
        rest = &rest[index + "Config.required".len()..];
        let open = rest.find('(')?;
        rest = &rest[open + 1..];
        let trimmed = rest.trim_start();
        let quote = trimmed
            .chars()
            .next()
            .filter(|ch| *ch == '"' || *ch == '\'')?;
        let key_start = quote.len_utf8();
        let end = trimmed[key_start..].find(quote)?;
        keys.push(trimmed[key_start..key_start + end].to_string());
        rest = &trimmed[key_start + end + quote.len_utf8()..];
    }
    Some(keys)
}

fn validate_auth_policy_source(path: &Path, source: &str, span: Span) -> Result<(), Diagnostic> {
    let params = auth_policy_parameters(source).ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            "app.auth.addPolicy compiler extraction supports inline functions with simple parameters",
        )
        .with_path(path)
        .with_span(span)
    })?;
    if let Some(identifier) = captured_auth_policy_identifier(source, &params) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            format!(
                "app.auth.addPolicy inline policy cannot capture '{identifier}' in compiler output"
            ),
        )
        .with_path(path)
        .with_span(span));
    }
    Ok(())
}

fn auth_policy_parameters(source: &str) -> Option<BTreeSet<String>> {
    let mut trimmed = source.trim_start();
    if let Some(rest) = trimmed.strip_prefix("async") {
        if rest.starts_with(char::is_whitespace) {
            trimmed = rest.trim_start();
        }
    }
    if let Some(rest) = trimmed.strip_prefix("function") {
        let open = rest.find('(')?;
        let close = rest[open + 1..].find(')')? + open + 1;
        return auth_policy_parameter_list(&rest[open + 1..close]);
    }
    if let Some(rest) = trimmed.strip_prefix('(') {
        let close = rest.find(')')?;
        let after = rest[close + 1..].trim_start();
        if !after.starts_with("=>") {
            return None;
        }
        return auth_policy_parameter_list(&rest[..close]);
    }
    let arrow = trimmed.find("=>")?;
    let parameter = trimmed[..arrow].trim();
    if js_identifier(parameter) {
        let mut params = BTreeSet::new();
        params.insert(parameter.to_string());
        return Some(params);
    }
    None
}

fn auth_policy_parameter_list(source: &str) -> Option<BTreeSet<String>> {
    let mut params = BTreeSet::new();
    let trimmed = source.trim();
    if trimmed.is_empty() {
        return Some(params);
    }
    for part in trimmed.split(',') {
        let name = part.trim();
        if !js_identifier(name) {
            return None;
        }
        params.insert(name.to_string());
    }
    Some(params)
}

fn captured_auth_policy_identifier(source: &str, params: &BTreeSet<String>) -> Option<String> {
    let bytes = source.as_bytes();
    let mut index = 0usize;
    while index < bytes.len() {
        match bytes[index] {
            b'\'' | b'"' | b'`' => {
                index = skip_js_quoted_literal(bytes, index);
                continue;
            }
            b'/' if index + 1 < bytes.len() && bytes[index + 1] == b'/' => {
                index = skip_js_line_comment(bytes, index + 2);
                continue;
            }
            b'/' if index + 1 < bytes.len() && bytes[index + 1] == b'*' => {
                index = skip_js_block_comment(bytes, index + 2);
                continue;
            }
            _ => {}
        }
        if js_identifier_start(bytes[index]) {
            let start = index;
            index += 1;
            while index < bytes.len() && js_identifier_part(bytes[index]) {
                index += 1;
            }
            let identifier = &source[start..index];
            if auth_policy_identifier_allowed(source, start, index, identifier, params) {
                continue;
            }
            return Some(identifier.to_string());
        }
        index += 1;
    }
    None
}

fn auth_policy_identifier_allowed(
    source: &str,
    start: usize,
    end: usize,
    identifier: &str,
    params: &BTreeSet<String>,
) -> bool {
    if params.contains(identifier)
        || auth_policy_keyword(identifier)
        || auth_policy_global(identifier)
    {
        return true;
    }
    if previous_non_ws_byte(source.as_bytes(), start) == Some(b'.') {
        return true;
    }
    if next_non_ws_byte(source.as_bytes(), end) == Some(b':') {
        return true;
    }
    false
}

fn auth_policy_keyword(identifier: &str) -> bool {
    matches!(
        identifier,
        "async"
            | "await"
            | "break"
            | "case"
            | "catch"
            | "const"
            | "continue"
            | "default"
            | "do"
            | "else"
            | "false"
            | "finally"
            | "for"
            | "function"
            | "if"
            | "in"
            | "instanceof"
            | "let"
            | "new"
            | "null"
            | "return"
            | "switch"
            | "throw"
            | "true"
            | "try"
            | "typeof"
            | "undefined"
            | "var"
            | "void"
            | "while"
    )
}

fn auth_policy_global(identifier: &str) -> bool {
    matches!(
        identifier,
        "Array"
            | "Boolean"
            | "Date"
            | "JSON"
            | "Map"
            | "Math"
            | "Number"
            | "Object"
            | "RegExp"
            | "Set"
            | "String"
    )
}

fn js_identifier(source: &str) -> bool {
    let mut bytes = source.bytes();
    let Some(first) = bytes.next() else {
        return false;
    };
    js_identifier_start(first) && bytes.all(js_identifier_part)
}

fn js_identifier_start(byte: u8) -> bool {
    byte.is_ascii_alphabetic() || byte == b'_' || byte == b'$'
}

fn js_identifier_part(byte: u8) -> bool {
    js_identifier_start(byte) || byte.is_ascii_digit()
}

fn previous_non_ws_byte(bytes: &[u8], start: usize) -> Option<u8> {
    let mut index = start;
    while index > 0 {
        index -= 1;
        if !bytes[index].is_ascii_whitespace() {
            return Some(bytes[index]);
        }
    }
    None
}

fn next_non_ws_byte(bytes: &[u8], start: usize) -> Option<u8> {
    let mut index = start;
    while index < bytes.len() {
        if !bytes[index].is_ascii_whitespace() {
            return Some(bytes[index]);
        }
        index += 1;
    }
    None
}

fn config_required_read(
    key: &str,
    value_type: &str,
    sensitive: bool,
    source: &str,
    source_name: &str,
    span: Span,
) -> ConfigReadMetadata {
    ConfigReadMetadata {
        key: key.to_string(),
        value_type: value_type.to_string(),
        has_default: false,
        default_value: None,
        required: true,
        sensitive: sensitive || config_key_is_sensitive(key),
        source_name: source_name.to_string(),
        source: source.to_string(),
        span,
    }
}

fn middleware_from_argument(
    path: &Path,
    source: &str,
    source_name: &str,
    argument: &Argument<'_>,
    state: &mut AppState,
) -> Result<FrameworkMiddleware, Diagnostic> {
    match argument {
        Argument::ArrowFunctionExpression(function) => {
            if arrow_requires_results_import(function)
                && !state.results_imported
                && state.results_required_span.is_none()
            {
                state.results_required_span = Some(function.span);
            }
            validate_middleware_arrow(path, function, state)?;
            let source_text = source_slice(source, function.span).ok_or_else(|| {
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
                    "middleware source could not be extracted",
                )
                .with_path(path)
                .with_span(function.span)
            })?;
            Ok(FrameworkMiddleware {
                kind: FrameworkMiddlewareKind::User,
                source: source_text,
                sequence: state.next_middleware_sequence,
                source_name: source_name.to_string(),
                source_text: source.to_string(),
                span: function.span,
            })
        }
        Argument::FunctionExpression(function) => {
            if function_requires_results_import(function)
                && !state.results_imported
                && state.results_required_span.is_none()
            {
                state.results_required_span = Some(function.span);
            }
            validate_middleware_function(path, function, state)?;
            let source_text = source_slice(source, function.span).ok_or_else(|| {
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
                    "middleware source could not be extracted",
                )
                .with_path(path)
                .with_span(function.span)
            })?;
            Ok(FrameworkMiddleware {
                kind: FrameworkMiddlewareKind::User,
                source: source_text,
                sequence: state.next_middleware_sequence,
                source_name: source_name.to_string(),
                source_text: source.to_string(),
                span: function.span,
            })
        }
        Argument::Identifier(identifier) => {
            let name = identifier.name.as_str();
            if !state.helper_sources.contains_key(name) {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
                    "named middleware must reference a top-level function helper",
                )
                .with_path(path)
                .with_span(identifier.span));
            }
            if state.helper_effects.get(name).is_some_and(|summary| {
                !summary.effects.is_empty()
                    || !summary.provider_bindings.is_empty()
                    || summary.unknown_provider_usage
            }) {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
                    "named middleware cannot capture provider handles in compiler input",
                )
                .with_path(path)
                .with_span(identifier.span));
            }
            Ok(FrameworkMiddleware {
                kind: FrameworkMiddlewareKind::User,
                source: name.to_string(),
                sequence: state.next_middleware_sequence,
                source_name: source_name.to_string(),
                source_text: source.to_string(),
                span: identifier.span,
            })
        }
        _ => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
            "middleware must be an inline function or a top-level function identifier",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or_else(|| Span::new(0, 0)))),
    }
}

fn validate_middleware_arrow(
    path: &Path,
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    state: &AppState,
) -> Result<(), Diagnostic> {
    if function.params.rest.is_some()
        || function.params.items.len() > 2
        || arrow_has_typescript_syntax(function)
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
            "middleware functions may declare at most ctx and next parameters without TypeScript syntax",
        )
        .with_path(path)
        .with_span(function.span));
    }
    reject_middleware_captures(
        path,
        function.span,
        middleware_arrow_free_identifiers(function),
        state,
    )
}

fn validate_middleware_function(
    path: &Path,
    function: &oxc_ast::ast::Function<'_>,
    state: &AppState,
) -> Result<(), Diagnostic> {
    if function.generator
        || function.body.is_none()
        || function.params.rest.is_some()
        || function.params.items.len() > 2
        || function_has_typescript_syntax(function)
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
            "middleware functions may declare at most ctx and next parameters without TypeScript syntax",
        )
        .with_path(path)
        .with_span(function.span));
    }
    reject_middleware_captures(
        path,
        function.span,
        middleware_function_free_identifiers(function),
        state,
    )
}

fn reject_middleware_captures(
    path: &Path,
    span: Span,
    mut free_identifiers: BTreeSet<String>,
    state: &AppState,
) -> Result<(), Diagnostic> {
    free_identifiers.remove("Results");
    free_identifiers.remove("Promise");
    for name in state.helper_sources.keys() {
        free_identifiers.remove(name);
    }
    if free_identifiers.is_empty() {
        return Ok(());
    }
    let identifier = free_identifiers
        .iter()
        .next()
        .map(String::as_str)
        .unwrap_or("");
    Err(Diagnostic::new(
        "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
        format!("middleware captures unsupported identifier '{identifier}'"),
    )
    .with_path(path)
    .with_span(span)
    .with_hint("Use middleware that depends on ctx, next, Results, local values, or emitted top-level helpers."))
}

fn middleware_arrow_free_identifiers(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
) -> BTreeSet<String> {
    let mut scope = BTreeSet::new();
    collect_formal_parameter_bindings(&function.params, &mut scope);
    collect_function_body_bindings(&function.body.statements, &mut scope);
    let mut free = BTreeSet::new();
    collect_statement_list_identifier_references(&function.body.statements, &scope, &mut free);
    free
}

fn middleware_function_free_identifiers(function: &oxc_ast::ast::Function<'_>) -> BTreeSet<String> {
    let mut scope = BTreeSet::new();
    collect_formal_parameter_bindings(&function.params, &mut scope);
    if let Some(identifier) = &function.id {
        scope.insert(identifier.name.as_str().to_string());
    }
    let Some(body) = &function.body else {
        return BTreeSet::new();
    };
    collect_function_body_bindings(&body.statements, &mut scope);
    let mut free = BTreeSet::new();
    collect_statement_list_identifier_references(&body.statements, &scope, &mut free);
    free
}

fn app_use_cors_call(
    path: &Path,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(false);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(false);
    };
    if property != "useCors" || !state.app_vars.contains(receiver) {
        return Ok(false);
    }
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CORS",
            "app.useCors requires one literal policy for compiler extraction",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let Some(argument) = call.arguments.first() else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CORS",
            "app.useCors requires one literal policy for compiler extraction",
        )
        .with_path(path)
        .with_span(call.span));
    };
    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CORS",
            "dynamic CORS policies are not supported by compiler extraction",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };
    state.cors_policy = Some(cors_policy_from_object(path, object)?);
    Ok(true)
}

fn cors_policy_from_object(
    path: &Path,
    object: &oxc_ast::ast::ObjectExpression<'_>,
) -> Result<CorsPolicy, Diagnostic> {
    let mut origins: Option<Vec<String>> = None;
    let mut methods: Option<Vec<String>> = None;
    let mut headers: Option<Vec<String>> = None;
    let mut exposed_headers: Option<Vec<String>> = None;
    let mut credentials = false;
    let mut max_age_seconds: Option<u64> = None;

    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                "CORS policies must use literal properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        if property.kind != PropertyKind::Init || property.method || property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                "CORS policies must use simple literal properties",
            )
            .with_path(path)
            .with_span(property.span));
        }
        let Some(name) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                "CORS policy option names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        match name {
            "origins" | "origin" => {
                origins = Some(cors_string_list(path, &property.value, "origins", false)?);
            }
            "methods" => {
                methods = Some(cors_methods(path, &property.value)?);
            }
            "headers" | "allowHeaders" => {
                headers = Some(cors_string_list(path, &property.value, "headers", true)?);
            }
            "exposedHeaders" | "exposeHeaders" => {
                exposed_headers = Some(cors_string_list(
                    path,
                    &property.value,
                    "exposedHeaders",
                    false,
                )?);
            }
            "credentials" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_CORS",
                        "CORS credentials must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                credentials = value.value;
            }
            "maxAgeSeconds" | "maxAge" => {
                let Expression::NumericLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_CORS",
                        "CORS maxAgeSeconds must be a non-negative integer literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                if value.value < 0.0 || value.value.fract() != 0.0 {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_CORS",
                        "CORS maxAgeSeconds must be a non-negative integer literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                }
                max_age_seconds = Some(value.value as u64);
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_CORS",
                    format!("unsupported CORS policy option '{name}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }

    let origins = origins.unwrap_or_default();
    if origins.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CORS",
            "CORS origins must include at least one origin or '*'",
        )
        .with_path(path)
        .with_span(object.span));
    }
    let allow_any_origin = origins.iter().any(|origin| origin == "*");
    if allow_any_origin && origins.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CORS",
            "CORS '*' origin cannot be combined with other origins",
        )
        .with_path(path)
        .with_span(object.span));
    }
    if allow_any_origin && credentials {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CORS",
            "CORS credentials require explicit origins",
        )
        .with_path(path)
        .with_span(object.span));
    }
    Ok(CorsPolicy {
        origins,
        methods: methods.unwrap_or_default(),
        headers: headers.unwrap_or_default(),
        exposed_headers: exposed_headers.unwrap_or_default(),
        credentials,
        max_age_seconds,
    })
}

fn cors_string_list(
    path: &Path,
    expression: &Expression<'_>,
    subject: &str,
    lower: bool,
) -> Result<Vec<String>, Diagnostic> {
    let values = match expression {
        Expression::StringLiteral(value) => vec![value.value.as_str().to_string()],
        Expression::ArrayExpression(array) => {
            let mut values = Vec::new();
            for element in &array.elements {
                let ArrayExpressionElement::StringLiteral(value) = element else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_CORS",
                        format!("CORS {subject} entries must be string literals"),
                    )
                    .with_path(path)
                    .with_span(array.span));
                };
                values.push(value.value.as_str().to_string());
            }
            values
        }
        _ => {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                format!("CORS {subject} must be a string literal or string literal array"),
            )
            .with_path(path)
            .with_span(expression.span()));
        }
    };
    let mut normalized = Vec::new();
    for value in values {
        if value.is_empty() || value.bytes().any(|byte| byte < 0x20 || byte == 0x7f) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                format!(
                    "CORS {subject} entries must be non-empty strings without control characters"
                ),
            )
            .with_path(path)
            .with_span(expression.span()));
        }
        let value = if lower { value.to_lowercase() } else { value };
        if matches!(subject, "headers" | "exposedHeaders") && !compiler_header_token(&value) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                format!("CORS {subject} entries must be HTTP token strings"),
            )
            .with_path(path)
            .with_span(expression.span()));
        }
        if !normalized.contains(&value) {
            normalized.push(value);
        }
    }
    Ok(normalized)
}

fn cors_methods(path: &Path, expression: &Expression<'_>) -> Result<Vec<String>, Diagnostic> {
    let methods = cors_string_list(path, expression, "methods", false)?
        .into_iter()
        .map(|method| method.to_uppercase())
        .collect::<Vec<_>>();
    for method in &methods {
        if !matches!(
            method.as_str(),
            "GET" | "POST" | "PUT" | "PATCH" | "DELETE" | "HEAD" | "OPTIONS"
        ) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                "CORS methods must be supported HTTP methods",
            )
            .with_path(path)
            .with_span(expression.span()));
        }
    }
    Ok(methods.into_iter().fold(Vec::new(), |mut acc, method| {
        if !acc.contains(&method) {
            acc.push(method);
        }
        acc
    }))
}

fn compiler_header_name_allowed(name: &str) -> bool {
    compiler_header_token(name)
        && !matches!(
            name.to_ascii_lowercase().as_str(),
            "connection" | "content-type" | "content-length" | "transfer-encoding" | "keep-alive"
        )
}

fn compiler_header_token(name: &str) -> bool {
    !name.is_empty()
        && name.bytes().all(|byte| {
            byte.is_ascii_alphanumeric()
                || matches!(
                    byte,
                    b'!' | b'#'
                        | b'$'
                        | b'%'
                        | b'&'
                        | b'\''
                        | b'*'
                        | b'+'
                        | b'-'
                        | b'.'
                        | b'^'
                        | b'_'
                        | b'`'
                        | b'|'
                        | b'~'
                )
        })
}

fn app_map_controller_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<Option<Vec<Route>>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if !matches!(property, "mapController" | "controller") || !state.app_vars.contains(receiver) {
        return Ok(None);
    }
    if !matches!(call.arguments.len(), 2 | 3) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "app.mapController requires a literal prefix, controller class, and optional mapper callback",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let Some(prefix) = call.arguments.first().and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller prefix must be a string literal",
        )
        .with_path(path)
        .with_span(
            call.arguments
                .first()
                .and_then(argument_span)
                .unwrap_or(call.span),
        ));
    };
    let Some(controller_name) = call.arguments.get(1).and_then(argument_identifier) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller mapping requires a static controller class identifier",
        )
        .with_path(path)
        .with_span(
            call.arguments
                .get(1)
                .and_then(argument_span)
                .unwrap_or(call.span),
        ));
    };
    let Some(controller) = state.controllers.get(controller_name).cloned() else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller mapping requires a top-level controller class",
        )
        .with_path(path)
        .with_span(
            call.arguments
                .get(1)
                .and_then(argument_span)
                .unwrap_or(call.span),
        ));
    };
    let Some(configure_argument) = call.arguments.get(2) else {
        return Ok(Some(Vec::new()));
    };
    let (mapper_name, statements, configure_span) =
        controller_mapper_callback(path, configure_argument)?;
    let mut routes = Vec::new();
    for statement in statements {
        let Statement::ExpressionStatement(statement) = statement else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                "controller mapper callback supports literal mapper route calls only",
            )
            .with_path(path)
            .with_span(statement.span()));
        };
        let (route_expr, fluent_metadata) = route_metadata_chain(&statement.expression)
            .map_err(|diagnostic| diagnostic.with_path(path))?;
        let Some((method, pattern, action, route_metadata)) =
            controller_route_parts(route_expr, mapper_name)?
        else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                "controller mapper callback supports mapper.get/post/put/patch/delete calls only",
            )
            .with_path(path)
            .with_span(statement.span));
        };
        let Some(controller_method) = controller.methods.get(action).cloned() else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                format!("controller action '{action}' must name a prototype method"),
            )
            .with_path(path)
            .with_span(route_expr.span()));
        };
        let full_pattern = join_route_patterns(prefix, pattern);
        let normalized_pattern = normalize_framework_route_pattern(&full_pattern);
        if !route_pattern_supported(&normalized_pattern) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_PATTERN",
                "controller route pattern is outside the Plan v1 alpha route syntax",
            )
            .with_path(path)
            .with_span(statement.span)
            .with_hint("Use '/', static segments, {name}, {name:str}, or {name:int}."));
        }
        let contract_metadata = merged_route_metadata(&route_metadata, &fluent_metadata);
        let tags = contract_metadata.tags.clone();
        let middleware = route_middleware_metadata(&state.middleware);
        let cors = state.cors_policy.clone();
        let handler_source = source_slice(&controller.source_text, controller_method.span)
            .unwrap_or_else(|| format!("{controller_name}.{action}"));
        let emitted_source = controller_handler_source(controller_name, action);
        let mut handler = Handler {
            source: handler_source.clone(),
            emitted_source,
            span: controller_method.span,
            requires_results_import: controller_method.requires_results_import,
            is_async: true,
            runtime_deferred: false,
            source_name: controller.source_name.clone(),
            source_text: controller.source_text.clone(),
            source_map_line_offset: 0,
            source_map_column_offset: 0,
            bindings: Vec::new(),
            response: None,
            responses: Vec::new(),
            effects: Vec::new(),
            schema_metadata_conflict: false,
        };
        apply_route_schema_metadata(
            path,
            statement.span,
            &state.schema_names,
            &fluent_metadata,
            &mut handler,
        )?;
        let auth = contract_metadata.auth.clone();
        if let Some(requirement) = &auth {
            handler.emitted_source = wrap_handler_with_auth(&handler.emitted_source, requirement);
            handler.is_async = true;
        }
        handler.emitted_source = wrap_handler_with_framework_pipeline(
            &handler.emitted_source,
            &state.middleware,
            cors.as_ref(),
        );
        handler.is_async = true;
        routes.push(Route {
            method,
            kind: "http",
            websocket: None,
            realtime: None,
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
            docs: None,
            health: None,
            middleware,
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
    }
    if routes.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller mapper callback must register at least one route",
        )
        .with_path(path)
        .with_span(configure_span));
    }
    Ok(Some(routes))
}

fn controller_mapper_callback<'a>(
    path: &Path,
    argument: &'a Argument<'a>,
) -> Result<(&'a str, &'a [Statement<'a>], Span), Diagnostic> {
    match argument {
        Argument::ArrowFunctionExpression(function) => {
            if function.params.items.len() != 1 || function.params.rest.is_some() {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                    "controller mapper callback must declare one mapper parameter",
                )
                .with_path(path)
                .with_span(function.span));
            }
            let Some(mapper) = function
                .params
                .items
                .first()
                .and_then(|parameter| binding_identifier(&parameter.pattern))
            else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                    "controller mapper parameter must be a simple identifier",
                )
                .with_path(path)
                .with_span(function.span));
            };
            Ok((mapper, &function.body.statements, function.span))
        }
        Argument::FunctionExpression(function) => {
            if function.params.items.len() != 1 || function.params.rest.is_some() {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                    "controller mapper callback must declare one mapper parameter",
                )
                .with_path(path)
                .with_span(function.span));
            }
            let Some(mapper) = function
                .params
                .items
                .first()
                .and_then(|parameter| binding_identifier(&parameter.pattern))
            else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                    "controller mapper parameter must be a simple identifier",
                )
                .with_path(path)
                .with_span(function.span));
            };
            let Some(body) = &function.body else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                    "controller mapper callback must have a body",
                )
                .with_path(path)
                .with_span(function.span));
            };
            Ok((mapper, &body.statements, function.span))
        }
        _ => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller mapper must be an inline function",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or_else(|| Span::new(0, 0)))),
    }
}

fn controller_route_parts<'a>(
    expression: &'a Expression<'a>,
    mapper_name: &str,
) -> Result<Option<(&'static str, &'a str, &'a str, RouteMetadata)>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if receiver != mapper_name {
        return Ok(None);
    }
    let method = route_method_from_property(property);
    let Some(method) = method else {
        return Ok(None);
    };
    if !matches!(call.arguments.len(), 2 | 3) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller mapper routes require pattern and action literals",
        )
        .with_span(call.span));
    }
    let Some(pattern) = call.arguments.first().and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller mapper route pattern must be a string literal",
        )
        .with_span(call.span));
    };
    let Some(action) = call.arguments.get(1).and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller mapper action must be a string literal",
        )
        .with_span(call.span));
    };
    let metadata = if call.arguments.len() == 3 {
        route_metadata_from_options_argument(&call.arguments[2])?
    } else {
        RouteMetadata::default()
    };
    Ok(Some((method, pattern, action, metadata)))
}

fn controller_handler_source(controller_name: &str, action: &str) -> String {
    let action = serde_json::to_string(action).unwrap_or_else(|_| "\"\"".to_string());
    format!(
        "async function(ctx) {{ const __sloppy_scope = __sloppy_framework_services.createScope(ctx); ctx.services = __sloppy_scope; try {{ const __sloppy_tokens = {controller_name}.inject ?? {controller_name}.dependencies ?? []; if (!Array.isArray(__sloppy_tokens)) {{ throw new TypeError(\"Sloppy controller inject metadata must be an array when provided.\"); }} const __sloppy_controller = new {controller_name}(...__sloppy_tokens.map((token) => __sloppy_scope.get(token))); return await __sloppy_controller[{action}](ctx); }} finally {{ await __sloppy_scope.dispose(); }} }}"
    )
}

fn route_middleware_metadata(middleware: &[FrameworkMiddleware]) -> Vec<RouteMiddlewareMetadata> {
    middleware
        .iter()
        .map(|entry| RouteMiddlewareMetadata {
            kind: match &entry.kind {
                FrameworkMiddlewareKind::User => "user".to_string(),
                FrameworkMiddlewareKind::RequestId => "requestId".to_string(),
                FrameworkMiddlewareKind::RequestLogging => "requestLogging".to_string(),
            },
            source: entry.source.clone(),
            sequence: entry.sequence,
            source_name: entry.source_name.clone(),
            source_text: entry.source_text.clone(),
            span: entry.span,
        })
        .collect()
}

fn cors_policy_metadata(policy: &CorsPolicy) -> CorsPolicyMetadata {
    CorsPolicyMetadata {
        origins: policy.origins.clone(),
        methods: policy.methods.clone(),
        headers: policy.headers.clone(),
        exposed_headers: policy.exposed_headers.clone(),
        credentials: policy.credentials,
        max_age_seconds: policy.max_age_seconds,
    }
}

fn cors_policy_json(policy: &CorsPolicy) -> String {
    serde_json::to_string(&json!({
        "origins": policy.origins,
        "methods": policy.methods,
        "headers": policy.headers,
        "exposedHeaders": policy.exposed_headers,
        "credentials": policy.credentials,
        "maxAgeSeconds": policy.max_age_seconds
    }))
    .unwrap_or_else(|_| "null".to_string())
}

fn auth_schemes_json(auth: &AuthMetadata) -> String {
    serde_json::to_string(
        &auth
            .schemes
            .iter()
            .map(|scheme| match scheme {
                AuthSchemeMetadata::JwtBearer {
                    name,
                    issuer,
                    audience,
                    clock_skew_seconds,
                    secret_config_key,
                } => json!({
                    "kind": "jwtBearer",
                    "name": name,
                    "issuer": issuer,
                    "audience": audience,
                    "clockSkewSeconds": clock_skew_seconds,
                    "secretConfigKey": secret_config_key,
                    "secretEnvKey": secret_config_key.as_deref().map(config_key_to_env_name)
                }),
                AuthSchemeMetadata::ApiKey {
                    name,
                    header,
                    config_key,
                } => json!({
                    "kind": "apiKey",
                    "name": name,
                    "header": header,
                    "configKey": config_key,
                    "configEnvKey": config_key.as_deref().map(config_key_to_env_name)
                }),
                AuthSchemeMetadata::CookieSession {
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
                    secret_config_key,
                } => json!({
                    "kind": "cookieSession",
                    "name": name,
                    "cookie": cookie,
                    "secure": secure,
                    "httpOnly": http_only,
                    "sameSite": same_site,
                    "path": path,
                    "maxAgeSeconds": max_age_seconds,
                    "store": store,
                    "idleTimeoutMs": idle_timeout_ms,
                    "absoluteTimeoutMs": absolute_timeout_ms,
                    "rotation": rotation,
                    "secretConfigKey": secret_config_key,
                    "secretEnvKey": secret_config_key.as_deref().map(config_key_to_env_name)
                }),
            })
            .collect::<Vec<_>>(),
    )
    .unwrap_or_else(|_| "[]".to_string())
}

fn auth_policies_js(auth: &AuthMetadata) -> String {
    let entries = auth
        .policies
        .iter()
        .filter_map(|policy| {
            let source = policy.source.as_ref()?;
            let name = serde_json::to_string(&policy.name).ok()?;
            Some(format!("[{name}, __sloppy_auth_policy_function({source})]"))
        })
        .collect::<Vec<_>>()
        .join(", ");
    format!("new Map([{entries}])")
}

fn wrap_handler_with_framework_pipeline(
    handler_source: &str,
    middleware: &[FrameworkMiddleware],
    cors: Option<&CorsPolicy>,
) -> String {
    if middleware.is_empty() && cors.is_none() {
        return handler_source.to_string();
    }

    let terminal = format!("() => ({handler_source})(ctx)");
    let invocation = if middleware.is_empty() {
        terminal
    } else {
        let middleware_sources = middleware
            .iter()
            .map(|entry| entry.source.clone())
            .collect::<Vec<_>>()
            .join(", ");
        format!("() => __sloppy_run_middleware(ctx, [{middleware_sources}], {terminal})")
    };

    if let Some(policy) = cors {
        let policy = cors_policy_json(policy);
        format!(
            "async function(ctx) {{ return await __sloppy_finish_cors(await ({invocation})(), {policy}, ctx); }}"
        )
    } else {
        format!("async function(ctx) {{ return await ({invocation})(); }}")
    }
}

fn append_cors_preflight_routes(path: &Path, routes: &mut Vec<Route>) -> Result<(), Diagnostic> {
    let mut preflights = BTreeMap::<String, (CorsPolicyMetadata, Vec<String>, usize)>::new();
    for (index, route) in routes.iter().enumerate() {
        if route.cors_preflight {
            continue;
        }
        let Some(cors) = &route.cors else {
            continue;
        };
        let entry = preflights
            .entry(route.pattern.clone())
            .or_insert_with(|| (cors.clone(), Vec::new(), index));
        if !cors_policy_metadata_equal(&entry.0, cors) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                "multiple CORS policies for the same route pattern are not supported by compiler extraction",
            )
            .with_path(path)
            .with_span(route.span));
        }
        if !entry.1.iter().any(|method| method == route.method) {
            entry.1.push(route.method.to_string());
        }
        entry.2 = index;
    }

    for (pattern, (policy, methods, route_index)) in preflights {
        let Some(route) = routes.get(route_index).cloned() else {
            continue;
        };
        let policy_json = serde_json::to_string(&json!({
            "origins": policy.origins,
            "methods": policy.methods,
            "headers": policy.headers,
            "exposedHeaders": policy.exposed_headers,
            "credentials": policy.credentials,
            "maxAgeSeconds": policy.max_age_seconds
        }))
        .unwrap_or_else(|_| "null".to_string());
        let methods_json = serde_json::to_string(&methods).unwrap_or_else(|_| "[]".to_string());
        let terminal_source = format!(
            "function(ctx) {{ return __sloppy_cors_preflight({policy_json}, {methods_json}, ctx); }}"
        );
        let handler_source = if route.middleware.is_empty() {
            format!("async function(ctx) {{ return await ({terminal_source})(ctx); }}")
        } else {
            let middleware_sources = route
                .middleware
                .iter()
                .map(|entry| entry.source.clone())
                .collect::<Vec<_>>()
                .join(", ");
            format!(
                "async function(ctx) {{ return await __sloppy_run_middleware(ctx, [{middleware_sources}], () => ({terminal_source})(ctx)); }}"
            )
        };
        routes.push(Route {
            method: "OPTIONS",
            kind: "http",
            websocket: None,
            realtime: None,
            framework_path: None,
            pattern,
            name: None,
            tags: Vec::new(),
            summary: None,
            description: None,
            deprecated: None,
            consumes: Vec::new(),
            produces: Vec::new(),
            headers: Vec::new(),
            query_schema: None,
            params_schema: None,
            openapi_override: None,
            output_cache: None,
            cache_headers: None,
            docs: None,
            health: None,
            middleware: route.middleware.clone(),
            auth: None,
            cors: Some(policy),
            cors_preflight: true,
            span: route.span,
            source_path: route.source_path.clone(),
            source_name: route.source_name.clone(),
            source: route.source.clone(),
            module: route.module.clone(),
            handler: Handler {
                source: "app.useCors(...)".to_string(),
                emitted_source: handler_source,
                span: route.handler.span,
                requires_results_import: false,
                is_async: true,
                runtime_deferred: false,
                source_name: route.handler.source_name.clone(),
                source_text: route.handler.source_text.clone(),
                source_map_line_offset: 0,
                source_map_column_offset: 0,
                bindings: Vec::new(),
                response: Some(ResponseMetadata {
                    helper: "status".to_string(),
                    status: 204,
                    kind: "empty".to_string(),
                    body_schema: None,
                    native_body: None,
                    source_name: None,
                    source_text: None,
                    span: None,
                    partial: false,
                }),
                responses: vec![ResponseMetadata {
                    helper: "status".to_string(),
                    status: 204,
                    kind: "empty".to_string(),
                    body_schema: None,
                    native_body: None,
                    source_name: None,
                    source_text: None,
                    span: None,
                    partial: false,
                }],
                effects: Vec::new(),
                schema_metadata_conflict: false,
            },
        });
    }
    Ok(())
}

fn cors_policy_metadata_equal(left: &CorsPolicyMetadata, right: &CorsPolicyMetadata) -> bool {
    left.origins == right.origins
        && left.methods == right.methods
        && left.headers == right.headers
        && left.exposed_headers == right.exposed_headers
        && left.credentials == right.credentials
        && left.max_age_seconds == right.max_age_seconds
}

fn app_map_health_checks_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<Vec<Route>>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if property != "mapHealthChecks" || !state.app_vars.contains(receiver) {
        return Ok(None);
    }
    if call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "app.mapHealthChecks accepts at most one literal options argument",
        )
        .with_path(path)
        .with_span(call.span));
    }

    let options = health_options_from_call(path, source, call)?;
    Ok(Some(health_routes_from_options(
        path,
        source,
        source_name,
        call.span,
        &options,
        &state.middleware,
        state.cors_policy.as_ref(),
    )?))
}

fn app_health_expose_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<Vec<Route>>, Diagnostic> {
    let Expression::CallExpression(expose_call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_expression(&expose_call.callee) else {
        return Ok(None);
    };
    if property != "expose" {
        return Ok(None);
    }
    if expose_call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "app.health().expose accepts at most one literal options argument",
        )
        .with_path(path)
        .with_span(expose_call.span));
    }

    let mut options = health_expose_options_from_call(path, expose_call)?;
    let Some(checks) = health_check_chain_from_expression(path, source, receiver, state)? else {
        return Ok(None);
    };
    options.checks = checks;
    Ok(Some(health_routes_from_options(
        path,
        source,
        source_name,
        expose_call.span,
        &options,
        &state.middleware,
        state.cors_policy.as_ref(),
    )?))
}

fn app_management_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<Vec<Route>>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if property != "management" || !state.app_vars.contains(receiver) {
        return Ok(None);
    }
    if call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
            "app.management accepts at most one literal options argument",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let options = management_options_from_call(path, call)?;
    Ok(Some(management_routes_from_options(
        path,
        source,
        source_name,
        call.span,
        &options,
        &state.middleware,
        state.cors_policy.as_ref(),
    )?))
}

fn health_expose_options_from_call(
    path: &Path,
    call: &CallExpression<'_>,
) -> Result<HealthOptions, Diagnostic> {
    let mut options = HealthOptions {
        path: DEFAULT_HEALTH_PATH.to_string(),
        liveness_path: "/live".to_string(),
        readiness_path: "/ready".to_string(),
        startup_path: Some("/startup".to_string()),
        checks: Vec::new(),
    };

    let Some(argument) = call.arguments.first() else {
        health_paths_are_distinct(path, call.span, &options)?;
        return Ok(options);
    };
    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "app.health().expose options must be an object literal",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };

    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health expose options do not support spread properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        if property.computed
            || property.shorthand
            || property.method
            || property.kind != PropertyKind::Init
        {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health expose options must use simple literal properties",
            )
            .with_path(path)
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health expose option names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        match key {
            "health" => {
                options.path =
                    health_path_from_expression(path, &property.value, "health", property.span)?;
            }
            "live" | "liveness" | "livenessPath" => {
                options.liveness_path =
                    health_path_from_expression(path, &property.value, "liveness", property.span)?;
            }
            "ready" | "readiness" | "readinessPath" => {
                options.readiness_path =
                    health_path_from_expression(path, &property.value, "readiness", property.span)?;
            }
            "startup" | "startupPath" => {
                options.startup_path = Some(health_path_from_expression(
                    path,
                    &property.value,
                    "startup",
                    property.span,
                )?);
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    format!("unsupported health expose option '{key}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }
    health_paths_are_distinct(path, object.span, &options)?;
    Ok(options)
}

fn health_check_chain_from_expression(
    path: &Path,
    source: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<Vec<HealthCheck>>, Diagnostic> {
    let mut checks = Vec::new();
    let mut current = expression;
    loop {
        let Expression::CallExpression(call) = current else {
            return Ok(None);
        };
        let Some((receiver, property)) = static_member_expression(&call.callee) else {
            return Ok(None);
        };
        match property {
            "check" => {
                checks.push(health_check_from_chain_call(path, source, call)?);
                current = receiver;
            }
            "health" => {
                if !call.arguments.is_empty() {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "app.health does not accept compiler-visible arguments",
                    )
                    .with_path(path)
                    .with_span(call.span));
                }
                let Some((root_receiver, root_property)) = static_member_name(&call.callee) else {
                    return Ok(None);
                };
                if root_property != "health" || !state.app_vars.contains(root_receiver) {
                    return Ok(None);
                }
                checks.reverse();
                return Ok(Some(checks));
            }
            _ => return Ok(None),
        }
    }
}

fn health_check_from_chain_call(
    path: &Path,
    source: &str,
    call: &CallExpression<'_>,
) -> Result<HealthCheck, Diagnostic> {
    if !(2..=3).contains(&call.arguments.len()) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "app.health().check requires a name, check function, and optional literal options",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let Some(name) = call.arguments.first().and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check name must be a string literal",
        )
        .with_path(path)
        .with_span(
            call.arguments
                .first()
                .and_then(argument_span)
                .unwrap_or(call.span),
        ));
    };
    if name.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check name must be non-empty",
        )
        .with_path(path)
        .with_span(
            call.arguments
                .first()
                .and_then(argument_span)
                .unwrap_or(call.span),
        ));
    }

    let Some(check_argument) = call.arguments.get(1) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check requires a check callback or built-in check",
        )
        .with_path(path)
        .with_span(call.span));
    };
    let check_source = health_chain_check_source(path, source, check_argument)?;
    let mut tags = Vec::new();
    let mut critical = true;
    let mut degraded_is_unhealthy = false;
    if let Some(options_argument) = call.arguments.get(2) {
        let Some(object) = object_argument(options_argument) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check options must be an object literal",
            )
            .with_path(path)
            .with_span(argument_span(options_argument).unwrap_or(call.span)));
        };
        for property in &object.properties {
            let ObjectPropertyKind::ObjectProperty(property) = property else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    "health check options do not support spread properties",
                )
                .with_path(path)
                .with_span(object.span));
            };
            if property.computed
                || property.shorthand
                || property.method
                || property.kind != PropertyKind::Init
            {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    "health check options must use simple literal properties",
                )
                .with_path(path)
                .with_span(property.span));
            }
            let Some(key) = property_key_name(&property.key) else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    "health check option names must be literal",
                )
                .with_path(path)
                .with_span(property.span));
            };
            match key {
                "tags" => {
                    tags = route_tags_from_expression(&property.value)
                        .map_err(|diagnostic| diagnostic.with_path(path))?;
                }
                "critical" => {
                    let Expression::BooleanLiteral(value) = &property.value else {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                            "health check critical flag must be a boolean literal",
                        )
                        .with_path(path)
                        .with_span(property.value.span()));
                    };
                    critical = value.value;
                }
                "degradedIsUnhealthy" => {
                    let Expression::BooleanLiteral(value) = &property.value else {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                            "health check degradedIsUnhealthy flag must be a boolean literal",
                        )
                        .with_path(path)
                        .with_span(property.value.span()));
                    };
                    degraded_is_unhealthy = value.value;
                }
                "timeoutMs" | "cacheMs" => {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        format!("compiler-generated health does not support health check option '{key}'"),
                    )
                    .with_path(path)
                    .with_span(property.span));
                }
                _ => {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        format!("unsupported health check option '{key}'"),
                    )
                    .with_path(path)
                    .with_span(property.span));
                }
            }
        }
    }

    Ok(HealthCheck {
        name: name.to_string(),
        check_source,
        liveness: tags.iter().any(|tag| tag == "live"),
        readiness: tags.iter().any(|tag| tag == "ready"),
        startup: tags.iter().any(|tag| tag == "startup"),
        critical,
        degraded_is_unhealthy,
        tags,
    })
}

fn health_chain_check_source(
    path: &Path,
    source: &str,
    argument: &Argument<'_>,
) -> Result<String, Diagnostic> {
    match argument {
        Argument::ArrowFunctionExpression(function) => {
            health_check_arrow_source(path, source, function)
        }
        Argument::FunctionExpression(function) => {
            health_check_function_source(path, source, function, false)
        }
        Argument::CallExpression(call) => {
            let Some((receiver, property)) = static_member_name(&call.callee) else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    "compiler-visible built-in health checks must use Health.*()",
                )
                .with_path(path)
                .with_span(call.span));
            };
            if receiver != "Health" || !call.arguments.is_empty() {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    "compiler-visible built-in health checks support only zero-argument Health.self(), Health.runtime(), Health.memory(), and Health.openApi(); richer Health.* checks are app-host-only",
                )
                .with_path(path)
                .with_span(call.span));
            }
            match property {
                "self" => Ok("() => ({ status: \"healthy\" })".to_string()),
                "runtime" => Ok("(ctx) => ((ctx && ctx.lifecycle && ctx.lifecycle.shuttingDown) ? { status: \"unhealthy\", errorCode: \"SLOPPY_HEALTH_SHUTTING_DOWN\" } : { status: \"healthy\" })".to_string()),
                "memory" => Ok("() => ({ status: \"healthy\" })".to_string()),
                "openApi" => Ok("() => ({ status: \"healthy\" })".to_string()),
                _ => Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    format!("unsupported compiler-visible built-in health check Health.{property}()"),
                )
                .with_path(path)
                .with_span(call.span)),
            }
        }
        _ => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check must be an inline function or supported Health.*() built-in",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or_default())),
    }
}

fn management_options_from_call(
    path: &Path,
    call: &CallExpression<'_>,
) -> Result<ManagementOptions, Diagnostic> {
    let mut options = ManagementOptions {
        path: DEFAULT_MANAGEMENT_PATH.to_string(),
        health: true,
        metrics: true,
        info: true,
        runtime: true,
    };
    let Some(argument) = call.arguments.first() else {
        return Ok(options);
    };
    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
            "app.management options must be an object literal",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
                "management options do not support spread properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        if property.computed
            || property.shorthand
            || property.method
            || property.kind != PropertyKind::Init
        {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
                "management options must use simple literal properties",
            )
            .with_path(path)
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
                "management option names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        match key {
            "path" => {
                options.path = health_path_from_expression(
                    path,
                    &property.value,
                    "management",
                    property.span,
                )?;
            }
            "health" => {
                options.health = literal_bool_option(path, &property.value, property.span, key)?
            }
            "metrics" => {
                options.metrics = literal_bool_option(path, &property.value, property.span, key)?
            }
            "info" => {
                options.info = literal_bool_option(path, &property.value, property.span, key)?
            }
            "runtime" => {
                options.runtime = literal_bool_option(path, &property.value, property.span, key)?
            }
            "protect" => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
                    "compiler-visible app.management does not support protect hooks; use the bootstrap app host for protected management endpoints",
                )
                .with_path(path)
                .with_span(property.span));
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
                    format!("unsupported management option '{key}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }
    Ok(options)
}

fn literal_bool_option(
    path: &Path,
    expression: &Expression<'_>,
    span: Span,
    name: &str,
) -> Result<bool, Diagnostic> {
    let Expression::BooleanLiteral(value) = expression else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
            format!("management option '{name}' must be a boolean literal"),
        )
        .with_path(path)
        .with_span(span));
    };
    Ok(value.value)
}

fn health_options_from_call(
    path: &Path,
    source: &str,
    call: &CallExpression<'_>,
) -> Result<HealthOptions, Diagnostic> {
    let mut options = HealthOptions {
        path: DEFAULT_HEALTH_PATH.to_string(),
        liveness_path: DEFAULT_LIVENESS_PATH.to_string(),
        readiness_path: DEFAULT_READINESS_PATH.to_string(),
        startup_path: None,
        checks: Vec::new(),
    };

    let Some(argument) = call.arguments.first() else {
        return Ok(options);
    };
    if let Some(path_value) = string_argument(argument) {
        options.path = health_path(
            path,
            path_value,
            argument_span(argument).unwrap_or(call.span),
        )?;
        health_paths_are_distinct(path, call.span, &options)?;
        return Ok(options);
    }

    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "app.mapHealthChecks options must be a string literal or object literal",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };

    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health options do not support spread properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        if property.computed
            || property.shorthand
            || property.method
            || property.kind != PropertyKind::Init
        {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health options must use simple literal properties",
            )
            .with_path(path)
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health option names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        match key {
            "path" => {
                options.path =
                    health_path_from_expression(path, &property.value, "health", property.span)?;
            }
            "livenessPath" => {
                options.liveness_path =
                    health_path_from_expression(path, &property.value, "liveness", property.span)?;
            }
            "readinessPath" => {
                options.readiness_path =
                    health_path_from_expression(path, &property.value, "readiness", property.span)?;
            }
            "startupPath" => {
                options.startup_path = Some(health_path_from_expression(
                    path,
                    &property.value,
                    "startup",
                    property.span,
                )?);
            }
            "checks" => {
                options.checks = health_checks_from_expression(path, source, &property.value)?;
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    format!("unsupported health option '{key}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }

    health_paths_are_distinct(path, object.span, &options)?;
    Ok(options)
}

fn health_path_from_expression(
    path: &Path,
    expression: &Expression<'_>,
    subject: &str,
    fallback_span: Span,
) -> Result<String, Diagnostic> {
    let Some(value) = expression_string_literal(expression) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            format!("health {subject} path must be a string literal"),
        )
        .with_path(path)
        .with_span(expression.span()));
    };
    health_path(path, value, fallback_span)
}

fn health_path(path: &Path, value: &str, span: Span) -> Result<String, Diagnostic> {
    if value.is_empty() || !value.starts_with('/') {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health paths must be non-empty string literals starting with '/'",
        )
        .with_path(path)
        .with_span(span));
    }
    let normalized = normalize_framework_route_pattern(value);
    if !route_pattern_supported(&normalized) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health path is outside the Plan v1 alpha route syntax",
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use '/', static segments, {name}, {name:str}, or {name:int}."));
    }
    Ok(value.to_string())
}

fn health_paths_are_distinct(
    path: &Path,
    span: Span,
    options: &HealthOptions,
) -> Result<(), Diagnostic> {
    let mut seen = BTreeSet::new();
    if !seen.insert(options.path.clone())
        || !seen.insert(options.liveness_path.clone())
        || !seen.insert(options.readiness_path.clone())
        || options
            .startup_path
            .as_ref()
            .is_some_and(|startup_path| !seen.insert(startup_path.clone()))
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health, liveness, readiness, and startup paths must be distinct",
        )
        .with_path(path)
        .with_span(span));
    }
    Ok(())
}

fn health_checks_from_expression(
    path: &Path,
    source: &str,
    expression: &Expression<'_>,
) -> Result<Vec<HealthCheck>, Diagnostic> {
    let Expression::ArrayExpression(array) = expression else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health checks must be an array literal",
        )
        .with_path(path)
        .with_span(expression.span()));
    };

    let mut checks = Vec::new();
    for (index, element) in array.elements.iter().enumerate() {
        checks.push(health_check_from_array_element(
            path, source, element, index, array.span,
        )?);
    }
    Ok(checks)
}

fn health_check_from_array_element(
    path: &Path,
    source: &str,
    element: &ArrayExpressionElement<'_>,
    index: usize,
    fallback_span: Span,
) -> Result<HealthCheck, Diagnostic> {
    match element {
        ArrayExpressionElement::ArrowFunctionExpression(function) => Ok(HealthCheck {
            name: format!("check-{}", index + 1),
            check_source: health_check_arrow_source(path, source, function)?,
            liveness: false,
            readiness: true,
            startup: false,
            critical: true,
            degraded_is_unhealthy: false,
            tags: Vec::new(),
        }),
        ArrayExpressionElement::FunctionExpression(function) => {
            let name = function
                .id
                .as_ref()
                .map(|identifier| identifier.name.as_str())
                .filter(|name| !name.is_empty())
                .map_or_else(|| format!("check-{}", index + 1), ToOwned::to_owned);
            Ok(HealthCheck {
                name,
                check_source: health_check_function_source(path, source, function, false)?,
                liveness: false,
                readiness: true,
                startup: false,
                critical: true,
                degraded_is_unhealthy: false,
                tags: Vec::new(),
            })
        }
        ArrayExpressionElement::ObjectExpression(object) => {
            health_check_from_object(path, source, object)
        }
        _ => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health checks must be inline functions or literal check objects",
        )
        .with_path(path)
        .with_span(fallback_span)),
    }
}

fn health_check_from_object(
    path: &Path,
    source: &str,
    object: &oxc_ast::ast::ObjectExpression<'_>,
) -> Result<HealthCheck, Diagnostic> {
    let mut name = None;
    let mut check_source = None;
    let mut liveness = false;
    let mut readiness = true;
    let mut startup = false;
    let mut critical = true;
    let mut degraded_is_unhealthy = false;
    let mut tags = Vec::new();

    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check objects do not support spread properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        if property.computed || property.shorthand || property.kind != PropertyKind::Init {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check objects must use simple literal properties",
            )
            .with_path(path)
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check property names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        if property.method && key != "check" {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check objects only support method syntax for the check function",
            )
            .with_path(path)
            .with_span(property.span));
        }
        match key {
            "name" => {
                let Some(value) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "health check name must be a string literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                if value.is_empty() {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "health check name must be non-empty",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                }
                name = Some(value.to_string());
            }
            "check" => {
                check_source = Some(health_check_source_from_expression(
                    path,
                    source,
                    &property.value,
                    property.method,
                    property.span,
                )?);
            }
            "liveness" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "health check liveness flag must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                liveness = value.value;
            }
            "readiness" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "health check readiness flag must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                readiness = value.value;
            }
            "startup" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "health check startup flag must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                startup = value.value;
            }
            "critical" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "health check critical flag must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                critical = value.value;
            }
            "degradedIsUnhealthy" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "health check degradedIsUnhealthy flag must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                degraded_is_unhealthy = value.value;
            }
            "timeoutMs" | "cacheMs" => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    format!(
                        "compiler-generated health does not support health check option '{key}'"
                    ),
                )
                .with_path(path)
                .with_span(property.span));
            }
            "tags" => {
                tags = route_tags_from_expression(&property.value)
                    .map_err(|diagnostic| diagnostic.with_path(path))?;
                liveness |= tags.iter().any(|tag| tag == "live");
                readiness |= tags.iter().any(|tag| tag == "ready");
                startup |= tags.iter().any(|tag| tag == "startup");
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    format!("unsupported health check option '{key}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }

    let Some(name) = name else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check objects must include a literal name",
        )
        .with_path(path)
        .with_span(object.span));
    };
    let Some(check_source) = check_source else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check objects must include an inline check function",
        )
        .with_path(path)
        .with_span(object.span));
    };
    if !liveness && !readiness && !startup {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check must target readiness, liveness, or startup",
        )
        .with_path(path)
        .with_span(object.span));
    }

    Ok(HealthCheck {
        name,
        check_source,
        liveness,
        readiness,
        startup,
        critical,
        degraded_is_unhealthy,
        tags,
    })
}

fn health_check_source_from_expression(
    path: &Path,
    source: &str,
    expression: &Expression<'_>,
    method: bool,
    fallback_span: Span,
) -> Result<String, Diagnostic> {
    match expression {
        Expression::ArrowFunctionExpression(function) => {
            health_check_arrow_source(path, source, function)
        }
        Expression::FunctionExpression(function) => {
            health_check_function_source(path, source, function, method)
        }
        _ => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check must be an inline function",
        )
        .with_path(path)
        .with_span(fallback_span)),
    }
}

fn health_check_arrow_source(
    path: &Path,
    source: &str,
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
) -> Result<String, Diagnostic> {
    if handler_parameters_are_unsupported(&function.params) || arrow_has_typescript_syntax(function)
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check functions may declare at most one untyped context parameter",
        )
        .with_path(path)
        .with_span(function.span));
    }
    reject_health_check_captures(
        path,
        function.span,
        health_check_arrow_free_identifiers(function),
    )?;
    source_slice(source, function.span).ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check function source could not be extracted",
        )
        .with_path(path)
        .with_span(function.span)
    })
}

fn health_check_function_source(
    path: &Path,
    source: &str,
    function: &oxc_ast::ast::Function<'_>,
    method: bool,
) -> Result<String, Diagnostic> {
    if function.generator
        || function.body.is_none()
        || handler_parameters_are_unsupported(&function.params)
        || function_has_typescript_syntax(function)
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check functions may declare at most one untyped context parameter",
        )
        .with_path(path)
        .with_span(function.span));
    }
    reject_health_check_captures(
        path,
        function.span,
        health_check_function_free_identifiers(function),
    )?;
    if method {
        let params = source_slice(source, function.params.span).ok_or_else(|| {
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check function source could not be extracted",
            )
            .with_path(path)
            .with_span(function.span)
        })?;
        let Some(function_body) = function.body.as_ref() else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check function source could not be extracted",
            )
            .with_path(path)
            .with_span(function.span));
        };
        let body = source_slice(source, function_body.span).ok_or_else(|| {
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check function source could not be extracted",
            )
            .with_path(path)
            .with_span(function.span)
        })?;
        let async_prefix = if function.r#async { "async " } else { "" };
        return Ok(format!("{async_prefix}function {params} {body}"));
    }
    source_slice(source, function.span).ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check function source could not be extracted",
        )
        .with_path(path)
        .with_span(function.span)
    })
}

fn reject_health_check_captures(
    path: &Path,
    span: Span,
    free_identifiers: BTreeSet<String>,
) -> Result<(), Diagnostic> {
    if free_identifiers.is_empty() {
        return Ok(());
    }
    let identifier = free_identifiers
        .iter()
        .next()
        .map(String::as_str)
        .unwrap_or("");
    Err(Diagnostic::new(
        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
        format!("health check captures unsupported identifier '{identifier}'"),
    )
    .with_path(path)
    .with_span(span)
    .with_hint("Use inline health checks that only depend on their optional context parameter and local values."))
}

fn health_check_arrow_free_identifiers(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
) -> BTreeSet<String> {
    let mut scope = BTreeSet::new();
    collect_formal_parameter_bindings(&function.params, &mut scope);
    collect_function_body_bindings(&function.body.statements, &mut scope);
    let mut free = BTreeSet::new();
    collect_statement_list_identifier_references(&function.body.statements, &scope, &mut free);
    free
}

fn health_check_function_free_identifiers(
    function: &oxc_ast::ast::Function<'_>,
) -> BTreeSet<String> {
    let mut scope = BTreeSet::new();
    collect_formal_parameter_bindings(&function.params, &mut scope);
    if let Some(identifier) = &function.id {
        scope.insert(identifier.name.as_str().to_string());
    }
    let Some(body) = &function.body else {
        return BTreeSet::new();
    };
    collect_function_body_bindings(&body.statements, &mut scope);
    let mut free = BTreeSet::new();
    collect_statement_list_identifier_references(&body.statements, &scope, &mut free);
    free
}

fn health_routes_from_options(
    path: &Path,
    source: &str,
    source_name: &str,
    span: Span,
    options: &HealthOptions,
    middleware: &[FrameworkMiddleware],
    cors: Option<&CorsPolicy>,
) -> Result<Vec<Route>, Diagnostic> {
    let mut routes = vec![
        health_route(
            path,
            source,
            source_name,
            span,
            HealthRouteSpec {
                framework_path: &options.path,
                name: "Health",
                kind: "aggregate",
                checks: options.checks.iter().collect(),
            },
            middleware,
            cors,
        )?,
        health_route(
            path,
            source,
            source_name,
            span,
            HealthRouteSpec {
                framework_path: &options.liveness_path,
                name: "Health.Liveness",
                kind: "liveness",
                checks: options
                    .checks
                    .iter()
                    .filter(|check| check.liveness)
                    .collect(),
            },
            middleware,
            cors,
        )?,
        health_route(
            path,
            source,
            source_name,
            span,
            HealthRouteSpec {
                framework_path: &options.readiness_path,
                name: "Health.Readiness",
                kind: "readiness",
                checks: options
                    .checks
                    .iter()
                    .filter(|check| check.readiness)
                    .collect(),
            },
            middleware,
            cors,
        )?,
    ];
    if let Some(startup_path) = &options.startup_path {
        routes.push(health_route(
            path,
            source,
            source_name,
            span,
            HealthRouteSpec {
                framework_path: startup_path,
                name: "Health.Startup",
                kind: "startup",
                checks: options
                    .checks
                    .iter()
                    .filter(|check| check.startup)
                    .collect(),
            },
            middleware,
            cors,
        )?);
    }
    Ok(routes)
}

fn management_routes_from_options(
    path: &Path,
    source: &str,
    source_name: &str,
    span: Span,
    options: &ManagementOptions,
    middleware: &[FrameworkMiddleware],
    cors: Option<&CorsPolicy>,
) -> Result<Vec<Route>, Diagnostic> {
    let mut routes = Vec::new();
    let ops_context = OpsRouteContext {
        path,
        source,
        source_name,
        span,
        middleware,
        cors,
    };
    if options.health {
        let health_options = HealthOptions {
            path: join_route_patterns(&options.path, "health"),
            liveness_path: join_route_patterns(&options.path, "live"),
            readiness_path: join_route_patterns(&options.path, "ready"),
            startup_path: Some(join_route_patterns(&options.path, "startup")),
            checks: vec![
                HealthCheck {
                    name: "self".to_string(),
                    check_source: "() => ({ status: \"healthy\" })".to_string(),
                    liveness: true,
                    readiness: true,
                    startup: true,
                    critical: true,
                    degraded_is_unhealthy: false,
                    tags: vec![
                        "live".to_string(),
                        "ready".to_string(),
                        "startup".to_string(),
                    ],
                },
                HealthCheck {
                    name: "runtime".to_string(),
                    check_source: "(ctx) => ((ctx && ctx.lifecycle && ctx.lifecycle.shuttingDown) ? { status: \"unhealthy\", errorCode: \"SLOPPY_HEALTH_SHUTTING_DOWN\" } : { status: \"healthy\" })".to_string(),
                    liveness: false,
                    readiness: true,
                    startup: true,
                    critical: true,
                    degraded_is_unhealthy: false,
                    tags: vec!["ready".to_string(), "startup".to_string()],
                },
                HealthCheck {
                    name: "memory".to_string(),
                    check_source: "() => ({ status: \"healthy\" })".to_string(),
                    liveness: false,
                    readiness: false,
                    startup: false,
                    critical: false,
                    degraded_is_unhealthy: false,
                    tags: vec!["health".to_string()],
                },
            ],
        };
        routes.extend(management_health_routes(
            path,
            source,
            source_name,
            span,
            &health_options,
            middleware,
            cors,
        )?);
    }
    if options.metrics {
        routes.push(ops_route(
            &ops_context,
            &join_route_patterns(&options.path, "metrics"),
            "Management.Metrics",
            "function() { return Results.text(\"# HELP sloppy_management_info Static management endpoint metadata\\n# TYPE sloppy_management_info gauge\\nsloppy_management_info 1\\n\", { contentType: \"text/plain; version=0.0.4; charset=utf-8\" }); }",
            "text",
            200,
        )?);
        routes.push(ops_route(
            &ops_context,
            &join_route_patterns(&options.path, "metrics.json"),
            "Management.MetricsJson",
            "function() { return Results.json({ counters: {}, gauges: { sloppy_management_info: { value: 1, labels: {} } }, histograms: {} }); }",
            "json",
            200,
        )?);
    }
    if options.info {
        routes.push(ops_route(
            &ops_context,
            &join_route_patterns(&options.path, "info"),
            "Management.Info",
            "function() { return Results.json({ app: { name: \"sloppy-app\" }, runtime: { name: \"sloppy\" }, management: { safe: true } }); }",
            "json",
            200,
        )?);
    }
    if options.runtime {
        routes.push(ops_route(
            &ops_context,
            &join_route_patterns(&options.path, "runtime"),
            "Management.Runtime",
            "function() { return Results.json({ lifecycle: { startupComplete: true, shuttingDown: false }, diagnostics: { count: 0 }, management: { safe: true } }); }",
            "json",
            200,
        )?);
    }
    Ok(routes)
}

fn management_health_routes(
    path: &Path,
    source: &str,
    source_name: &str,
    span: Span,
    options: &HealthOptions,
    middleware: &[FrameworkMiddleware],
    cors: Option<&CorsPolicy>,
) -> Result<Vec<Route>, Diagnostic> {
    let mut routes =
        health_routes_from_options(path, source, source_name, span, options, middleware, cors)?;
    for route in &mut routes {
        route.name = match route.health.as_ref().map(|health| health.kind) {
            Some("aggregate") => Some("Management.Health".to_string()),
            Some("liveness") => Some("Management.Live".to_string()),
            Some("readiness") => Some("Management.Ready".to_string()),
            Some("startup") => Some("Management.Startup".to_string()),
            _ => route.name.clone(),
        };
    }
    Ok(routes)
}

struct OpsRouteContext<'a> {
    path: &'a Path,
    source: &'a str,
    source_name: &'a str,
    span: Span,
    middleware: &'a [FrameworkMiddleware],
    cors: Option<&'a CorsPolicy>,
}

fn ops_route(
    context: &OpsRouteContext<'_>,
    framework_path: &str,
    name: &str,
    handler_source: &str,
    response_kind: &str,
    status: u16,
) -> Result<Route, Diagnostic> {
    let normalized_pattern = normalize_framework_route_pattern(framework_path);
    let response = ResponseMetadata {
        helper: response_kind.to_string(),
        status,
        kind: response_kind.to_string(),
        body_schema: None,
        native_body: None,
        source_name: Some(context.source_name.to_string()),
        source_text: Some(context.source.to_string()),
        span: Some(context.span),
        partial: false,
    };
    let emitted_source =
        wrap_handler_with_framework_pipeline(handler_source, context.middleware, context.cors);
    Ok(Route {
        method: "GET",
        kind: "http",
        websocket: None,
        realtime: None,
        framework_path: (normalized_pattern != framework_path).then(|| framework_path.to_string()),
        pattern: normalized_pattern,
        name: Some(name.to_string()),
        tags: Vec::new(),
        summary: None,
        description: None,
        deprecated: None,
        consumes: Vec::new(),
        produces: Vec::new(),
        headers: Vec::new(),
        query_schema: None,
        params_schema: None,
        openapi_override: None,
        output_cache: None,
        cache_headers: None,
        docs: None,
        health: None,
        middleware: route_middleware_metadata(context.middleware),
        auth: None,
        cors: context.cors.map(cors_policy_metadata),
        cors_preflight: false,
        span: context.span,
        source_path: context.path.to_path_buf(),
        source_name: context.source_name.to_string(),
        source: context.source.to_string(),
        module: None,
        handler: Handler {
            source: source_slice(context.source, context.span)
                .unwrap_or_else(|| "app.management()".to_string()),
            emitted_source,
            span: context.span,
            requires_results_import: false,
            is_async: false,
            runtime_deferred: false,
            source_name: context.source_name.to_string(),
            source_text: context.source.to_string(),
            source_map_line_offset: 0,
            source_map_column_offset: 0,
            bindings: Vec::new(),
            response: Some(response.clone()),
            responses: vec![response],
            effects: Vec::new(),
            schema_metadata_conflict: false,
        },
    })
}

fn health_route(
    path: &Path,
    source: &str,
    source_name: &str,
    span: Span,
    spec: HealthRouteSpec<'_>,
    middleware: &[FrameworkMiddleware],
    cors: Option<&CorsPolicy>,
) -> Result<Route, Diagnostic> {
    let normalized_pattern = normalize_framework_route_pattern(spec.framework_path);
    let response = health_response_metadata("ok", 200, span, source_name, source);
    let responses = vec![
        response.clone(),
        health_response_metadata("status", 503, span, source_name, source),
    ];
    let check_names = spec
        .checks
        .iter()
        .map(|check| check.name.clone())
        .collect::<Vec<_>>();
    let mut handler_source = health_handler_source(spec.checks);
    handler_source = wrap_handler_with_framework_pipeline(&handler_source, middleware, cors);
    Ok(Route {
        method: "GET",
        kind: "http",
        websocket: None,
        realtime: None,
        framework_path: (normalized_pattern != spec.framework_path)
            .then(|| spec.framework_path.to_string()),
        pattern: normalized_pattern,
        name: Some(spec.name.to_string()),
        tags: Vec::new(),
        summary: None,
        description: None,
        deprecated: None,
        consumes: Vec::new(),
        produces: Vec::new(),
        headers: Vec::new(),
        query_schema: None,
        params_schema: None,
        openapi_override: None,
        output_cache: None,
        cache_headers: None,
        docs: None,
        health: Some(HealthRouteMetadata {
            kind: spec.kind,
            checks: check_names,
        }),
        middleware: route_middleware_metadata(middleware),
        auth: None,
        cors: cors.map(cors_policy_metadata),
        cors_preflight: false,
        span,
        source_path: path.to_path_buf(),
        source_name: source_name.to_string(),
        source: source.to_string(),
        module: None,
        handler: Handler {
            source: source_slice(source, span)
                .unwrap_or_else(|| "app.mapHealthChecks()".to_string()),
            emitted_source: handler_source,
            span,
            requires_results_import: false,
            is_async: true,
            runtime_deferred: false,
            source_name: source_name.to_string(),
            source_text: source.to_string(),
            source_map_line_offset: 0,
            source_map_column_offset: 0,
            bindings: Vec::new(),
            response: Some(response),
            responses,
            effects: Vec::new(),
            schema_metadata_conflict: false,
        },
    })
}

fn health_response_metadata(
    helper: &str,
    status: u16,
    span: Span,
    source_name: &str,
    source: &str,
) -> ResponseMetadata {
    ResponseMetadata {
        helper: helper.to_string(),
        status,
        kind: "json".to_string(),
        body_schema: None,
        native_body: None,
        source_name: Some(source_name.to_string()),
        source_text: Some(source.to_string()),
        span: Some(span),
        partial: false,
    }
}

fn app_docs_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<Vec<Route>>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if property != "docs" || !state.app_vars.contains(receiver) {
        return Ok(None);
    }
    if call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_DOCS",
            "app.docs accepts at most one literal options argument",
        )
        .with_path(path)
        .with_span(call.span));
    }

    let options = docs_options_from_call(path, call)?;
    if !options.enabled {
        return Ok(Some(Vec::new()));
    }
    docs_paths_are_distinct(path, call.span, &options)?;
    Ok(Some(vec![
        docs_route(DocsRouteInput {
            path,
            source,
            source_name,
            span: call.span,
            framework_path: &options.openapi_path,
            name: "Docs.OpenApi",
            kind: "json",
            helper: "openapi",
            handler_source: docs_openapi_handler_source(),
            docs: Some(DocsRouteMetadata {
                kind: "openapi",
                strict: options.strict,
            }),
            auth: options.require_auth.clone(),
            middleware: &state.middleware,
            cors: state.cors_policy.as_ref(),
        })?,
        docs_route(DocsRouteInput {
            path,
            source,
            source_name,
            span: call.span,
            framework_path: &options.path,
            name: "Docs.Ui",
            kind: "html",
            helper: "html",
            handler_source: docs_html_handler_source(&options.title, &options.openapi_path),
            docs: Some(DocsRouteMetadata {
                kind: "ui",
                strict: options.strict,
            }),
            auth: options.require_auth,
            middleware: &state.middleware,
            cors: state.cors_policy.as_ref(),
        })?,
    ]))
}

fn docs_options_from_call(
    path: &Path,
    call: &CallExpression<'_>,
) -> Result<DocsOptions, Diagnostic> {
    let mut options = DocsOptions {
        enabled: true,
        strict: false,
        path: "/docs".to_string(),
        openapi_path: "/openapi.json".to_string(),
        title: "Sloppy API".to_string(),
        require_auth: None,
    };
    let Some(argument) = call.arguments.first() else {
        return Ok(options);
    };
    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_DOCS",
            "app.docs options must be an object literal",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_DOCS",
                "app.docs options do not support spread properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_DOCS",
                "app.docs option names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        match key {
            "enabled" => {
                let Expression::BooleanLiteral(enabled) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_DOCS",
                        "app.docs enabled must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                options.enabled = enabled.value;
            }
            "strict" => {
                let Expression::BooleanLiteral(strict) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_DOCS",
                        "app.docs strict must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                options.strict = strict.value;
            }
            "path" => {
                let Some(value) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_DOCS",
                        "app.docs path must be a string literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                options.path = docs_path(path, value, property.span)?;
            }
            "openapiPath" => {
                let Some(value) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_DOCS",
                        "app.docs openapiPath must be a string literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                options.openapi_path = docs_path(path, value, property.span)?;
            }
            "title" => {
                let Some(value) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_DOCS",
                        "app.docs title must be a string literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                options.title = value.to_string();
            }
            "requireAuth" => match &property.value {
                Expression::BooleanLiteral(required) => {
                    if required.value {
                        options.require_auth = Some(AuthRequirementMetadata {
                            required: true,
                            ..AuthRequirementMetadata::default()
                        });
                    }
                }
                Expression::ObjectExpression(auth_object) => {
                    options.require_auth = Some(auth_requirement_from_object(auth_object)?);
                }
                _ => {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_DOCS",
                        "app.docs requireAuth must be a boolean literal or object literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                }
            },
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_DOCS",
                    format!("unsupported app.docs option '{key}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }
    Ok(options)
}

fn docs_path(path: &Path, value: &str, span: Span) -> Result<String, Diagnostic> {
    if value.is_empty() || !value.starts_with('/') || (value.len() > 1 && value.ends_with('/')) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_DOCS",
            "app.docs paths must be absolute string literals without trailing slash",
        )
        .with_path(path)
        .with_span(span));
    }
    let normalized = normalize_framework_route_pattern(value);
    if !route_pattern_supported(&normalized) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_DOCS",
            "app.docs path is outside the Plan v1 alpha route syntax",
        )
        .with_path(path)
        .with_span(span));
    }
    Ok(normalized)
}

fn docs_paths_are_distinct(
    path: &Path,
    span: Span,
    options: &DocsOptions,
) -> Result<(), Diagnostic> {
    if options.path == options.openapi_path {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_DOCS",
            "app.docs path and openapiPath must be distinct",
        )
        .with_path(path)
        .with_span(span));
    }
    Ok(())
}

struct DocsRouteInput<'a> {
    path: &'a Path,
    source: &'a str,
    source_name: &'a str,
    span: Span,
    framework_path: &'a str,
    name: &'a str,
    kind: &'a str,
    helper: &'a str,
    handler_source: String,
    docs: Option<DocsRouteMetadata>,
    auth: Option<AuthRequirementMetadata>,
    middleware: &'a [FrameworkMiddleware],
    cors: Option<&'a CorsPolicy>,
}

fn docs_route(input: DocsRouteInput<'_>) -> Result<Route, Diagnostic> {
    let response = ResponseMetadata {
        helper: input.helper.to_string(),
        status: 200,
        kind: input.kind.to_string(),
        body_schema: None,
        native_body: None,
        source_name: Some(input.source_name.to_string()),
        source_text: Some(input.source.to_string()),
        span: Some(input.span),
        partial: false,
    };
    let handler_source =
        wrap_handler_with_framework_pipeline(&input.handler_source, input.middleware, input.cors);
    Ok(Route {
        method: "GET",
        kind: "http",
        websocket: None,
        realtime: None,
        framework_path: None,
        pattern: input.framework_path.to_string(),
        name: Some(input.name.to_string()),
        tags: vec!["Documentation".to_string()],
        summary: None,
        description: None,
        deprecated: None,
        consumes: Vec::new(),
        produces: vec![if input.kind == "html" {
            "text/html".to_string()
        } else {
            "application/json".to_string()
        }],
        headers: Vec::new(),
        query_schema: None,
        params_schema: None,
        openapi_override: None,
        output_cache: None,
        cache_headers: None,
        docs: input.docs,
        health: None,
        middleware: route_middleware_metadata(input.middleware),
        auth: input.auth,
        cors: input.cors.map(cors_policy_metadata),
        cors_preflight: false,
        span: input.span,
        source_path: input.path.to_path_buf(),
        source_name: input.source_name.to_string(),
        source: input.source.to_string(),
        module: None,
        handler: Handler {
            source: source_slice(input.source, input.span)
                .unwrap_or_else(|| "app.docs()".to_string()),
            emitted_source: handler_source,
            span: input.span,
            requires_results_import: false,
            is_async: !input.middleware.is_empty() || input.cors.is_some(),
            runtime_deferred: false,
            source_name: input.source_name.to_string(),
            source_text: input.source.to_string(),
            source_map_line_offset: 0,
            source_map_column_offset: 0,
            bindings: Vec::new(),
            response: Some(response.clone()),
            responses: vec![response],
            effects: Vec::new(),
            schema_metadata_conflict: false,
        },
    })
}

fn docs_openapi_handler_source() -> String {
    "function() { return Results.json({ ok: false, code: \"SLOPPY_E_OPENAPI_ARTIFACT_REQUIRED\" }, { status: 500 }); }".to_string()
}

fn docs_html_handler_source(title: &str, openapi_path: &str) -> String {
    let html = format!(
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>{}</title></head><body><pre id=\"spec\">Loading...</pre><script>fetch({}).then(r=>r.json()).then(j=>document.getElementById(\"spec\").textContent=JSON.stringify(j,null,2));</script></body></html>",
        title,
        serde_json::to_string(openapi_path).unwrap_or_else(|_| "\"/openapi.json\"".to_string())
    );
    let html = serde_json::to_string(&html).unwrap_or_else(|_| "\"\"".to_string());
    format!("function() {{ return Results.html({html}); }}")
}

fn health_handler_source(checks: Vec<&HealthCheck>) -> String {
    let entries = checks
        .iter()
        .map(|check| {
            let name = serde_json::to_string(&check.name).unwrap_or_else(|_| "\"\"".to_string());
            let critical = if check.critical { "true" } else { "false" };
            let degraded_is_unhealthy = if check.degraded_is_unhealthy {
                "true"
            } else {
                "false"
            };
            let tags = serde_json::to_string(&check.tags).unwrap_or_else(|_| "[]".to_string());
            format!(
                "{{ name: {name}, check: {}, critical: {critical}, degradedIsUnhealthy: {degraded_is_unhealthy}, tags: {tags} }}",
                check.check_source
            )
        })
        .collect::<Vec<_>>()
        .join(", ");
    format!(
        "async function(ctx) {{ const __sloppy_health_started = Date.now(); const __sloppy_health_checks = [{entries}]; const __sloppy_health_results = []; let __sloppy_health_rank = 0; for (const __sloppy_health_check of __sloppy_health_checks) {{ const __sloppy_check_started = Date.now(); let __sloppy_check_status = \"healthy\"; try {{ const __sloppy_health_value = await __sloppy_health_check.check(ctx); if (__sloppy_health_value === false) __sloppy_check_status = \"unhealthy\"; else if (__sloppy_health_value && typeof __sloppy_health_value === \"object\") {{ if (__sloppy_health_value.status === \"degraded\" || __sloppy_health_value.status === \"unhealthy\") __sloppy_check_status = __sloppy_health_value.status; else if (__sloppy_health_value.ok === false) __sloppy_check_status = \"unhealthy\"; }} }} catch {{ __sloppy_check_status = \"unhealthy\"; }} let __sloppy_aggregate_status = __sloppy_check_status; if (!__sloppy_health_check.critical && __sloppy_aggregate_status === \"unhealthy\") __sloppy_aggregate_status = \"degraded\"; if (__sloppy_health_check.critical && __sloppy_check_status === \"degraded\" && __sloppy_health_check.degradedIsUnhealthy === true) __sloppy_aggregate_status = \"unhealthy\"; const __sloppy_aggregate_rank = __sloppy_aggregate_status === \"unhealthy\" ? 2 : (__sloppy_aggregate_status === \"degraded\" ? 1 : 0); if (__sloppy_aggregate_rank > __sloppy_health_rank) __sloppy_health_rank = __sloppy_aggregate_rank; __sloppy_health_results.push({{ name: __sloppy_health_check.name, status: __sloppy_check_status, critical: __sloppy_health_check.critical, tags: __sloppy_health_check.tags, durationMs: Date.now() - __sloppy_check_started, checkedAtUtc: new Date().toISOString() }}); }} const __sloppy_health_status = __sloppy_health_rank === 2 ? \"unhealthy\" : (__sloppy_health_rank === 1 ? \"degraded\" : \"healthy\"); const __sloppy_health_body = {{ status: __sloppy_health_status, durationMs: Date.now() - __sloppy_health_started, checkedAtUtc: new Date().toISOString(), checks: __sloppy_health_results }}; return __sloppy_health_status === \"unhealthy\" ? Results.status(503, __sloppy_health_body) : Results.ok(__sloppy_health_body); }}"
    )
}

const PROBLEM_DETAILS_WRAPPER_PREFIX: &str = "async function(ctx) { try { return await (";

fn wrap_handler_with_problem_details(source: &str, detail: &str) -> String {
    let detail_branch = match detail {
        "always" => "true",
        "development" => "(String((ctx && ctx.config && typeof ctx.config.get === \"function\") ? (ctx.config.get(\"Sloppy:Environment\") ?? \"\") : \"\")).toLowerCase() === \"development\"",
        _ => "false",
    };
    format!(
        "{prefix}{source})(ctx); }} catch (error) {{ const __sloppy_problem = {{ status: 500, title: \"Internal Server Error\", code: \"SLOPPY_E_HANDLER_ERROR\" }}; if ({detail_branch}) {{ __sloppy_problem.detail = String((error && error.message) ?? error); }} return Results.problem(__sloppy_problem, {{ status: 500 }}); }} }}",
        prefix = PROBLEM_DETAILS_WRAPPER_PREFIX,
    )
}

fn apply_problem_details_to_routes(
    path: &Path,
    routes: &mut [Route],
    descriptor: &ProblemDetailsDescriptor,
) -> Result<(), Diagnostic> {
    for route in routes {
        route.handler.emitted_source =
            wrap_handler_with_problem_details(&route.handler.emitted_source, &descriptor.detail);
        route.handler.is_async = true;
        // The wrapper prepends a single-line prefix; shift the column offset
        // so source-map mappings still anchor on the original handler body.
        route.handler.source_map_column_offset = route
            .handler
            .source_map_column_offset
            .checked_add(PROBLEM_DETAILS_WRAPPER_PREFIX.len())
            .ok_or_else(|| {
                Diagnostic::new(
                    "SLOPPYC_E_INTERNAL_OFFSET_OVERFLOW",
                    "ProblemDetails wrapper produced a source-map offset that overflows usize",
                )
                .with_path(path)
                .with_span(route.handler.span)
            })?;
        let problem_response = ResponseMetadata {
            helper: "problem".to_string(),
            status: 500,
            kind: "problem".to_string(),
            body_schema: None,
            native_body: None,
            source_name: None,
            source_text: None,
            span: None,
            partial: true,
        };
        let already_has_problem = route.handler.responses.iter().any(|response| {
            response.helper == "problem" && response.status == 500 && response.kind == "problem"
        });
        if !already_has_problem {
            route.handler.responses.push(problem_response.clone());
        }
        if route.handler.response.is_none() {
            route.handler.response = Some(problem_response);
        }
    }
    Ok(())
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

fn orm_table_declaration_source(
    path: &Path,
    source: &str,
    name: &str,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<Option<String>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Expression::Identifier(callee) = &call.callee else {
        return Ok(None);
    };
    if callee.name.as_str() != "table" || !state.orm_imported {
        return Ok(None);
    }
    if call.arguments.len() < 2 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ORM_TABLE",
            "ORM table declarations require a literal table name and static column object",
        )
        .with_path(path)
        .with_span(call.span)
        .with_hint(orm_table_hint()));
    }
    let columns_argument = &call.arguments[1];
    if let Argument::ObjectExpression(columns) = columns_argument {
        if columns.properties.is_empty() {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ORM_TABLE",
                "ORM table declarations require a non-empty column object",
            )
            .with_path(path)
            .with_span(argument_span(columns_argument).unwrap_or(call.span))
            .with_hint(orm_table_hint()));
        }
    }
    if let Some(metadata) =
        orm_table_metadata_from_call(source_name_from_path(path), source, name, call)
    {
        state.orm_tables.push(metadata);
    } else {
        state.orm_extraction_partial = true;
    }
    let Some(init_source) = source_slice(source, expression.span()) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ORM_TABLE",
            "ORM table declaration source could not be extracted",
        )
        .with_path(path)
        .with_span(call.span)
        .with_hint(orm_table_hint()));
    };
    Ok(Some(format!("const {name} = {init_source};")))
}

fn source_name_from_path(path: &Path) -> String {
    path.file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("app")
        .to_string()
}

fn orm_table_metadata_from_call(
    source_name: String,
    source: &str,
    model: &str,
    call: &CallExpression<'_>,
) -> Option<Value> {
    let table_name = string_argument(call.arguments.first()?)?;
    let columns_object = object_argument(call.arguments.get(1)?)?;
    let mut columns = Vec::new();
    for property in &columns_object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return None;
        };
        if property.computed || property.method || property.shorthand {
            return None;
        }
        let column_name = property_key_name(&property.key)?.to_string();
        let column_expression = source_slice(source, property.value.span())?;
        let column_type = orm_column_type_from_expression(&property.value)?;
        columns.push(json!({
            "name": column_name,
            "type": column_type,
            "primaryKey": orm_column_has_modifier(&property.value, "primaryKey"),
            "nullable": orm_column_has_modifier(&property.value, "nullable"),
            "notNull": orm_column_has_modifier(&property.value, "notNull"),
            "unique": orm_column_has_modifier(&property.value, "unique"),
            "index": orm_column_has_modifier(&property.value, "index"),
            "private": orm_column_has_modifier(&property.value, "private"),
            "softDelete": orm_column_has_modifier(&property.value, "softDelete"),
            "concurrencyToken": orm_column_has_modifier(&property.value, "concurrencyToken"),
            "default": orm_column_has_modifier(&property.value, "default"),
            "defaultNow": orm_column_has_modifier(&property.value, "defaultNow"),
            "generated": orm_column_has_modifier(&property.value, "generated"),
            "reference": crate::plan_emit::parse_reference(&column_expression),
        }));
    }
    Some(json!({
        "model": model,
        "name": table_name,
        "source": source_name,
        "columns": columns,
    }))
}

fn orm_column_type_from_expression<'a>(expression: &'a Expression<'a>) -> Option<&'a str> {
    let mut current = expression;
    loop {
        let Expression::CallExpression(call) = current else {
            return None;
        };
        let Expression::StaticMemberExpression(member) = &call.callee else {
            return None;
        };
        if let Expression::Identifier(object) = &member.object {
            if object.name.as_str() == "column" {
                let column_type = member.property.name.as_str();
                return if orm_column_type_supported(column_type) {
                    Some(column_type)
                } else {
                    None
                };
            }
        }
        current = &member.object;
    }
}

fn orm_column_type_supported(column_type: &str) -> bool {
    matches!(
        column_type,
        "text"
            | "string"
            | "int"
            | "integer"
            | "bigint"
            | "number"
            | "float"
            | "decimal"
            | "bool"
            | "boolean"
            | "uuid"
            | "instant"
            | "timestamp"
            | "date"
            | "json"
            | "blob"
            | "bytes"
            | "enum"
    )
}

fn orm_column_has_modifier(expression: &Expression<'_>, modifier: &str) -> bool {
    let mut current = expression;
    while let Expression::CallExpression(call) = current {
        let Expression::StaticMemberExpression(member) = &call.callee else {
            return false;
        };
        if member.property.name.as_str() == modifier {
            return true;
        }
        current = &member.object;
    }
    false
}

fn orm_table_hint() -> &'static str {
    "Use:\n  const Users = table(\"users\", {\n    id: column.uuid().primaryKey(),\n    teamId: column.uuid().references(() => Teams.id),\n  });"
}

fn orm_relation_hint() -> &'static str {
    "Use:\n  relation(Users, ({ one, many }) => ({\n    team: one(Teams, {\n      local: Users.teamId,\n      foreign: Teams.id,\n    }),\n  }));"
}

fn orm_relation_metadata_call(
    path: &Path,
    source: &str,
    statement: &ExpressionStatement<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let expression = &statement.expression;
    let Expression::CallExpression(call) = expression else {
        return Ok(false);
    };
    let Expression::Identifier(callee) = &call.callee else {
        return Ok(false);
    };
    if callee.name.as_str() != "relation" {
        return Ok(false);
    }
    if !state.orm_imported {
        return Ok(false);
    }
    if call.arguments.len() < 2 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ORM_RELATION",
            "ORM relation declarations require a table identifier and an inline callback",
        )
        .with_path(path)
        .with_span(call.span)
        .with_hint(orm_relation_hint()));
    }
    let Some(table_argument) = call.arguments.first() else {
        return Ok(false);
    };
    if !matches!(table_argument, Argument::Identifier(_)) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ORM_RELATION",
            "ORM relation declarations require a static table identifier",
        )
        .with_path(path)
        .with_span(argument_span(table_argument).unwrap_or(call.span))
        .with_hint(orm_relation_hint()));
    }
    let Some(callback_argument) = call.arguments.get(1) else {
        return Ok(false);
    };
    let Some(statement_source) = source_slice(source, statement.span) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ORM_RELATION",
            "ORM relation declaration source could not be extracted",
        )
        .with_path(path)
        .with_span(statement.span)
        .with_hint(orm_relation_hint()));
    };
    state
        .orm_metadata_sources
        .push((statement.span.start, statement_source.to_string()));
    if matches!(
        callback_argument,
        Argument::ArrowFunctionExpression(_) | Argument::FunctionExpression(_)
    ) {
        if let Some(callback_source) = source_slice(
            source,
            argument_span(callback_argument).unwrap_or(call.span),
        ) {
            if let Some(object_source) = crate::plan_emit::relation_object_source(&callback_source)
            {
                let Argument::Identifier(table_identifier) = table_argument else {
                    unreachable!("table argument shape checked above");
                };
                for part in crate::plan_emit::split_top_level_properties(object_source) {
                    let Some((relation_name, expression)) =
                        crate::plan_emit::parse_property_name(part)
                    else {
                        state.orm_extraction_partial = true;
                        continue;
                    };
                    let Some(mut relation) =
                        crate::plan_emit::parse_relation_definition(relation_name, expression)
                    else {
                        state.orm_extraction_partial = true;
                        continue;
                    };
                    if let Value::Object(ref mut object) = relation {
                        object.insert(
                            "tableModel".to_string(),
                            json!(table_identifier.name.as_str()),
                        );
                        object.insert("source".to_string(), json!(source_name_from_path(path)));
                    }
                    state.orm_relations.push(relation);
                }
            } else {
                state.orm_extraction_partial = true;
            }
        } else {
            state.orm_extraction_partial = true;
        }
    } else {
        state.orm_extraction_partial = true;
    }
    Ok(true)
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
    if chain.len() != 3
        || chain[1] != "services"
        || !(state.app_vars.contains(chain[0]) || state.builder_vars.contains(chain[0]))
    {
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
    let free_identifiers = service_factory_free_identifiers(factory_argument);
    let unsupported_capture = free_identifiers
        .iter()
        .find(|identifier| !service_factory_capture_is_emit_safe(identifier, state));
    if let Some(identifier) = unsupported_capture {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_SERVICE_REGISTRATION",
            format!(
                "service registration '{}' factory captures unsupported identifier '{}'",
                token, identifier
            ),
        )
        .with_path(path)
        .with_span(argument_span(factory_argument).unwrap_or(call.span))
        .with_hint("Use an inline factory that depends only on its scope parameter, emitted top-level helper functions, JavaScript globals, or local values."));
    }
    Ok(Some(ServiceRegistration {
        token: token.to_string(),
        lifetime,
        factory_source,
    }))
}

fn service_factory_capture_is_emit_safe(identifier: &str, state: &AppState) -> bool {
    if state.helper_sources.contains_key(identifier)
        && helper_source_is_safe_for_top_level(state.helper_effects.get(identifier))
    {
        return true;
    }
    if matches!(
        identifier,
        "Array"
            | "Boolean"
            | "Date"
            | "Error"
            | "Intl"
            | "JSON"
            | "Map"
            | "Math"
            | "Number"
            | "Object"
            | "Promise"
            | "RegExp"
            | "Set"
            | "String"
            | "TypeError"
            | "URL"
            | "URLSearchParams"
            | "console"
            | "globalThis"
            | "undefined"
    ) {
        return true;
    }
    matches!(
        identifier,
        "data" if state.data_imported
    ) || matches!(
        identifier,
        "sql" if state.sql_imported
    )
}

fn service_factory_free_identifiers(argument: &Argument<'_>) -> BTreeSet<String> {
    let mut scope = BTreeSet::new();
    match argument {
        Argument::ArrowFunctionExpression(function) => {
            collect_formal_parameter_bindings(&function.params, &mut scope);
            collect_function_body_bindings(&function.body.statements, &mut scope);
            let mut free = BTreeSet::new();
            collect_statement_list_identifier_references(
                &function.body.statements,
                &scope,
                &mut free,
            );
            free
        }
        Argument::FunctionExpression(function) => {
            collect_formal_parameter_bindings(&function.params, &mut scope);
            if let Some(body) = &function.body {
                collect_function_body_bindings(&body.statements, &mut scope);
                let mut free = BTreeSet::new();
                collect_statement_list_identifier_references(&body.statements, &scope, &mut free);
                free
            } else {
                BTreeSet::new()
            }
        }
        _ => BTreeSet::new(),
    }
}

fn collect_formal_parameter_bindings(
    parameters: &oxc_ast::ast::FormalParameters<'_>,
    scope: &mut BTreeSet<String>,
) {
    for parameter in &parameters.items {
        collect_binding_roots(&parameter.pattern, scope);
    }
}

fn collect_function_body_bindings(statements: &[Statement<'_>], scope: &mut BTreeSet<String>) {
    for statement in statements {
        match statement {
            Statement::VariableDeclaration(declaration) => {
                for declarator in &declaration.declarations {
                    collect_binding_roots(&declarator.id, scope);
                }
            }
            Statement::FunctionDeclaration(function) => {
                if let Some(identifier) = &function.id {
                    scope.insert(identifier.name.as_str().to_string());
                }
            }
            _ => {}
        }
    }
}

fn collect_statement_list_identifier_references(
    statements: &[Statement<'_>],
    scope: &BTreeSet<String>,
    free: &mut BTreeSet<String>,
) {
    for statement in statements {
        collect_statement_identifier_references(statement, scope, free);
    }
}

fn collect_statement_identifier_references(
    statement: &Statement<'_>,
    scope: &BTreeSet<String>,
    free: &mut BTreeSet<String>,
) {
    match statement {
        Statement::BlockStatement(block) => {
            collect_statement_list_identifier_references(&block.body, scope, free);
        }
        Statement::ExpressionStatement(statement) => {
            collect_expression_identifier_references(&statement.expression, scope, free);
        }
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                collect_expression_identifier_references(argument, scope, free);
            }
        }
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_expression_identifier_references(init, scope, free);
                }
            }
        }
        Statement::IfStatement(statement) => {
            collect_expression_identifier_references(&statement.test, scope, free);
            collect_statement_identifier_references(&statement.consequent, scope, free);
            if let Some(alternate) = &statement.alternate {
                collect_statement_identifier_references(alternate, scope, free);
            }
        }
        Statement::ThrowStatement(statement) => {
            collect_expression_identifier_references(&statement.argument, scope, free);
        }
        _ => {}
    }
}

fn collect_expression_identifier_references(
    expression: &Expression<'_>,
    scope: &BTreeSet<String>,
    free: &mut BTreeSet<String>,
) {
    match expression {
        Expression::Identifier(identifier) => {
            let name = identifier.name.as_str();
            if !scope.contains(name) && !service_factory_allowed_global(name) {
                free.insert(name.to_string());
            }
        }
        Expression::CallExpression(call) => {
            collect_expression_identifier_references(&call.callee, scope, free);
            for argument in &call.arguments {
                collect_argument_identifier_references(argument, scope, free);
            }
        }
        Expression::NewExpression(expression) => {
            collect_expression_identifier_references(&expression.callee, scope, free);
            for argument in &expression.arguments {
                collect_argument_identifier_references(argument, scope, free);
            }
        }
        Expression::AwaitExpression(expression) => {
            collect_expression_identifier_references(&expression.argument, scope, free);
        }
        Expression::ObjectExpression(object) => {
            for property in &object.properties {
                match property {
                    ObjectPropertyKind::ObjectProperty(property) => {
                        collect_expression_identifier_references(&property.value, scope, free);
                    }
                    ObjectPropertyKind::SpreadProperty(property) => {
                        collect_expression_identifier_references(&property.argument, scope, free);
                    }
                }
            }
        }
        Expression::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_identifier_references(element, scope, free);
            }
        }
        Expression::ParenthesizedExpression(expression) => {
            collect_expression_identifier_references(&expression.expression, scope, free);
        }
        Expression::StaticMemberExpression(expression) => {
            collect_expression_identifier_references(&expression.object, scope, free);
        }
        Expression::ComputedMemberExpression(expression) => {
            collect_expression_identifier_references(&expression.object, scope, free);
            collect_expression_identifier_references(&expression.expression, scope, free);
        }
        Expression::BinaryExpression(expression) => {
            collect_expression_identifier_references(&expression.left, scope, free);
            collect_expression_identifier_references(&expression.right, scope, free);
        }
        Expression::LogicalExpression(expression) => {
            collect_expression_identifier_references(&expression.left, scope, free);
            collect_expression_identifier_references(&expression.right, scope, free);
        }
        Expression::ConditionalExpression(expression) => {
            collect_expression_identifier_references(&expression.test, scope, free);
            collect_expression_identifier_references(&expression.consequent, scope, free);
            collect_expression_identifier_references(&expression.alternate, scope, free);
        }
        Expression::ArrowFunctionExpression(function) => {
            let mut nested_scope = scope.clone();
            collect_formal_parameter_bindings(&function.params, &mut nested_scope);
            collect_function_body_bindings(&function.body.statements, &mut nested_scope);
            collect_statement_list_identifier_references(
                &function.body.statements,
                &nested_scope,
                free,
            );
        }
        Expression::FunctionExpression(function) => {
            let mut nested_scope = scope.clone();
            collect_formal_parameter_bindings(&function.params, &mut nested_scope);
            if let Some(body) = &function.body {
                collect_function_body_bindings(&body.statements, &mut nested_scope);
                collect_statement_list_identifier_references(&body.statements, &nested_scope, free);
            }
        }
        _ => {}
    }
}

fn collect_argument_identifier_references(
    argument: &Argument<'_>,
    scope: &BTreeSet<String>,
    free: &mut BTreeSet<String>,
) {
    match argument {
        Argument::Identifier(identifier) => {
            let name = identifier.name.as_str();
            if !scope.contains(name) && !service_factory_allowed_global(name) {
                free.insert(name.to_string());
            }
        }
        Argument::ArrowFunctionExpression(function) => {
            let mut nested_scope = scope.clone();
            collect_formal_parameter_bindings(&function.params, &mut nested_scope);
            collect_function_body_bindings(&function.body.statements, &mut nested_scope);
            collect_statement_list_identifier_references(
                &function.body.statements,
                &nested_scope,
                free,
            );
        }
        Argument::FunctionExpression(function) => {
            let mut nested_scope = scope.clone();
            collect_formal_parameter_bindings(&function.params, &mut nested_scope);
            if let Some(body) = &function.body {
                collect_function_body_bindings(&body.statements, &mut nested_scope);
                collect_statement_list_identifier_references(&body.statements, &nested_scope, free);
            }
        }
        Argument::ObjectExpression(object) => {
            for property in &object.properties {
                match property {
                    ObjectPropertyKind::ObjectProperty(property) => {
                        collect_expression_identifier_references(&property.value, scope, free);
                    }
                    ObjectPropertyKind::SpreadProperty(property) => {
                        collect_expression_identifier_references(&property.argument, scope, free);
                    }
                }
            }
        }
        Argument::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_identifier_references(element, scope, free);
            }
        }
        _ => {
            if let Some(expression) = argument.as_expression() {
                collect_expression_identifier_references(expression, scope, free);
            }
        }
    }
}

fn collect_array_element_identifier_references(
    element: &ArrayExpressionElement<'_>,
    scope: &BTreeSet<String>,
    free: &mut BTreeSet<String>,
) {
    if let Some(expression) = element.as_expression() {
        collect_expression_identifier_references(expression, scope, free);
    }
}

fn service_factory_allowed_global(name: &str) -> bool {
    matches!(
        name,
        "Array"
            | "ArrayBuffer"
            | "Boolean"
            | "Date"
            | "Error"
            | "JSON"
            | "Map"
            | "Math"
            | "Number"
            | "Object"
            | "Promise"
            | "Set"
            | "String"
            | "Symbol"
            | "TypeError"
            | "Uint8Array"
            | "WeakMap"
            | "WeakSet"
            | "undefined"
    )
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

#[derive(Debug, Clone)]
struct ImportedHelper {
    name: String,
    source: String,
    summary: FunctionEffectSummary,
}

fn extract_relative_helper_import(
    graph: &mut ModuleGraph,
    imported: &ImportedModule,
) -> Result<Vec<ImportedHelper>, Diagnostic> {
    if graph.visiting.contains(&imported.path) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_CIRCULAR_IMPORT",
            "circular relative imports are not supported",
        )
        .with_path(&imported.path)
        .with_span(imported.span)
        .with_hint("Keep relative helper modules acyclic."));
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
        graph.visiting.remove(&imported.path);
        return Err(Diagnostic::new(
            "SLOPPYC_E_PARSE",
            format!("failed to parse module: {error}"),
        )
        .with_path(&imported.path));
    }

    let mut helpers = Vec::<ImportedHelper>::new();
    let mut helper_sources = BTreeMap::<String, String>::new();
    let mut helper_effects = BTreeMap::<String, FunctionEffectSummary>::new();

    for statement in &parsed.program.body {
        let Statement::ImportDeclaration(import) = statement else {
            continue;
        };
        let import_source = import.source.value.as_str();
        if import_source == "sloppy" {
            validate_module_sloppy_root_import(&imported.path, import)?;
            mark_sloppy_root_runtime_usage(graph, import);
            continue;
        }
        if import_source == "sloppy/time" {
            validate_module_sloppy_time_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_time_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/data" {
            validate_module_sloppy_data_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                mark_sloppy_data_runtime_usage(graph, import);
            }
            continue;
        }
        if import_source == "sloppy/fs" {
            validate_module_sloppy_fs_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_fs_runtime = true;
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
        if import_source == "sloppy/cache" {
            validate_module_sloppy_cache_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_cache_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/net" {
            validate_module_sloppy_net_import(&imported.path, import)?;
            if let Some(specifiers) = &import.specifiers {
                for specifier in specifiers {
                    if let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier {
                        if import_specifier_is_runtime_value(import, specifier) {
                            mark_sloppy_net_runtime_usage(
                                &mut graph.uses_net_runtime,
                                &mut graph.uses_http_client_runtime,
                                specifier.imported.name().as_str(),
                            );
                        }
                    }
                }
            }
            continue;
        }
        if import_source == "sloppy/http" {
            validate_module_sloppy_http_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_http_client_runtime = true;
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
        if import_source == "sloppy/ffi" {
            validate_module_sloppy_ffi_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_ffi_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/providers/sqlite" {
            continue;
        }
        if import_source.starts_with("./") || import_source.starts_with("../") {
            let nested =
                resolve_relative_import(&imported.path, import_source).ok_or_else(|| {
                    Diagnostic::new(
                        "SLOPPYC_E_MISSING_RELATIVE_IMPORT",
                        format!("relative import \"{import_source}\" could not be resolved"),
                    )
                    .with_path(&imported.path)
                    .with_span(import.source.span)
                })?;
            if !resolver::stays_within_source_root(&nested, &graph.entry_dir) {
                graph.visiting.remove(&imported.path);
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_RELATIVE_IMPORT",
                    "relative imports must stay within the source root",
                )
                .with_path(&imported.path)
                .with_span(import.source.span));
            }
            graph.add_relative_dependency_import(&imported.path, import_source, &nested);
            if let Some(specifiers) = &import.specifiers {
                for specifier in specifiers {
                    let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
                        graph.visiting.remove(&imported.path);
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
                            format!("unsupported import specifier \"{import_source}\""),
                        )
                        .with_path(&imported.path)
                        .with_span(import.source.span));
                    };
                    let nested_import = ImportedModule {
                        local_name: specifier.local.name.as_str().to_string(),
                        export_name: specifier.imported.name().as_str().to_string(),
                        path: nested.clone(),
                        span: specifier.span,
                    };
                    for helper in extract_relative_helper_import(graph, &nested_import)? {
                        helper_sources
                            .entry(helper.name.clone())
                            .or_insert(helper.source.clone());
                        helper_effects
                            .entry(helper.name.clone())
                            .or_insert(helper.summary.clone());
                        helpers.push(helper);
                    }
                    resolve_helper_effect_callgraph(&mut helper_effects);
                }
            }
            continue;
        }
        if let resolver::ImportKind::Package(package_resolution) =
            resolver::classify_import(&imported.path, import_source)
        {
            graph.add_package_record(&package_resolution);
            let package_id = program_module_id(
                graph,
                &package_resolution.entry,
                Some(&package_resolution.name),
            );
            graph.add_dependency_module(
                package_id.clone(),
                package_id.clone(),
                package_resolution.format,
                Some(package_resolution.name.clone()),
                Some(resolver::normalized_artifact_id(
                    &imported.path,
                    &graph.entry_dir,
                )),
            );
            graph.add_package_dependency_import(&imported.path, import_source, &package_id);
            if let Some(specifiers) = &import.specifiers {
                for specifier in specifiers {
                    let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
                        graph.visiting.remove(&imported.path);
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
                            format!("unsupported import specifier \"{import_source}\""),
                        )
                        .with_path(&imported.path)
                        .with_span(import.source.span));
                    };
                    let package_import = ImportedModule {
                        local_name: specifier.local.name.as_str().to_string(),
                        export_name: specifier.imported.name().as_str().to_string(),
                        path: package_resolution.entry.clone(),
                        span: specifier.span,
                    };
                    for helper in extract_relative_helper_import(graph, &package_import)? {
                        helper_sources
                            .entry(helper.name.clone())
                            .or_insert(helper.source.clone());
                        helper_effects
                            .entry(helper.name.clone())
                            .or_insert(helper.summary.clone());
                        helpers.push(helper);
                    }
                    resolve_helper_effect_callgraph(&mut helper_effects);
                }
            }
            continue;
        }
        graph.visiting.remove(&imported.path);
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
            format!("unsupported import specifier \"{import_source}\""),
        )
        .with_path(&imported.path)
        .with_span(import.source.span));
    }

    let mut found = None::<ImportedHelper>;
    for statement in &parsed.program.body {
        let Statement::ExportNamedDeclaration(export) = statement else {
            continue;
        };
        match &export.declaration {
            Some(Declaration::FunctionDeclaration(function)) => {
                let Some(identifier) = &function.id else {
                    continue;
                };
                let export_name = identifier.name.as_str();
                if export_name != imported.export_name {
                    continue;
                }
                let Some(source_text) = source_slice(&source, function.span) else {
                    graph.visiting.remove(&imported.path);
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HELPER",
                        "helper source could not be extracted",
                    )
                    .with_path(&imported.path)
                    .with_span(function.span));
                };
                let summary = helper_effects_from_function(
                    function,
                    &BTreeMap::new(),
                    &helper_effects,
                    &source,
                    &source_name,
                );
                found = Some(imported_helper_with_alias(
                    imported,
                    export_name,
                    source_text,
                    summary,
                ));
                break;
            }
            Some(Declaration::VariableDeclaration(declaration)) => {
                for declarator in &declaration.declarations {
                    let Some(export_name) = binding_identifier(&declarator.id) else {
                        continue;
                    };
                    if export_name != imported.export_name {
                        continue;
                    }
                    let Some(init) = &declarator.init else {
                        continue;
                    };
                    let Some(init_source) = source_slice(&source, init.span()) else {
                        graph.visiting.remove(&imported.path);
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_HELPER",
                            "helper source could not be extracted",
                        )
                        .with_path(&imported.path)
                        .with_span(init.span()));
                    };
                    let source_text = format!("const {export_name} = {init_source};");
                    let summary = helper_effects_from_initializer(
                        init,
                        &BTreeMap::new(),
                        &helper_effects,
                        &source,
                        &source_name,
                    );
                    found = Some(imported_helper_with_alias(
                        imported,
                        export_name,
                        source_text,
                        summary,
                    ));
                    break;
                }
            }
            _ => {}
        }
        if found.is_some() {
            break;
        }
    }

    graph.visiting.remove(&imported.path);
    let Some(helper) = found else {
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
    helpers.push(helper);
    Ok(helpers)
}

fn imported_helper_with_alias(
    imported: &ImportedModule,
    export_name: &str,
    mut source: String,
    summary: FunctionEffectSummary,
) -> ImportedHelper {
    if imported.local_name != export_name {
        source.push_str(&format!("\nconst {} = {export_name};", imported.local_name));
    }
    ImportedHelper {
        name: imported.local_name.clone(),
        source,
        summary,
    }
}

fn extract_relative_module(
    graph: &mut ModuleGraph,
    imported: &ImportedModule,
    mut metrics: Option<&mut CompileMetrics>,
) -> Result<CachedModuleExport, Diagnostic> {
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

    let read_start = Instant::now();
    let source = fs::read_to_string(&imported.path).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_INPUT",
            format!("failed to read imported module: {error}"),
        )
        .with_path(&imported.path)
    })?;
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("readInputMs", read_start.elapsed());
    }
    let source_name = graph.record_source(&imported.path, &source);
    graph.noncrypto_hash_security_context_visible |=
        noncrypto_hash_security_context_visible(&source);
    graph.checksum_security_context_visible |= checksum_security_context_visible(&source);
    let source_type = source_type_for_path(&imported.path, ParseContext::Module)?;
    let parse_start = Instant::now();
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, &source, source_type).parse();
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("parseModulesMs", parse_start.elapsed());
    }
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_PARSE",
            format!("failed to parse module: {error}"),
        )
        .with_path(&imported.path));
    }
    extract_ffi_declarations_from_statements(
        &imported.path,
        &source,
        &source_name,
        &parsed.program.body,
        &mut graph.ffi_libraries,
        &mut graph.ffi_structs,
    )?;

    let mut exports = BTreeMap::<String, CachedModuleExport>::new();
    let mut duplicate_exports = BTreeSet::<String>::new();
    let extract_start = Instant::now();
    let mut module_results_imported = false;
    let mut imported_helper_sources = BTreeMap::<String, String>::new();
    let mut imported_helper_effects = BTreeMap::<String, FunctionEffectSummary>::new();
    for statement in &parsed.program.body {
        let Statement::ImportDeclaration(import) = statement else {
            continue;
        };
        let import_source = import.source.value.as_str();
        if import_source == "sloppy" {
            module_results_imported |= validate_module_sloppy_root_import(&imported.path, import)?;
            mark_sloppy_root_runtime_usage(graph, import);
            continue;
        }
        if import_source == "sloppy/time" {
            validate_module_sloppy_time_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_time_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/data" {
            validate_module_sloppy_data_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                mark_sloppy_data_runtime_usage(graph, import);
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
        if import_source == "sloppy/cache" {
            validate_module_sloppy_cache_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_cache_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/net" {
            validate_module_sloppy_net_import(&imported.path, import)?;
            if let Some(specifiers) = &import.specifiers {
                for specifier in specifiers {
                    if let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier {
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
        if import_source == "sloppy/http" {
            validate_module_sloppy_http_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_http_client_runtime = true;
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
        if import_source == "sloppy/ffi" {
            validate_module_sloppy_ffi_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_ffi_runtime = true;
            }
            continue;
        }
        if import_source.starts_with("./") || import_source.starts_with("../") {
            let nested =
                resolve_relative_import(&imported.path, import_source).ok_or_else(|| {
                    Diagnostic::new(
                        "SLOPPYC_E_MISSING_RELATIVE_IMPORT",
                        format!("relative import \"{import_source}\" could not be resolved"),
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
            if !resolver::stays_within_source_root(&nested, &graph.entry_dir) {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_RELATIVE_IMPORT",
                    "relative imports must stay within the source root",
                )
                .with_path(&imported.path)
                .with_span(import.source.span));
            }
            graph.add_relative_dependency_import(&imported.path, import_source, &nested);
            if let Some(specifiers) = &import.specifiers {
                for specifier in specifiers {
                    let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
                            format!("unsupported import specifier \"{import_source}\""),
                        )
                        .with_path(&imported.path)
                        .with_span(import.source.span));
                    };
                    let nested_import = ImportedModule {
                        local_name: specifier.local.name.as_str().to_string(),
                        export_name: specifier.imported.name().as_str().to_string(),
                        path: nested.clone(),
                        span: specifier.span,
                    };
                    for helper in extract_relative_helper_import(graph, &nested_import)? {
                        imported_helper_sources
                            .entry(helper.name.clone())
                            .or_insert(helper.source);
                        imported_helper_effects
                            .entry(helper.name)
                            .or_insert(helper.summary);
                    }
                    resolve_helper_effect_callgraph(&mut imported_helper_effects);
                }
            }
            continue;
        }
        match resolver::classify_import(&imported.path, import_source) {
            resolver::ImportKind::Package(package_resolution) => {
                graph.add_package_record(&package_resolution);
                let package_id = program_module_id(
                    graph,
                    &package_resolution.entry,
                    Some(&package_resolution.name),
                );
                graph.add_dependency_module(
                    package_id.clone(),
                    package_id.clone(),
                    package_resolution.format,
                    Some(package_resolution.name.clone()),
                    Some(resolver::normalized_artifact_id(
                        &imported.path,
                        &graph.entry_dir,
                    )),
                );
                graph.add_package_dependency_import(&imported.path, import_source, &package_id);
                if let Some(specifiers) = &import.specifiers {
                    for specifier in specifiers {
                        let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier
                        else {
                            return Err(Diagnostic::new(
                                "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
                                format!("unsupported import specifier \"{import_source}\""),
                            )
                            .with_path(&imported.path)
                            .with_span(import.source.span));
                        };
                        let package_import = ImportedModule {
                            local_name: specifier.local.name.as_str().to_string(),
                            export_name: specifier.imported.name().as_str().to_string(),
                            path: package_resolution.entry.clone(),
                            span: specifier.span,
                        };
                        for helper in extract_relative_helper_import(graph, &package_import)? {
                            imported_helper_sources
                                .entry(helper.name.clone())
                                .or_insert(helper.source);
                            imported_helper_effects
                                .entry(helper.name)
                                .or_insert(helper.summary);
                        }
                        resolve_helper_effect_callgraph(&mut imported_helper_effects);
                    }
                }
                continue;
            }
            resolver::ImportKind::NativeAddonUnsupported(package_resolution) => {
                graph.add_package_record(&package_resolution);
            }
            _ => {}
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

    for statement in &parsed.program.body {
        match statement {
            Statement::FunctionDeclaration(function) => {
                let Some(identifier) = &function.id else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                        "function module helper declarations must be named",
                    )
                    .with_path(&imported.path)
                    .with_span(function.span));
                };
                let Some(helper_source) = source_slice(&source, function.span) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HELPER",
                        "helper source could not be extracted",
                    )
                    .with_path(&imported.path)
                    .with_span(function.span));
                };
                let name = identifier.name.as_str().to_string();
                let summary = helper_effects_from_function(
                    function,
                    &BTreeMap::new(),
                    &imported_helper_effects,
                    &source,
                    &source_name,
                );
                imported_helper_sources.insert(name.clone(), helper_source);
                imported_helper_effects.insert(name, summary);
                resolve_helper_effect_callgraph(&mut imported_helper_effects);
            }
            Statement::VariableDeclaration(declaration) => {
                for declarator in &declaration.declarations {
                    let Some(init) = &declarator.init else {
                        continue;
                    };
                    if helper_initializer(init).is_none() {
                        continue;
                    }
                    let Some(name) = binding_identifier(&declarator.id) else {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                            "function module helper declarations must use simple identifiers",
                        )
                        .with_path(&imported.path)
                        .with_span(declarator.span));
                    };
                    let Some(init_source) = source_slice(&source, init.span()) else {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_HELPER",
                            "helper source could not be extracted",
                        )
                        .with_path(&imported.path)
                        .with_span(init.span()));
                    };
                    let helper_source = format!("const {name} = {init_source};");
                    let summary = helper_effects_from_initializer(
                        init,
                        &BTreeMap::new(),
                        &imported_helper_effects,
                        &source,
                        &source_name,
                    );
                    imported_helper_sources.insert(name.to_string(), helper_source);
                    imported_helper_effects.insert(name.to_string(), summary);
                    resolve_helper_effect_callgraph(&mut imported_helper_effects);
                }
            }
            _ => {}
        }
    }

    for statement in &parsed.program.body {
        match statement {
            Statement::ImportDeclaration(_) => {}
            Statement::FunctionDeclaration(_) => {}
            Statement::VariableDeclaration(declaration)
                if module_variable_declaration_is_helper(declaration) => {}
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
                let module_export = extract_module_function_routes(
                    &imported.path,
                    &source,
                    &source_name,
                    export_name,
                    function,
                    &imported_helper_sources,
                    &imported_helper_effects,
                )?;
                if !module_results_imported {
                    if let Some(route) = module_export
                        .routes
                        .iter()
                        .find(|route| route.handler.requires_results_import)
                    {
                        return Err(missing_results_import_diagnostic(
                            &imported.path,
                            route.handler.span,
                        ));
                    }
                }
                if exports
                    .insert(export_name.to_string(), module_export)
                    .is_some()
                {
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
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("extractMs", extract_start.elapsed());
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

fn module_variable_declaration_is_helper(declaration: &VariableDeclaration<'_>) -> bool {
    !declaration.declarations.is_empty()
        && declaration.declarations.iter().all(|declarator| {
            binding_identifier(&declarator.id).is_some()
                && declarator
                    .init
                    .as_ref()
                    .is_some_and(|init| helper_initializer(init).is_some())
        })
}

fn cached_module_routes(
    module: &CachedModule,
    imported: &ImportedModule,
) -> Result<CachedModuleExport, Diagnostic> {
    if module.duplicate_exports.contains(&imported.export_name) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_DUPLICATE_EXPORT",
            format!("module exports \"{}\" more than once", imported.export_name),
        )
        .with_path(&imported.path)
        .with_span(imported.span));
    }
    let Some(module_export) = module.exports.get(&imported.export_name) else {
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
    Ok(module_export.clone())
}

fn extract_module_function_routes(
    path: &Path,
    source: &str,
    source_name: &str,
    module_name: &str,
    function: &oxc_ast::ast::Function<'_>,
    imported_helper_sources: &BTreeMap<String, String>,
    imported_helper_effects: &BTreeMap<String, FunctionEffectSummary>,
) -> Result<CachedModuleExport, Diagnostic> {
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

    let mut groups = BTreeMap::<String, RouteGroupState>::new();
    let mut providers = BTreeMap::<String, ProviderBinding>::new();
    let mut helper_sources = imported_helper_sources.clone();
    let mut helper_effects = imported_helper_effects.clone();
    let schema_names = collect_schema_declaration_names(&body.statements);
    let mut schemas = Vec::new();
    let mut routes = Vec::new();

    for statement in &body.statements {
        match statement {
            Statement::FunctionDeclaration(function) => {
                let Some(identifier) = &function.id else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                        "function module helper declarations must be named",
                    )
                    .with_path(path)
                    .with_span(function.span));
                };
                let Some(helper_source) = source_slice(source, function.span) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                        "function module helper source could not be extracted",
                    )
                    .with_path(path)
                    .with_span(function.span));
                };
                let name = identifier.name.as_str().to_string();
                let summary = helper_effects_from_function(
                    function,
                    &providers,
                    &helper_effects,
                    source,
                    source_name,
                );
                helper_sources.insert(name.clone(), helper_source);
                helper_effects.insert(name, summary);
                resolve_helper_effect_callgraph(&mut helper_effects);
            }
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
                    } else if schema_names.contains(name) {
                        if let Some(schema) = schema_declaration(
                            path,
                            source,
                            source_name,
                            name,
                            init,
                            &schema_names,
                        )? {
                            schemas.push(schema);
                        }
                    } else if let Some((receiver, prefix, metadata)) = app_group_call(init)? {
                        let full_prefix = if receiver == app_name {
                            prefix.to_string()
                        } else if let Some(parent) = groups.get(receiver) {
                            join_route_patterns(&parent.prefix, prefix)
                        } else {
                            return Err(Diagnostic::new(
                                "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                                "function module route groups must be created from the module app parameter or another module route group",
                            )
                            .with_path(path)
                            .with_span(init.span()));
                        };
                        let mut tags = if receiver == app_name {
                            Vec::new()
                        } else {
                            groups
                                .get(receiver)
                                .map(|parent| parent.tags.clone())
                                .unwrap_or_default()
                        };
                        let auth = metadata.auth.or_else(|| {
                            groups.get(receiver).and_then(|parent| parent.auth.clone())
                        });
                        tags.extend(metadata.tags);
                        groups.insert(
                            name.to_string(),
                            RouteGroupState {
                                prefix: full_prefix,
                                tags,
                                auth,
                                middleware: Vec::new(),
                            },
                        );
                    } else if helper_initializer(init).is_some() {
                        let Some(init_source) = source_slice(source, init.span()) else {
                            return Err(Diagnostic::new(
                                "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                                "function module helper source could not be extracted",
                            )
                            .with_path(path)
                            .with_span(init.span()));
                        };
                        let helper_source = format!("const {name} = {init_source};");
                        let summary = helper_effects_from_initializer(
                            init,
                            &providers,
                            &helper_effects,
                            source,
                            source_name,
                        );
                        helper_sources.insert(name.to_string(), helper_source);
                        helper_effects.insert(name.to_string(), summary);
                        resolve_helper_effect_callgraph(&mut helper_effects);
                    }
                }
            }
            Statement::ExpressionStatement(statement) => {
                if let Expression::CallExpression(call) = &statement.expression {
                    if let Some((receiver, property)) = static_member_name(&call.callee) {
                        if groups.contains_key(receiver) {
                            let requirement = match property {
                                "requireAuth" | "requiresAuth" => Some(
                                    auth_requirement_from_call(call)
                                        .map_err(|diagnostic| diagnostic.with_path(path))?,
                                ),
                                "allowAnonymous" => Some(anonymous_auth_requirement()),
                                _ => None,
                            };
                            if let Some(requirement) = requirement {
                                if let Some(group) = groups.get_mut(receiver) {
                                    group.auth = Some(requirement);
                                }
                                continue;
                            }
                        }
                    }
                }
                let mut module_app_state = AppState::new();
                module_app_state.app_vars.insert(app_name.to_string());
                if let Some(mut health_routes) = app_map_health_checks_call(
                    path,
                    source,
                    source_name,
                    &statement.expression,
                    &module_app_state,
                )? {
                    for route in &mut health_routes {
                        route.module = Some(module_name.to_string());
                    }
                    routes.extend(health_routes);
                    continue;
                }
                if let Some(mut health_routes) = app_health_expose_call(
                    path,
                    source,
                    source_name,
                    &statement.expression,
                    &module_app_state,
                )? {
                    for route in &mut health_routes {
                        route.module = Some(module_name.to_string());
                    }
                    routes.extend(health_routes);
                    continue;
                }
                if let Some(mut management_routes) = app_management_call(
                    path,
                    source,
                    source_name,
                    &statement.expression,
                    &module_app_state,
                )? {
                    for route in &mut management_routes {
                        route.module = Some(module_name.to_string());
                    }
                    routes.extend(management_routes);
                    continue;
                }
                let (route_expr, fluent_metadata) = route_metadata_chain(&statement.expression)
                    .map_err(|diagnostic| diagnostic.with_path(path))?;
                let static_strings = StaticStringEnv::default();
                let Some((receiver, method, kind, pattern, route_metadata, handler_arg)) =
                    route_call_parts(route_expr, source, &static_strings)
                        .map_err(|diagnostic| diagnostic.with_path(path))?
                else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                        "function modules support provider lookup, route groups, and literal routes only",
                    )
                    .with_path(path)
                    .with_span(statement.span));
                };
                let (full_pattern, mut tags, inherited_auth) = if receiver == app_name {
                    (pattern.clone(), Vec::new(), None)
                } else if let Some(group) = groups.get(receiver) {
                    (
                        join_route_patterns(&group.prefix, &pattern),
                        group.tags.clone(),
                        group.auth.clone(),
                    )
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

                let handler_context = HandlerExtractionContext {
                    route_pattern: &full_pattern,
                    route_kind: kind,
                    source,
                    source_name,
                    allow_data_handler_body: !providers.is_empty(),
                    schema_names: &schema_names,
                    provider_bindings: &providers,
                    helper_effects: &helper_effects,
                };
                validate_handler_body_validate_schema_references(path, handler_arg, &schema_names)?;
                let Some(mut handler) = handler_from_argument(handler_arg, &handler_context) else {
                    return Err(handler_diagnostic(
                        path,
                        handler_arg,
                        &full_pattern,
                        &schema_names,
                        statement.span,
                    ));
                };
                wrap_realtime_handler(&mut handler, kind, &route_metadata);

                let referenced_helper_sources =
                    helper_sources_referenced_by_handler(&handler.emitted_source, &helper_sources);
                if !handler.effects.is_empty()
                    || !handler_context.helper_effects.is_empty()
                    || !referenced_helper_sources.is_empty()
                {
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
                    handler.emitted_source = wrap_handler_with_providers_and_helpers(
                        &handler.emitted_source,
                        &used_providers,
                        &referenced_helper_sources,
                        handler.is_async,
                    );
                }
                apply_route_schema_metadata(
                    path,
                    statement.span,
                    &schema_names,
                    &fluent_metadata,
                    &mut handler,
                )?;
                let contract_metadata = merged_route_metadata(&route_metadata, &fluent_metadata);
                tags.extend(contract_metadata.tags.clone());
                let auth = contract_metadata.auth.clone().or(inherited_auth);
                if let Some(requirement) = &auth {
                    handler.emitted_source =
                        wrap_handler_with_auth(&handler.emitted_source, requirement);
                    handler.is_async = true;
                }
                routes.push(Route {
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
                    docs: None,
                    health: None,
                    middleware: Vec::new(),
                    auth,
                    cors: None,
                    cors_preflight: false,
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
    Ok(CachedModuleExport { routes, schemas })
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

fn provider_has_generated_runtime_bridge(binding: &ProviderBinding) -> bool {
    binding.capability_kind == "database" && binding.provider == "sqlite"
}

fn helper_sources_referenced_by_handler(
    handler_source: &str,
    helper_sources: &BTreeMap<String, String>,
) -> Vec<String> {
    let mut selected = BTreeSet::<String>::new();
    let mut pending_sources = vec![handler_source.to_string()];
    while let Some(source) = pending_sources.pop() {
        for (name, helper_source) in helper_sources {
            if selected.contains(name) || !source_contains_identifier(&source, name) {
                continue;
            }
            selected.insert(name.clone());
            pending_sources.push(helper_source.clone());
        }
    }
    selected
        .into_iter()
        .filter_map(|name| helper_sources.get(&name).cloned())
        .collect()
}

fn source_contains_identifier(source: &str, identifier: &str) -> bool {
    if identifier.is_empty() {
        return false;
    }
    let source_bytes = source.as_bytes();
    let identifier_bytes = identifier.as_bytes();
    if identifier_bytes.len() > source_bytes.len() {
        return false;
    }
    let mut index = 0usize;
    while index <= source_bytes.len() - identifier_bytes.len() {
        match source_bytes[index] {
            b'\'' | b'"' | b'`' => {
                index = skip_js_quoted_literal(source_bytes, index);
                continue;
            }
            b'/' if source_bytes.get(index + 1) == Some(&b'/') => {
                index = skip_js_line_comment(source_bytes, index + 2);
                continue;
            }
            b'/' if source_bytes.get(index + 1) == Some(&b'*') => {
                index = skip_js_block_comment(source_bytes, index + 2);
                continue;
            }
            _ => {}
        }
        if &source_bytes[index..index + identifier_bytes.len()] != identifier_bytes {
            index += 1;
            continue;
        }
        let before = index
            .checked_sub(1)
            .and_then(|before| source_bytes.get(before));
        let after = source_bytes.get(index + identifier_bytes.len());
        if before.is_none_or(|byte| !is_js_identifier_byte(*byte))
            && after.is_none_or(|byte| !is_js_identifier_byte(*byte))
            && !identifier_match_is_object_key(source_bytes, index + identifier_bytes.len())
        {
            return true;
        }
        index += 1;
    }
    false
}

fn skip_js_quoted_literal(source: &[u8], start: usize) -> usize {
    let quote = source[start];
    let mut index = start + 1;
    while index < source.len() {
        if source[index] == b'\\' {
            index = (index + 2).min(source.len());
            continue;
        }
        if source[index] == quote {
            return index + 1;
        }
        index += 1;
    }
    source.len()
}

fn skip_js_line_comment(source: &[u8], mut index: usize) -> usize {
    while index < source.len() && source[index] != b'\n' && source[index] != b'\r' {
        index += 1;
    }
    index
}

fn skip_js_block_comment(source: &[u8], mut index: usize) -> usize {
    while index + 1 < source.len() {
        if source[index] == b'*' && source[index + 1] == b'/' {
            return index + 2;
        }
        index += 1;
    }
    source.len()
}

fn identifier_match_is_object_key(source: &[u8], mut index: usize) -> bool {
    while let Some(byte) = source.get(index) {
        if !byte.is_ascii_whitespace() {
            return *byte == b':';
        }
        index += 1;
    }
    false
}

fn is_js_identifier_byte(byte: u8) -> bool {
    byte == b'$' || byte == b'_' || byte.is_ascii_alphanumeric()
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

fn wrap_handler_with_auth(handler_source: &str, requirement: &AuthRequirementMetadata) -> String {
    let requirement_json = serde_json::to_string(&json!({
        "required": requirement.required,
        "allowAnonymous": requirement.allow_anonymous,
        "schemes": requirement.schemes,
        "scopes": requirement.scopes,
        "roles": requirement.roles,
        "claims": requirement.claims,
        "policy": requirement.policy
    }))
    .unwrap_or_else(|_| "{\"required\":true}".to_string());
    format!(
        "async function(ctx) {{ return await __sloppy_require_auth(ctx, {requirement_json}, () => ({handler_source})(ctx)); }}"
    )
}

fn force_auth_required_route_v8_dispatch(handler: &mut Handler) {
    if let Some(response) = &mut handler.response {
        response.native_body = None;
    }
    for response in &mut handler.responses {
        response.native_body = None;
    }
}

fn ensure_auth_required_route_request_context(handler: &mut Handler) {
    if handler
        .bindings
        .iter()
        .any(|binding| binding.kind == "context")
    {
        return;
    }
    handler.bindings.push(context_request_binding());
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

#[allow(clippy::too_many_arguments)]
fn route_call<'a>(
    expression: &'a Expression<'a>,
    source: &str,
    source_name: &str,
    allow_data_handler_body: bool,
    schema_names: &BTreeSet<String>,
    provider_bindings: &BTreeMap<String, ProviderBinding>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    static_strings: &StaticStringEnv,
) -> Result<Option<ExtractedRouteCall<'a>>, Diagnostic> {
    let Some((receiver, method, kind, pattern, _metadata, handler_arg)) =
        route_call_parts(expression, source, static_strings)?
    else {
        return Ok(None);
    };
    let context = HandlerExtractionContext {
        route_pattern: &pattern,
        route_kind: kind,
        source,
        source_name,
        allow_data_handler_body,
        schema_names,
        provider_bindings,
        helper_effects,
    };
    let Some(handler) = handler_from_argument(handler_arg, &context) else {
        return Ok(None);
    };
    Ok(Some((receiver, method, kind, pattern, handler)))
}

fn route_call_parts<'a>(
    expression: &'a Expression<'a>,
    source: &str,
    static_strings: &StaticStringEnv,
) -> Result<Option<RouteCallParts<'a>>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    let Some(method) = route_method_from_property(property) else {
        return Ok(None);
    };
    let kind = crate::slop_dsl::route_kind_from_property(property).unwrap_or("http");
    if property == "realtime" && !matches!(call.arguments.len(), 3 | 4) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_INVALID_REALTIME_ROUTE_ARGS",
            "Realtime routes must be declared as realtime(path, channel, handler[, options])",
        )
        .with_span(call.span));
    }
    if property != "realtime" && !matches!(call.arguments.len(), 2 | 3) {
        return Ok(None);
    }

    let Some(pattern) = call
        .arguments
        .first()
        .and_then(|argument| eval_string_argument(argument, static_strings))
    else {
        return Ok(None);
    };
    let (metadata, handler_arg) = if property == "realtime" {
        let channel_source =
            argument_span(&call.arguments[1]).map(|span| span_source(source, span).to_string());
        let realtime_options_source = if call.arguments.len() == 4 {
            argument_span(&call.arguments[3]).map(|span| span_source(source, span).to_string())
        } else {
            None
        };
        let metadata = RouteMetadata {
            websocket: if call.arguments.len() == 4 {
                Some(realtime_websocket_options_from_argument(
                    &call.arguments[3],
                )?)
            } else {
                None
            },
            realtime_channel_source: channel_source,
            realtime_options_source,
            ..Default::default()
        };
        (metadata, &call.arguments[2])
    } else if kind == "websocket" && call.arguments.len() == 3 {
        if route_handler_argument(&call.arguments[1]) {
            let metadata = RouteMetadata {
                websocket: Some(websocket_options_from_argument(&call.arguments[2])?),
                ..Default::default()
            };
            (metadata, &call.arguments[1])
        } else {
            let metadata = RouteMetadata {
                websocket: Some(websocket_options_from_argument(&call.arguments[1])?),
                ..Default::default()
            };
            (metadata, &call.arguments[2])
        }
    } else if kind == "websocket" {
        let metadata = RouteMetadata {
            websocket: Some(default_websocket_options()),
            ..Default::default()
        };
        (metadata, &call.arguments[1])
    } else if call.arguments.len() == 3 {
        (
            route_metadata_from_options_argument(&call.arguments[1])?,
            &call.arguments[2],
        )
    } else {
        (RouteMetadata::default(), &call.arguments[1])
    };
    Ok(Some((
        receiver,
        method,
        kind,
        pattern,
        metadata,
        handler_arg,
    )))
}

fn route_handler_argument(argument: &Argument<'_>) -> bool {
    matches!(
        argument,
        Argument::ArrowFunctionExpression(_) | Argument::FunctionExpression(_)
    )
}

fn default_websocket_options() -> WebSocketRouteOptionsMetadata {
    WebSocketRouteOptionsMetadata {
        protocols: Vec::new(),
        origins: None,
        max_message_bytes: Some(64 * 1024),
        max_send_queue_bytes: Some(1024 * 1024),
        heartbeat_ms: None,
        idle_timeout_ms: None,
        close_timeout_ms: Some(5000),
        slow_client_policy: Some("error".to_string()),
        compression: Some(false),
    }
}

fn merge_websocket_options(
    existing: &mut WebSocketRouteOptionsMetadata,
    incoming: WebSocketRouteOptionsMetadata,
) {
    if !incoming.protocols.is_empty() {
        existing.protocols = incoming.protocols;
    }
    if incoming.origins.is_some() {
        existing.origins = incoming.origins;
    }
    if incoming.max_message_bytes.is_some() {
        existing.max_message_bytes = incoming.max_message_bytes;
    }
    if incoming.max_send_queue_bytes.is_some() {
        existing.max_send_queue_bytes = incoming.max_send_queue_bytes;
    }
    if incoming.heartbeat_ms.is_some() {
        existing.heartbeat_ms = incoming.heartbeat_ms;
    }
    if incoming.idle_timeout_ms.is_some() {
        existing.idle_timeout_ms = incoming.idle_timeout_ms;
    }
    if incoming.close_timeout_ms.is_some() {
        existing.close_timeout_ms = incoming.close_timeout_ms;
    }
    if incoming.slow_client_policy.is_some() {
        existing.slow_client_policy = incoming.slow_client_policy;
    }
    if incoming.compression.is_some() {
        existing.compression = incoming.compression;
    }
}

fn websocket_options_from_argument(
    argument: &Argument<'_>,
) -> Result<WebSocketRouteOptionsMetadata, Diagnostic> {
    let Some(object) = object_argument(argument) else {
        let diagnostic = Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
            "WebSocket options must be a static object literal",
        );
        return Err(with_argument_span(diagnostic, argument));
    };
    let mut options = default_websocket_options();
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                "WebSocket options must use literal properties",
            )
            .with_span(object.span));
        };
        if property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                "WebSocket option names must be literal",
            )
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                "WebSocket option names must be literal",
            )
            .with_span(property.span));
        };
        match key {
            "protocols" => {
                options.protocols = websocket_protocols_from_expression(&property.value)?;
            }
            "origins" => {
                options.origins = Some(websocket_origins_from_expression(&property.value)?);
            }
            "maxMessageBytes" => {
                options.max_message_bytes = Some(websocket_positive_integer(&property.value, key)?);
            }
            "maxSendQueueBytes" => {
                options.max_send_queue_bytes =
                    Some(websocket_positive_integer(&property.value, key)?);
            }
            "heartbeatMs" => {
                options.heartbeat_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "idleTimeoutMs" => {
                options.idle_timeout_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "closeTimeoutMs" => {
                options.close_timeout_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "slowClientPolicy" => {
                let Some(policy) = expression_string_literal(&property.value) else {
                    return Err(websocket_options_diagnostic(
                        "WebSocket slowClientPolicy must be 'error' or 'close'",
                        property.value.span(),
                    ));
                };
                if policy != "error" && policy != "close" {
                    return Err(websocket_options_diagnostic(
                        "WebSocket slowClientPolicy must be 'error' or 'close'",
                        property.value.span(),
                    ));
                }
                options.slow_client_policy = Some(policy.to_string());
            }
            "compression" => {
                if !matches!(&property.value, Expression::BooleanLiteral(value) if !value.value) {
                    return Err(websocket_options_diagnostic(
                        "WebSocket compression must be the literal false",
                        property.value.span(),
                    ));
                }
                options.compression = Some(false);
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                    format!("unsupported WebSocket option '{key}'"),
                )
                .with_span(property.span));
            }
        }
    }
    Ok(options)
}

fn realtime_websocket_options_from_argument(
    argument: &Argument<'_>,
) -> Result<WebSocketRouteOptionsMetadata, Diagnostic> {
    let Some(object) = object_argument(argument) else {
        let diagnostic = Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REALTIME_OPTIONS",
            "Realtime route options must be a static object literal",
        );
        return Err(with_argument_span(diagnostic, argument));
    };
    let mut options = default_websocket_options();
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REALTIME_OPTIONS",
                "Realtime route options must use literal properties",
            )
            .with_span(object.span));
        };
        if property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REALTIME_OPTIONS",
                "Realtime route option names must be literal",
            )
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REALTIME_OPTIONS",
                "Realtime route option names must be literal",
            )
            .with_span(property.span));
        };
        match key {
            "websocket" => {
                let Expression::ObjectExpression(_) = &property.value else {
                    return Err(websocket_options_diagnostic(
                        "Realtime websocket option must be a static object literal",
                        property.value.span(),
                    ));
                };
                let websocket = websocket_options_from_expression_object(&property.value)?;
                merge_websocket_options(&mut options, websocket);
            }
            "protocols" => {
                options.protocols = websocket_protocols_from_expression(&property.value)?;
            }
            "origins" => {
                options.origins = Some(websocket_origins_from_expression(&property.value)?);
            }
            "maxMessageBytes" => {
                options.max_message_bytes = Some(websocket_positive_integer(&property.value, key)?);
            }
            "maxSendQueueBytes" => {
                options.max_send_queue_bytes =
                    Some(websocket_positive_integer(&property.value, key)?);
            }
            "heartbeatMs" => {
                options.heartbeat_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "idleTimeoutMs" => {
                options.idle_timeout_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "closeTimeoutMs" => {
                options.close_timeout_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "slowClientPolicy" => {
                let Some(policy) = expression_string_literal(&property.value) else {
                    return Err(websocket_options_diagnostic(
                        "WebSocket slowClientPolicy must be 'error' or 'close'",
                        property.value.span(),
                    ));
                };
                if policy != "error" && policy != "close" {
                    return Err(websocket_options_diagnostic(
                        "WebSocket slowClientPolicy must be 'error' or 'close'",
                        property.value.span(),
                    ));
                }
                options.slow_client_policy = Some(policy.to_string());
            }
            "compression" => {
                if !matches!(&property.value, Expression::BooleanLiteral(value) if !value.value) {
                    return Err(websocket_options_diagnostic(
                        "WebSocket compression must be the literal false",
                        property.value.span(),
                    ));
                }
                options.compression = Some(false);
            }
            "presence"
            | "backplane"
            | "unknownEventPolicy"
            | "validationFailurePolicy"
            | "handlerErrorPolicy" => {}
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_REALTIME_OPTIONS",
                    format!("unsupported Realtime route option '{key}'"),
                )
                .with_span(property.span));
            }
        }
    }
    Ok(options)
}

fn websocket_options_from_expression_object(
    expression: &Expression<'_>,
) -> Result<WebSocketRouteOptionsMetadata, Diagnostic> {
    let Expression::ObjectExpression(object) = expression else {
        return Err(websocket_options_diagnostic(
            "WebSocket options must be a static object literal",
            expression.span(),
        ));
    };
    let mut options = default_websocket_options();
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                "WebSocket options must use literal properties",
            )
            .with_span(object.span));
        };
        if property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                "WebSocket option names must be literal",
            )
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                "WebSocket option names must be literal",
            )
            .with_span(property.span));
        };
        match key {
            "protocols" => {
                options.protocols = websocket_protocols_from_expression(&property.value)?;
            }
            "origins" => {
                options.origins = Some(websocket_origins_from_expression(&property.value)?);
            }
            "maxMessageBytes" => {
                options.max_message_bytes = Some(websocket_positive_integer(&property.value, key)?);
            }
            "maxSendQueueBytes" => {
                options.max_send_queue_bytes =
                    Some(websocket_positive_integer(&property.value, key)?);
            }
            "heartbeatMs" => {
                options.heartbeat_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "idleTimeoutMs" => {
                options.idle_timeout_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "closeTimeoutMs" => {
                options.close_timeout_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "slowClientPolicy" => {
                let Some(policy) = expression_string_literal(&property.value) else {
                    return Err(websocket_options_diagnostic(
                        "WebSocket slowClientPolicy must be 'error' or 'close'",
                        property.value.span(),
                    ));
                };
                if policy != "error" && policy != "close" {
                    return Err(websocket_options_diagnostic(
                        "WebSocket slowClientPolicy must be 'error' or 'close'",
                        property.value.span(),
                    ));
                }
                options.slow_client_policy = Some(policy.to_string());
            }
            "compression" => {
                if !matches!(&property.value, Expression::BooleanLiteral(value) if !value.value) {
                    return Err(websocket_options_diagnostic(
                        "WebSocket compression must be the literal false",
                        property.value.span(),
                    ));
                }
                options.compression = Some(false);
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                    format!("unsupported WebSocket option '{key}'"),
                )
                .with_span(property.span));
            }
        }
    }
    Ok(options)
}

fn websocket_protocols_from_expression(
    expression: &Expression<'_>,
) -> Result<Vec<String>, Diagnostic> {
    let Expression::ArrayExpression(array) = expression else {
        return Err(websocket_options_diagnostic(
            "WebSocket protocols must be a string literal array",
            expression.span(),
        ));
    };
    let mut protocols = Vec::new();
    for element in &array.elements {
        let ArrayExpressionElement::StringLiteral(value) = element else {
            return Err(websocket_options_diagnostic(
                "WebSocket protocols must contain only string literals",
                array.span,
            ));
        };
        let protocol = value.value.as_str();
        if !websocket_protocol_token_supported(protocol) {
            return Err(websocket_options_diagnostic(
                "WebSocket protocols must be non-empty WebSocket subprotocol tokens",
                value.span,
            ));
        }
        if !protocols.iter().any(|existing| existing == protocol) {
            protocols.push(protocol.to_string());
        }
    }
    Ok(protocols)
}

fn websocket_protocol_token_supported(value: &str) -> bool {
    !value.is_empty()
        && value.bytes().all(|byte| {
            byte.is_ascii_alphanumeric()
                || matches!(
                    byte,
                    b'!' | b'#'
                        | b'$'
                        | b'%'
                        | b'&'
                        | b'\''
                        | b'*'
                        | b'+'
                        | b'-'
                        | b'.'
                        | b'^'
                        | b'_'
                        | b'`'
                        | b'|'
                        | b'~'
                )
        })
}

fn websocket_origins_from_call(
    call: &CallExpression<'_>,
) -> Result<WebSocketOriginsMetadata, Diagnostic> {
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
            "allowedOrigins requires exactly one static origin argument",
        )
        .with_span(call.span));
    }
    websocket_origins_from_argument(&call.arguments[0])
}

fn websocket_origins_from_argument(
    argument: &Argument<'_>,
) -> Result<WebSocketOriginsMetadata, Diagnostic> {
    match argument {
        Argument::StringLiteral(value) => websocket_origin_string(value.value.as_str(), value.span),
        Argument::ArrayExpression(array) => websocket_origin_array(array),
        _ => {
            let diagnostic = Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                "WebSocket origins must be '*', a string literal, or a string literal array",
            );
            Err(with_argument_span(diagnostic, argument))
        }
    }
}

fn websocket_origins_from_expression(
    expression: &Expression<'_>,
) -> Result<WebSocketOriginsMetadata, Diagnostic> {
    match expression {
        Expression::StringLiteral(value) => {
            websocket_origin_string(value.value.as_str(), value.span)
        }
        Expression::ArrayExpression(array) => websocket_origin_array(array),
        Expression::ParenthesizedExpression(parenthesized) => {
            websocket_origins_from_expression(&parenthesized.expression)
        }
        _ => Err(websocket_options_diagnostic(
            "WebSocket origins must be '*', a string literal, or a string literal array",
            expression.span(),
        )),
    }
}

fn websocket_origin_string(
    value: &str,
    span: Span,
) -> Result<WebSocketOriginsMetadata, Diagnostic> {
    if value.is_empty() {
        return Err(websocket_options_diagnostic(
            "WebSocket origins must be non-empty strings",
            span,
        ));
    }
    if value == "*" {
        return Ok(WebSocketOriginsMetadata::Any);
    }
    Ok(WebSocketOriginsMetadata::List(vec![value.to_string()]))
}

fn websocket_origin_array(
    array: &ArrayExpression<'_>,
) -> Result<WebSocketOriginsMetadata, Diagnostic> {
    let mut origins = Vec::new();
    for element in &array.elements {
        let ArrayExpressionElement::StringLiteral(value) = element else {
            return Err(websocket_options_diagnostic(
                "WebSocket origins must contain only string literals",
                array.span,
            ));
        };
        let origin = value.value.as_str();
        if origin.is_empty() {
            return Err(websocket_options_diagnostic(
                "WebSocket origins must be non-empty strings",
                value.span,
            ));
        }
        if origin == "*" && array.elements.len() != 1 {
            return Err(websocket_options_diagnostic(
                "WebSocket '*' origin cannot be combined with explicit origins",
                value.span,
            ));
        }
        if !origins.iter().any(|existing| existing == origin) {
            origins.push(origin.to_string());
        }
    }
    if origins.is_empty() {
        return Err(websocket_options_diagnostic(
            "WebSocket origins must not be an empty array",
            array.span,
        ));
    }
    if origins[0] == "*" {
        Ok(WebSocketOriginsMetadata::Any)
    } else {
        Ok(WebSocketOriginsMetadata::List(origins))
    }
}

fn websocket_positive_integer(expression: &Expression<'_>, name: &str) -> Result<u64, Diagnostic> {
    let Some(value) = expression_positive_integer(expression) else {
        return Err(websocket_options_diagnostic(
            format!("WebSocket {name} must be a positive integer literal"),
            expression.span(),
        ));
    };
    Ok(value)
}

fn expression_positive_integer(expression: &Expression<'_>) -> Option<u64> {
    match expression {
        Expression::NumericLiteral(value) => {
            if value.value > 0.0 && value.value.fract() == 0.0 && value.value <= u64::MAX as f64 {
                Some(value.value as u64)
            } else {
                None
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_positive_integer(&parenthesized.expression)
        }
        Expression::BinaryExpression(binary) => {
            let left = expression_positive_integer(&binary.left)?;
            let right = expression_positive_integer(&binary.right)?;
            match binary.operator {
                BinaryOperator::Addition => left.checked_add(right),
                BinaryOperator::Multiplication => left.checked_mul(right),
                _ => None,
            }
        }
        _ => None,
    }
}

fn websocket_options_diagnostic(message: impl Into<String>, span: Span) -> Diagnostic {
    Diagnostic::new("SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS", message.into()).with_span(span)
}

fn route_method_from_property(property: &str) -> Option<&'static str> {
    crate::slop_dsl::route_method_from_property(property)
}

fn route_metadata_chain<'a>(
    expression: &'a Expression<'a>,
) -> Result<(&'a Expression<'a>, RouteMetadata), Diagnostic> {
    let mut current = expression;
    let mut metadata = RouteMetadata::default();
    let mut auth_replaced_by_later_setter = false;
    let mut output_cache_setter_seen = false;
    while let Expression::CallExpression(call) = current {
        let Expression::StaticMemberExpression(member) = &call.callee else {
            break;
        };
        match member.property.name.as_str() {
            "withName" | "name" => {
                if metadata.name.is_none() {
                    metadata.name = Some(route_name_from_argument(call)?);
                }
                current = &member.object;
            }
            "summary" => {
                if metadata.summary.is_none() {
                    metadata.summary = Some(route_string_metadata_from_call(call, "summary")?);
                }
                current = &member.object;
            }
            "description" => {
                if metadata.description.is_none() {
                    metadata.description =
                        Some(route_string_metadata_from_call(call, "description")?);
                }
                current = &member.object;
            }
            "deprecated" => {
                if metadata.deprecated.is_none() {
                    metadata.deprecated = Some(route_deprecated_from_call(call)?);
                }
                current = &member.object;
            }
            "requireAuth" | "requiresAuth" | "security" => {
                let requirement = auth_requirement_from_call(call)?;
                if metadata.auth.is_none() {
                    metadata.auth = Some(requirement);
                    auth_replaced_by_later_setter = true;
                } else if !auth_replaced_by_later_setter {
                    merge_auth_requirement(&mut metadata.auth, requirement);
                }
                current = &member.object;
            }
            "allowAnonymous" => {
                let requirement = anonymous_auth_requirement();
                if metadata.auth.is_none() {
                    metadata.auth = Some(requirement);
                    auth_replaced_by_later_setter = true;
                } else if !auth_replaced_by_later_setter {
                    merge_auth_requirement(&mut metadata.auth, requirement);
                }
                current = &member.object;
            }
            "authorize" => {
                let policy = route_name_from_argument(call)?;
                if !auth_replaced_by_later_setter {
                    merge_auth_requirement(
                        &mut metadata.auth,
                        AuthRequirementMetadata {
                            required: true,
                            policy: Some(policy),
                            ..AuthRequirementMetadata::default()
                        },
                    );
                }
                current = &member.object;
            }
            "requiresScope" => {
                let scopes = route_tags_from_arguments(call)?;
                if !auth_replaced_by_later_setter {
                    merge_auth_requirement(
                        &mut metadata.auth,
                        AuthRequirementMetadata {
                            required: true,
                            scopes,
                            ..AuthRequirementMetadata::default()
                        },
                    );
                }
                current = &member.object;
            }
            "requiresRole" => {
                let roles = route_tags_from_arguments(call)?;
                if !auth_replaced_by_later_setter {
                    merge_auth_requirement(
                        &mut metadata.auth,
                        AuthRequirementMetadata {
                            required: true,
                            roles,
                            ..AuthRequirementMetadata::default()
                        },
                    );
                }
                current = &member.object;
            }
            "allowedOrigins" => {
                let origins = websocket_origins_from_call(call)?;
                let mut websocket = metadata.websocket.unwrap_or_default();
                websocket.origins = Some(origins);
                metadata.websocket = Some(websocket);
                current = &member.object;
            }
            "accepts" => {
                if metadata.accepts_schema.is_none() {
                    metadata.accepts_schema = Some(route_schema_from_argument(call, "accepts", 2)?);
                }
                current = &member.object;
            }
            "returns" => {
                let (status, schema) = route_returns_from_call(call)?;
                if metadata.returns_status.is_none() {
                    metadata.returns_status = status;
                }
                if metadata.returns_schema.is_none() {
                    metadata.returns_schema = schema;
                }
                current = &member.object;
            }
            "tags" | "withTags" => {
                metadata.tags.extend(route_tags_from_arguments(call)?);
                current = &member.object;
            }
            "consumes" => {
                metadata
                    .consumes
                    .push(route_string_metadata_from_call(call, "consumes")?);
                current = &member.object;
            }
            "produces" => {
                metadata
                    .produces
                    .push(route_string_metadata_from_call(call, "produces")?);
                current = &member.object;
            }
            "header" => {
                metadata
                    .headers
                    .push(route_contract_parameter_from_call(call, "header")?);
                current = &member.object;
            }
            "query" => {
                if metadata.query_schema.is_none() {
                    metadata.query_schema = Some(route_schema_from_argument(call, "query", 2)?);
                }
                current = &member.object;
            }
            "params" => {
                if metadata.params_schema.is_none() {
                    metadata.params_schema = Some(route_schema_from_argument(call, "params", 2)?);
                }
                current = &member.object;
            }
            "openapi" => {
                if metadata.openapi_override.is_none() {
                    metadata.openapi_override = Some(route_openapi_override_from_call(call)?);
                }
                current = &member.object;
            }
            "outputCache" => {
                if !output_cache_setter_seen {
                    metadata.output_cache =
                        Some(route_static_metadata_from_call(call, "outputCache")?);
                    output_cache_setter_seen = true;
                }
                current = &member.object;
            }
            "noOutputCache" => {
                if !output_cache_setter_seen {
                    metadata.output_cache = None;
                    output_cache_setter_seen = true;
                }
                current = &member.object;
            }
            "cacheHeaders" => {
                if metadata.cache_headers.is_none() {
                    metadata.cache_headers =
                        Some(route_static_metadata_from_call(call, "cacheHeaders")?);
                }
                current = &member.object;
            }
            _ => break,
        }
    }

    Ok((current, metadata))
}

fn merge_auth_requirement(
    existing: &mut Option<AuthRequirementMetadata>,
    incoming: AuthRequirementMetadata,
) {
    let Some(existing) = existing else {
        *existing = Some(incoming);
        return;
    };

    extend_unique(&mut existing.schemes, incoming.schemes);
    extend_unique(&mut existing.scopes, incoming.scopes);
    extend_unique(&mut existing.roles, incoming.roles);
    extend_unique(&mut existing.claims, incoming.claims);
    if existing.policy.is_none() {
        existing.policy = incoming.policy;
    }
}

fn anonymous_auth_requirement() -> AuthRequirementMetadata {
    AuthRequirementMetadata {
        required: false,
        allow_anonymous: true,
        ..AuthRequirementMetadata::default()
    }
}

fn extend_unique(values: &mut Vec<String>, incoming: Vec<String>) {
    for value in incoming {
        if !values.contains(&value) {
            values.push(value);
        }
    }
}

fn merged_route_metadata(options: &RouteMetadata, fluent: &RouteMetadata) -> RouteMetadata {
    let mut merged = options.clone();
    if fluent.name.is_some() {
        merged.name = fluent.name.clone();
    }
    if !fluent.tags.is_empty() {
        merged.tags.extend(fluent.tags.clone());
    }
    if fluent.auth.is_some() {
        merged.auth = fluent.auth.clone();
    }
    if fluent.accepts_schema.is_some() {
        merged.accepts_schema = fluent.accepts_schema.clone();
    }
    if fluent.returns_schema.is_some() {
        merged.returns_schema = fluent.returns_schema.clone();
    }
    if fluent.returns_status.is_some() {
        merged.returns_status = fluent.returns_status;
    }
    if fluent.summary.is_some() {
        merged.summary = fluent.summary.clone();
    }
    if fluent.description.is_some() {
        merged.description = fluent.description.clone();
    }
    if fluent.deprecated.is_some() {
        merged.deprecated = fluent.deprecated.clone();
    }
    if !fluent.consumes.is_empty() {
        merged.consumes.extend(fluent.consumes.clone());
    }
    if !fluent.produces.is_empty() {
        merged.produces.extend(fluent.produces.clone());
    }
    if !fluent.headers.is_empty() {
        merged.headers.extend(fluent.headers.clone());
    }
    if fluent.query_schema.is_some() {
        merged.query_schema = fluent.query_schema.clone();
    }
    if fluent.params_schema.is_some() {
        merged.params_schema = fluent.params_schema.clone();
    }
    if fluent.openapi_override.is_some() {
        merged.openapi_override = fluent.openapi_override.clone();
    }
    if fluent.output_cache.is_some() {
        merged.output_cache = fluent.output_cache.clone();
    }
    if fluent.cache_headers.is_some() {
        merged.cache_headers = fluent.cache_headers.clone();
    }
    if let Some(fluent_websocket) = &fluent.websocket {
        let mut websocket = merged.websocket.unwrap_or_default();
        merge_websocket_options(&mut websocket, fluent_websocket.clone());
        merged.websocket = Some(websocket);
    }
    merged
}

fn realtime_route_metadata_from_contract(
    metadata: &RouteMetadata,
) -> Option<RealtimeRouteMetadata> {
    metadata
        .realtime_channel_source
        .as_ref()
        .map(|channel_source| RealtimeRouteMetadata {
            channel_source: channel_source.clone(),
            options_source: metadata.realtime_options_source.clone(),
        })
}

fn auth_requirement_from_call(
    call: &CallExpression<'_>,
) -> Result<AuthRequirementMetadata, Diagnostic> {
    if call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            "requireAuth accepts at most one literal options object",
        )
        .with_span(call.span));
    }
    let mut requirement = AuthRequirementMetadata {
        required: true,
        ..AuthRequirementMetadata::default()
    };
    let Some(argument) = call.arguments.first() else {
        return Ok(requirement);
    };
    if let Some(scheme) = string_argument(argument) {
        requirement.schemes.push(scheme.to_string());
        return Ok(requirement);
    }
    if let Argument::ArrayExpression(array) = argument {
        requirement
            .schemes
            .extend(auth_string_list_from_array(array, "requireAuth schemes")?);
        return Ok(requirement);
    }
    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            "requireAuth options must be an object literal",
        )
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };
    auth_requirement_from_object(object)
}

fn auth_requirement_from_object(
    object: &ObjectExpression<'_>,
) -> Result<AuthRequirementMetadata, Diagnostic> {
    let mut requirement = AuthRequirementMetadata {
        required: true,
        ..AuthRequirementMetadata::default()
    };
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_AUTH",
                "requireAuth options must use literal properties",
            )
            .with_span(object.span));
        };
        if property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_AUTH",
                "requireAuth option names must be literal",
            )
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_AUTH",
                "requireAuth option names must be literal",
            )
            .with_span(property.span));
        };
        match key {
            "scheme" => {
                let Some(scheme) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "requireAuth scheme must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                requirement.schemes.push(scheme.to_string());
            }
            "schemes" => {
                requirement.schemes.extend(auth_string_list_from_expression(
                    &property.value,
                    "requireAuth schemes",
                )?);
            }
            "scope" => {
                let Some(scope) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "requireAuth scope must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                requirement.scopes.push(scope.to_string());
            }
            "scopes" => {
                requirement.scopes.extend(auth_string_list_from_expression(
                    &property.value,
                    "requireAuth scopes",
                )?);
            }
            "role" => {
                let Some(role) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "requireAuth role must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                requirement.roles.push(role.to_string());
            }
            "roles" => {
                requirement
                    .roles
                    .extend(route_tags_from_expression(&property.value)?);
            }
            "policy" => {
                let Some(policy) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "requireAuth policy must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                requirement.policy = Some(policy.to_string());
            }
            "claim" => {
                let Some(claim) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "requireAuth claim must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                requirement.claims.push(claim.to_string());
            }
            "allowAnonymous" => {
                if !matches!(&property.value, Expression::BooleanLiteral(value) if value.value) {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "requireAuth allowAnonymous must be the literal true when provided",
                    )
                    .with_span(property.value.span()));
                }
                requirement.required = false;
                requirement.allow_anonymous = true;
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_AUTH",
                    format!("unsupported requireAuth option '{key}'"),
                )
                .with_span(property.span));
            }
        }
    }
    Ok(requirement)
}

fn auth_string_list_from_expression(
    expression: &Expression<'_>,
    subject: &str,
) -> Result<Vec<String>, Diagnostic> {
    if let Some(value) = expression_string_literal(expression) {
        return Ok(vec![value.to_string()]);
    }
    let Expression::ArrayExpression(array) = expression else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            format!("{subject} must be a string literal or string literal array"),
        )
        .with_span(expression.span()));
    };
    auth_string_list_from_array(array, subject)
}

fn auth_string_list_from_array(
    array: &ArrayExpression<'_>,
    subject: &str,
) -> Result<Vec<String>, Diagnostic> {
    let mut values = Vec::new();
    for element in &array.elements {
        let ArrayExpressionElement::StringLiteral(value) = element else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_AUTH",
                format!("{subject} must contain only string literals"),
            )
            .with_span(array.span));
        };
        values.push(value.value.as_str().to_string());
    }
    Ok(values)
}

fn route_string_metadata_from_call(
    call: &CallExpression<'_>,
    method: &str,
) -> Result<String, Diagnostic> {
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            format!("{method} requires exactly one string literal"),
        )
        .with_span(call.span));
    }
    let Some(value) = call.arguments.first().and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            format!("{method} requires a string literal"),
        )
        .with_span(call.span));
    };
    if value.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            format!("{method} requires a non-empty string literal"),
        )
        .with_span(call.span));
    }
    Ok(value.to_string())
}

fn route_deprecated_from_call(call: &CallExpression<'_>) -> Result<String, Diagnostic> {
    if call.arguments.is_empty() {
        return Ok("true".to_string());
    }
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            "deprecated accepts at most one boolean or reason string",
        )
        .with_span(call.span));
    }
    match call.arguments.first() {
        Some(Argument::BooleanLiteral(value)) => {
            Ok(if value.value { "true" } else { "false" }.to_string())
        }
        Some(argument) => {
            let Some(reason) = string_argument(argument) else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                    "deprecated metadata must be a boolean literal or reason string",
                )
                .with_span(argument_span(argument).unwrap_or(call.span)));
            };
            Ok(reason.to_string())
        }
        None => Ok("true".to_string()),
    }
}

fn route_schema_from_argument(
    call: &CallExpression<'_>,
    method: &str,
    max_arguments: usize,
) -> Result<String, Diagnostic> {
    if call.arguments.is_empty() || call.arguments.len() > max_arguments {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_SCHEMA",
            format!("{method} requires a schema identifier"),
        )
        .with_span(call.span));
    }

    let Some(schema) = call.arguments.first().and_then(argument_identifier) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_SCHEMA",
            format!("{method} schema must be a static identifier"),
        )
        .with_span(call.span));
    };
    Ok(schema.to_string())
}

fn argument_status(argument: &Argument<'_>) -> Option<u16> {
    let Argument::NumericLiteral(literal) = argument else {
        return None;
    };
    let value = literal.value;
    if value.fract() == 0.0 && (100.0..=599.0).contains(&value) {
        Some(value as u16)
    } else {
        None
    }
}

fn route_returns_from_call(
    call: &CallExpression<'_>,
) -> Result<(Option<u16>, Option<String>), Diagnostic> {
    if call.arguments.is_empty() || call.arguments.len() > 3 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_SCHEMA",
            "returns requires a schema identifier or status plus schema identifier",
        )
        .with_span(call.span));
    }
    if let Some(status) = call.arguments.first().and_then(argument_status) {
        let schema = match call.arguments.get(1) {
            Some(argument) => {
                let Some(schema) = argument_identifier(argument) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_ROUTE_SCHEMA",
                        "returns status overload requires a static schema identifier when a schema is provided",
                    )
                    .with_span(argument_span(argument).unwrap_or(call.span)));
                };
                Some(schema.to_string())
            }
            None => None,
        };
        return Ok((Some(status), schema));
    }
    Ok((None, Some(route_schema_from_argument(call, "returns", 2)?)))
}

fn route_contract_parameter_from_call(
    call: &CallExpression<'_>,
    method: &str,
) -> Result<RouteContractParameter, Diagnostic> {
    if call.arguments.len() < 2 || call.arguments.len() > 3 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            format!("{method} requires a name and schema identifier"),
        )
        .with_span(call.span));
    }
    let Some(name) = call.arguments.first().and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            format!("{method} name must be a string literal"),
        )
        .with_span(call.span));
    };
    let Some(schema) = call.arguments.get(1).and_then(argument_identifier) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_SCHEMA",
            format!("{method} schema must be a static identifier"),
        )
        .with_span(call.span));
    };
    let options = call.arguments.get(2).and_then(object_argument);
    Ok(RouteContractParameter {
        name: name.to_string(),
        schema: schema.to_string(),
        required: options
            .and_then(|object| object_bool_property_value(object, "required"))
            .unwrap_or(false),
        description: options
            .and_then(|object| object_string_property_value(object, "description"))
            .map(ToString::to_string),
    })
}

fn route_openapi_override_from_call(call: &CallExpression<'_>) -> Result<Value, Diagnostic> {
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            "openapi requires exactly one object literal",
        )
        .with_span(call.span));
    }
    let Some(value) = call.arguments.first().and_then(argument_json_value) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            "openapi override must be a static JSON-compatible object literal",
        )
        .with_span(call.span));
    };
    if !value.is_object() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            "openapi override must be an object literal",
        )
        .with_span(call.span));
    }
    Ok(value)
}

fn route_static_metadata_from_call(
    call: &CallExpression<'_>,
    method: &str,
) -> Result<Value, Diagnostic> {
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            format!("{method} requires exactly one options object"),
        )
        .with_span(call.span));
    }
    let Some(value) = call.arguments.first().and_then(argument_json_value) else {
        return Ok(json!({
            "static": false
        }));
    };
    if value.is_object() {
        Ok(value)
    } else {
        Ok(json!({
            "static": false
        }))
    }
}

fn validate_route_schema_reference(
    path: &Path,
    span: Span,
    schema: &str,
    schema_names: &BTreeSet<String>,
) -> Result<(), Diagnostic> {
    if schema_names.contains(schema) {
        return Ok(());
    }
    Err(unresolved_schema_diagnostic(path, span, schema))
}

fn unresolved_schema_diagnostic(path: &Path, span: Span, schema: &str) -> Diagnostic {
    Diagnostic::new(
        "SLOPPYC_E_UNRESOLVED_SCHEMA",
        format!("route schema '{schema}' is not declared as a supported Sloppy schema"),
    )
    .with_path(path)
    .with_span(span)
    .with_hint("Declare the schema with Schema.object(...), Schema.array(...), or another supported Schema constructor in compiler-visible source.")
}

fn apply_route_schema_metadata(
    path: &Path,
    span: Span,
    schema_names: &BTreeSet<String>,
    metadata: &RouteMetadata,
    handler: &mut Handler,
) -> Result<(), Diagnostic> {
    let mut schema_metadata_conflict = false;
    if let Some(schema) = &metadata.accepts_schema {
        validate_route_schema_reference(path, span, schema, schema_names)?;
        if let Some(binding) = handler
            .bindings
            .iter_mut()
            .find(|binding| binding.kind == "body.json")
        {
            schema_metadata_conflict |= apply_binding_schema(&mut binding.schema, schema);
        } else {
            handler.bindings.push(framework_binding(
                "body.json",
                None,
                Some(schema),
                None,
                span,
            ));
        }
    }

    if let Some(schema) = &metadata.returns_schema {
        validate_route_schema_reference(path, span, schema, schema_names)?;
        let mut applied = false;
        if let Some(response) = &mut handler.response {
            if response.kind == "json" {
                schema_metadata_conflict |= apply_response_schema(response, schema);
                applied = true;
            }
        }
        for response in &mut handler.responses {
            if response.kind == "json" {
                schema_metadata_conflict |= apply_response_schema(response, schema);
                applied = true;
            }
        }
        if !applied {
            let response = ResponseMetadata {
                helper: "returns".to_string(),
                status: metadata.returns_status.unwrap_or(200),
                kind: "json".to_string(),
                body_schema: Some(schema.clone()),
                native_body: None,
                source_name: None,
                source_text: None,
                span: Some(span),
                partial: false,
            };
            if handler.response.is_none() {
                handler.response = Some(response.clone());
            }
            handler.responses.push(response);
        }
    }
    for header in &metadata.headers {
        validate_route_schema_reference(path, span, &header.schema, schema_names)?;
    }
    if let Some(schema) = &metadata.query_schema {
        validate_route_schema_reference(path, span, schema, schema_names)?;
    }
    if let Some(schema) = &metadata.params_schema {
        validate_route_schema_reference(path, span, schema, schema_names)?;
    }
    handler.schema_metadata_conflict |= schema_metadata_conflict;
    Ok(())
}

fn apply_binding_schema(target: &mut Option<String>, schema: &str) -> bool {
    match target {
        None => {
            *target = Some(schema.to_string());
            false
        }
        Some(existing) if existing == schema => false,
        Some(_) => {
            *target = None;
            true
        }
    }
}

fn apply_response_schema(response: &mut ResponseMetadata, schema: &str) -> bool {
    match &mut response.body_schema {
        None => {
            response.body_schema = Some(schema.to_string());
            false
        }
        Some(existing) if existing == schema => false,
        Some(_) => {
            response.partial = true;
            true
        }
    }
}

fn duplicate_schema_diagnostic(path: &Path, span: Span, schema: &str) -> Diagnostic {
    Diagnostic::new(
        "SLOPPYC_E_DUPLICATE_SCHEMA",
        format!("schema '{schema}' is declared more than once"),
    )
    .with_path(path)
    .with_span(span)
    .with_hint("Use unique schema names across the entry module and imported function modules.")
}

fn route_name_from_argument(call: &CallExpression<'_>) -> Result<String, Diagnostic> {
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_NAME",
            "withName requires exactly one literal name",
        )
        .with_span(call.span));
    }

    let Some(name) = call.arguments.first().and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_NAME",
            "route names must be string literals",
        )
        .with_span(call.span));
    };
    if name.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_NAME",
            "route names must be non-empty string literals",
        )
        .with_span(call.span));
    }
    Ok(name.to_string())
}

fn route_metadata_from_options_argument(
    argument: &Argument<'_>,
) -> Result<RouteMetadata, Diagnostic> {
    let Some(object) = object_argument(argument) else {
        let diagnostic = Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            "route metadata options must be an object literal",
        );
        return Err(with_argument_span(diagnostic, argument));
    };
    let mut metadata = RouteMetadata::default();
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                "route metadata options must use literal properties",
            )
            .with_span(object.span));
        };
        if property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                "route metadata option names must be literal",
            )
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                "route metadata option names must be literal",
            )
            .with_span(property.span));
        };
        match key {
            "name" => {
                let Some(name) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_ROUTE_NAME",
                        "route option name must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                if name.is_empty() {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_ROUTE_NAME",
                        "route option name must be a non-empty string literal",
                    )
                    .with_span(property.value.span()));
                }
                metadata.name = Some(name.to_string());
            }
            "tags" => {
                metadata.tags = route_tags_from_expression(&property.value)?;
            }
            "summary" => {
                let Some(value) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                        "route option summary must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                metadata.summary = Some(value.to_string());
            }
            "description" => {
                let Some(value) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                        "route option description must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                metadata.description = Some(value.to_string());
            }
            "deprecated" => match &property.value {
                Expression::BooleanLiteral(value) => {
                    metadata.deprecated =
                        Some(if value.value { "true" } else { "false" }.to_string());
                }
                _ => {
                    let Some(value) = expression_string_literal(&property.value) else {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                            "route option deprecated must be a boolean or reason string",
                        )
                        .with_span(property.value.span()));
                    };
                    metadata.deprecated = Some(value.to_string());
                }
            },
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                    format!("unsupported route metadata option '{key}'"),
                )
                .with_span(property.span));
            }
        }
    }
    Ok(metadata)
}

fn route_tags_from_arguments(call: &CallExpression<'_>) -> Result<Vec<String>, Diagnostic> {
    let mut tags = Vec::new();
    for argument in &call.arguments {
        let Some(tag) = string_argument(argument) else {
            let diagnostic = Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                "group.withTags(...) arguments must be string literals",
            );
            return Err(with_argument_span(diagnostic, argument));
        };
        if tag.is_empty() {
            let diagnostic = Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                "route tags must be non-empty string literals",
            );
            return Err(with_argument_span(diagnostic, argument));
        }
        tags.push(tag.to_string());
    }
    Ok(tags)
}

fn route_tags_from_expression(expression: &Expression<'_>) -> Result<Vec<String>, Diagnostic> {
    let Expression::ArrayExpression(array) = expression else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            "route option tags must be a string literal array",
        )
        .with_span(expression.span()));
    };
    let mut tags = Vec::new();
    for element in &array.elements {
        let ArrayExpressionElement::StringLiteral(value) = element else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                "route option tags must contain only string literals",
            )
            .with_span(array.span));
        };
        let tag = value.value.as_str();
        if tag.is_empty() {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                "route tags must be non-empty string literals",
            )
            .with_span(value.span));
        }
        tags.push(tag.to_string());
    }
    Ok(tags)
}

fn expression_string_literal<'a>(expression: &'a Expression<'a>) -> Option<&'a str> {
    match expression {
        Expression::StringLiteral(value) => Some(value.value.as_str()),
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_string_literal(&parenthesized.expression)
        }
        _ => None,
    }
}

fn with_argument_span(diagnostic: Diagnostic, argument: &Argument<'_>) -> Diagnostic {
    if let Some(span) = argument_span(argument) {
        diagnostic.with_span(span)
    } else {
        diagnostic
    }
}

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
            let realtime_route = context.route_kind == "sse" || context.route_kind == "websocket";
            if handler_parameters_are_unsupported_for_kind(&function.params, context.route_kind)
                || arrow_has_typescript_syntax(function)
                || effects.unknown_provider_usage
                || (!realtime_route
                    && !context.allow_data_handler_body
                    && effects.effects.is_empty()
                    && effects.helper_calls.is_empty()
                    && !handler_body_is_supported_arrow(function, context.schema_names))
            {
                return None;
            }
            let schema_spans = handler_context_parameter_name(&function.params)
                .map(|ctx_name| {
                    body_json_schema_argument_spans_arrow(function, &ctx_name, context.schema_names)
                })
                .unwrap_or_default();
            let handler_source = source_slice(context.source, function.span)?;
            let handler_source = sanitize_handler_schema_references(
                handler_source,
                function.span.start,
                &schema_spans,
            );
            let responses = response_metadata_many_from_arrow(
                function,
                context.source_name,
                context.source,
                context.schema_names,
            );
            Some(Handler {
                source: handler_source.clone(),
                emitted_source: handler_source,
                span: function.span,
                requires_results_import: arrow_requires_results_import(function),
                is_async: function.r#async,
                runtime_deferred: false,
                source_name: context.source_name.to_string(),
                source_text: context.source.to_string(),
                source_map_line_offset: 0,
                source_map_column_offset: 0,
                bindings: request_bindings_from_arrow(function, context.schema_names),
                response: responses.first().cloned(),
                responses,
                effects: effects.effects,
                schema_metadata_conflict: false,
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
            let realtime_route = context.route_kind == "sse" || context.route_kind == "websocket";
            if handler_parameters_are_unsupported_for_kind(&function.params, context.route_kind)
                || function_has_typescript_syntax(function)
                || effects.unknown_provider_usage
                || (!realtime_route
                    && !context.allow_data_handler_body
                    && effects.effects.is_empty()
                    && effects.helper_calls.is_empty()
                    && !handler_body_is_supported_function(function, context.schema_names))
            {
                return None;
            }
            let schema_spans = handler_context_parameter_name(&function.params)
                .map(|ctx_name| {
                    body_json_schema_argument_spans_function(
                        function,
                        &ctx_name,
                        context.schema_names,
                    )
                })
                .unwrap_or_default();
            let handler_source = source_slice(context.source, function.span)?;
            let handler_source = sanitize_handler_schema_references(
                handler_source,
                function.span.start,
                &schema_spans,
            );
            let responses = response_metadata_many_from_function(
                function,
                context.source_name,
                context.source,
                context.schema_names,
            );
            Some(Handler {
                source: handler_source.clone(),
                emitted_source: handler_source,
                span: function.span,
                requires_results_import: function_requires_results_import(function),
                is_async: function.r#async,
                runtime_deferred: false,
                source_name: context.source_name.to_string(),
                source_text: context.source.to_string(),
                source_map_line_offset: 0,
                source_map_column_offset: 0,
                bindings: request_bindings_from_function(function, context.schema_names),
                response: responses.first().cloned(),
                responses,
                effects: effects.effects,
                schema_metadata_conflict: false,
            })
        }
        _ => None,
    }
}

fn validate_handler_body_validate_schema_references(
    path: &Path,
    argument: &Argument<'_>,
    schema_names: &BTreeSet<String>,
) -> Result<(), Diagnostic> {
    let unresolved = match argument {
        Argument::ArrowFunctionExpression(function) => {
            handler_context_parameter_name(&function.params).and_then(|ctx_name| {
                function.body.statements.iter().find_map(|statement| {
                    unresolved_body_validate_schema_in_statement(statement, &ctx_name, schema_names)
                })
            })
        }
        Argument::FunctionExpression(function) => handler_context_parameter_name(&function.params)
            .and_then(|ctx_name| {
                function.body.as_ref().and_then(|body| {
                    body.statements.iter().find_map(|statement| {
                        unresolved_body_validate_schema_in_statement(
                            statement,
                            &ctx_name,
                            schema_names,
                        )
                    })
                })
            }),
        _ => None,
    };
    if let Some((schema, span)) = unresolved {
        return Err(unresolved_schema_diagnostic(path, span, &schema));
    }
    Ok(())
}

fn unresolved_body_validate_schema_in_statement(
    statement: &Statement<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<(String, Span)> {
    match statement {
        Statement::BlockStatement(block) => block.body.iter().find_map(|statement| {
            unresolved_body_validate_schema_in_statement(statement, ctx_name, schema_names)
        }),
        Statement::ReturnStatement(statement) => statement.argument.as_ref().and_then(|argument| {
            unresolved_body_validate_schema_in_expression(argument, ctx_name, schema_names)
        }),
        Statement::ExpressionStatement(statement) => unresolved_body_validate_schema_in_expression(
            &statement.expression,
            ctx_name,
            schema_names,
        ),
        Statement::VariableDeclaration(declaration) => {
            declaration.declarations.iter().find_map(|declarator| {
                declarator.init.as_ref().and_then(|init| {
                    unresolved_body_validate_schema_in_expression(init, ctx_name, schema_names)
                })
            })
        }
        Statement::IfStatement(statement) => {
            unresolved_body_validate_schema_in_expression(&statement.test, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_statement(
                        &statement.consequent,
                        ctx_name,
                        schema_names,
                    )
                })
                .or_else(|| {
                    statement.alternate.as_ref().and_then(|alternate| {
                        unresolved_body_validate_schema_in_statement(
                            alternate,
                            ctx_name,
                            schema_names,
                        )
                    })
                })
        }
        Statement::ThrowStatement(statement) => unresolved_body_validate_schema_in_expression(
            &statement.argument,
            ctx_name,
            schema_names,
        ),
        Statement::DoWhileStatement(statement) => {
            unresolved_body_validate_schema_in_statement(&statement.body, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_expression(
                        &statement.test,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Statement::WhileStatement(statement) => {
            unresolved_body_validate_schema_in_expression(&statement.test, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_statement(
                        &statement.body,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Statement::ForStatement(statement) => statement
            .init
            .as_ref()
            .and_then(|init| {
                unresolved_body_validate_schema_in_for_init(init, ctx_name, schema_names)
            })
            .or_else(|| {
                statement.test.as_ref().and_then(|test| {
                    unresolved_body_validate_schema_in_expression(test, ctx_name, schema_names)
                })
            })
            .or_else(|| {
                statement.update.as_ref().and_then(|update| {
                    unresolved_body_validate_schema_in_expression(update, ctx_name, schema_names)
                })
            })
            .or_else(|| {
                unresolved_body_validate_schema_in_statement(
                    &statement.body,
                    ctx_name,
                    schema_names,
                )
            }),
        Statement::ForInStatement(statement) => {
            unresolved_body_validate_schema_in_expression(&statement.right, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_statement(
                        &statement.body,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Statement::ForOfStatement(statement) => {
            unresolved_body_validate_schema_in_expression(&statement.right, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_statement(
                        &statement.body,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Statement::SwitchStatement(statement) => unresolved_body_validate_schema_in_expression(
            &statement.discriminant,
            ctx_name,
            schema_names,
        )
        .or_else(|| {
            statement.cases.iter().find_map(|case| {
                case.test
                    .as_ref()
                    .and_then(|test| {
                        unresolved_body_validate_schema_in_expression(test, ctx_name, schema_names)
                    })
                    .or_else(|| {
                        case.consequent.iter().find_map(|statement| {
                            unresolved_body_validate_schema_in_statement(
                                statement,
                                ctx_name,
                                schema_names,
                            )
                        })
                    })
            })
        }),
        Statement::TryStatement(statement) => statement
            .block
            .body
            .iter()
            .find_map(|statement| {
                unresolved_body_validate_schema_in_statement(statement, ctx_name, schema_names)
            })
            .or_else(|| {
                statement.handler.as_ref().and_then(|handler| {
                    handler.body.body.iter().find_map(|statement| {
                        unresolved_body_validate_schema_in_statement(
                            statement,
                            ctx_name,
                            schema_names,
                        )
                    })
                })
            })
            .or_else(|| {
                statement.finalizer.as_ref().and_then(|finalizer| {
                    finalizer.body.iter().find_map(|statement| {
                        unresolved_body_validate_schema_in_statement(
                            statement,
                            ctx_name,
                            schema_names,
                        )
                    })
                })
            }),
        _ => None,
    }
}

fn unresolved_body_validate_schema_in_for_init(
    init: &ForStatementInit<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<(String, Span)> {
    match init {
        ForStatementInit::VariableDeclaration(declaration) => {
            declaration.declarations.iter().find_map(|declarator| {
                declarator.init.as_ref().and_then(|init| {
                    unresolved_body_validate_schema_in_expression(init, ctx_name, schema_names)
                })
            })
        }
        _ => init.as_expression().and_then(|expression| {
            unresolved_body_validate_schema_in_expression(expression, ctx_name, schema_names)
        }),
    }
}

fn unresolved_body_validate_schema_in_expression(
    expression: &Expression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<(String, Span)> {
    match expression {
        Expression::CallExpression(call) => {
            unresolved_body_validate_schema_in_call(call, ctx_name, schema_names)
        }
        Expression::AwaitExpression(expression) => unresolved_body_validate_schema_in_expression(
            &expression.argument,
            ctx_name,
            schema_names,
        ),
        Expression::ObjectExpression(object) => object.properties.iter().find_map(|property| {
            if let ObjectPropertyKind::ObjectProperty(property) = property {
                unresolved_body_validate_schema_in_expression(
                    &property.value,
                    ctx_name,
                    schema_names,
                )
            } else {
                None
            }
        }),
        Expression::ArrayExpression(array) => array.elements.iter().find_map(|element| {
            unresolved_body_validate_schema_in_array_element(element, ctx_name, schema_names)
        }),
        Expression::StaticMemberExpression(member) => {
            unresolved_body_validate_schema_in_expression(&member.object, ctx_name, schema_names)
        }
        Expression::ComputedMemberExpression(member) => {
            unresolved_body_validate_schema_in_expression(&member.object, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_expression(
                        &member.expression,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Expression::ParenthesizedExpression(expression) => {
            unresolved_body_validate_schema_in_expression(
                &expression.expression,
                ctx_name,
                schema_names,
            )
        }
        Expression::ConditionalExpression(expression) => {
            unresolved_body_validate_schema_in_expression(&expression.test, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_expression(
                        &expression.consequent,
                        ctx_name,
                        schema_names,
                    )
                })
                .or_else(|| {
                    unresolved_body_validate_schema_in_expression(
                        &expression.alternate,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Expression::UnaryExpression(expression) => unresolved_body_validate_schema_in_expression(
            &expression.argument,
            ctx_name,
            schema_names,
        ),
        Expression::BinaryExpression(expression) => {
            unresolved_body_validate_schema_in_expression(&expression.left, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_expression(
                        &expression.right,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Expression::LogicalExpression(expression) => {
            unresolved_body_validate_schema_in_expression(&expression.left, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_expression(
                        &expression.right,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Expression::SequenceExpression(expression) => {
            expression.expressions.iter().find_map(|expression| {
                unresolved_body_validate_schema_in_expression(expression, ctx_name, schema_names)
            })
        }
        _ => None,
    }
}

fn unresolved_body_validate_schema_in_argument(
    argument: &Argument<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<(String, Span)> {
    match argument {
        Argument::CallExpression(call) => {
            unresolved_body_validate_schema_in_call(call, ctx_name, schema_names)
        }
        _ => argument.as_expression().and_then(|expression| {
            unresolved_body_validate_schema_in_expression(expression, ctx_name, schema_names)
        }),
    }
}

fn unresolved_body_validate_schema_in_array_element(
    element: &ArrayExpressionElement<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<(String, Span)> {
    match element {
        ArrayExpressionElement::CallExpression(call) => {
            unresolved_body_validate_schema_in_call(call, ctx_name, schema_names)
        }
        _ => element.as_expression().and_then(|expression| {
            unresolved_body_validate_schema_in_expression(expression, ctx_name, schema_names)
        }),
    }
}

fn unresolved_body_validate_schema_in_call(
    call: &CallExpression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<(String, Span)> {
    if let Some(chain) = static_member_chain(&call.callee) {
        if chain.len() == 3 && chain[0] == ctx_name && chain[1] == "body" && chain[2] == "validate"
        {
            let Some(Argument::Identifier(identifier)) = call.arguments.first() else {
                return None;
            };
            let schema = identifier.name.as_str();
            if schema == "undefined" {
                return None;
            }
            return (!schema_names.contains(schema)).then(|| (schema.to_string(), identifier.span));
        }
    }
    unresolved_body_validate_schema_in_expression(&call.callee, ctx_name, schema_names).or_else(
        || {
            call.arguments.iter().find_map(|argument| {
                unresolved_body_validate_schema_in_argument(argument, ctx_name, schema_names)
            })
        },
    )
}

fn arrow_requires_results_import(function: &oxc_ast::ast::ArrowFunctionExpression<'_>) -> bool {
    function
        .body
        .statements
        .iter()
        .any(statement_requires_results_import)
}

fn function_requires_results_import(function: &oxc_ast::ast::Function<'_>) -> bool {
    function.body.as_ref().is_some_and(|body| {
        body.statements
            .iter()
            .any(statement_requires_results_import)
    })
}

fn statement_requires_results_import(statement: &Statement<'_>) -> bool {
    match statement {
        Statement::VariableDeclaration(declaration) => {
            declaration.declarations.iter().any(|declarator| {
                declarator
                    .init
                    .as_ref()
                    .is_some_and(expression_requires_results_import)
            })
        }
        Statement::ReturnStatement(statement) => statement
            .argument
            .as_ref()
            .is_some_and(expression_requires_results_import),
        Statement::ExpressionStatement(statement) => {
            expression_requires_results_import(&statement.expression)
        }
        Statement::BlockStatement(block) => {
            block.body.iter().any(statement_requires_results_import)
        }
        Statement::IfStatement(statement) => {
            expression_requires_results_import(&statement.test)
                || statement_requires_results_import(&statement.consequent)
                || statement
                    .alternate
                    .as_ref()
                    .is_some_and(|alternate| statement_requires_results_import(alternate))
        }
        Statement::DoWhileStatement(statement) => {
            statement_requires_results_import(&statement.body)
                || expression_requires_results_import(&statement.test)
        }
        Statement::WhileStatement(statement) => {
            expression_requires_results_import(&statement.test)
                || statement_requires_results_import(&statement.body)
        }
        Statement::ForStatement(statement) => {
            statement
                .init
                .as_ref()
                .is_some_and(for_init_requires_results_import)
                || statement
                    .test
                    .as_ref()
                    .is_some_and(expression_requires_results_import)
                || statement
                    .update
                    .as_ref()
                    .is_some_and(expression_requires_results_import)
                || statement_requires_results_import(&statement.body)
        }
        Statement::ForInStatement(statement) => {
            expression_requires_results_import(&statement.right)
                || statement_requires_results_import(&statement.body)
        }
        Statement::ForOfStatement(statement) => {
            expression_requires_results_import(&statement.right)
                || statement_requires_results_import(&statement.body)
        }
        Statement::SwitchStatement(statement) => {
            expression_requires_results_import(&statement.discriminant)
                || statement.cases.iter().any(|case| {
                    case.test
                        .as_ref()
                        .is_some_and(expression_requires_results_import)
                        || case
                            .consequent
                            .iter()
                            .any(statement_requires_results_import)
                })
        }
        Statement::TryStatement(statement) => {
            statement
                .block
                .body
                .iter()
                .any(statement_requires_results_import)
                || statement.handler.as_ref().is_some_and(|handler| {
                    handler
                        .body
                        .body
                        .iter()
                        .any(statement_requires_results_import)
                })
                || statement.finalizer.as_ref().is_some_and(|finalizer| {
                    finalizer.body.iter().any(statement_requires_results_import)
                })
        }
        Statement::ThrowStatement(statement) => {
            expression_requires_results_import(&statement.argument)
        }
        _ => false,
    }
}

fn for_init_requires_results_import(init: &ForStatementInit<'_>) -> bool {
    match init {
        ForStatementInit::VariableDeclaration(declaration) => {
            declaration.declarations.iter().any(|declarator| {
                declarator
                    .init
                    .as_ref()
                    .is_some_and(expression_requires_results_import)
            })
        }
        ForStatementInit::CallExpression(call) => call_requires_results_import(call),
        ForStatementInit::ParenthesizedExpression(parenthesized) => {
            expression_requires_results_import(&parenthesized.expression)
        }
        _ => false,
    }
}

fn expression_requires_results_import(expression: &Expression<'_>) -> bool {
    match expression {
        Expression::CallExpression(call) => call_requires_results_import(call),
        Expression::NewExpression(expression) => {
            expression_requires_results_import(&expression.callee)
                || expression
                    .arguments
                    .iter()
                    .any(argument_requires_results_import)
        }
        Expression::AwaitExpression(expression) => {
            expression_requires_results_import(&expression.argument)
        }
        Expression::ArrayExpression(array) => array
            .elements
            .iter()
            .any(array_element_requires_results_import),
        Expression::ObjectExpression(object) => {
            object.properties.iter().any(|property| match property {
                ObjectPropertyKind::ObjectProperty(property) => {
                    expression_requires_results_import(&property.value)
                }
                ObjectPropertyKind::SpreadProperty(property) => {
                    expression_requires_results_import(&property.argument)
                }
            })
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_requires_results_import(&parenthesized.expression)
        }
        Expression::StaticMemberExpression(member) => {
            member_object_is_results(&member.object)
                || expression_requires_results_import(&member.object)
        }
        Expression::ComputedMemberExpression(member) => {
            member_object_is_results(&member.object)
                || expression_requires_results_import(&member.object)
                || expression_requires_results_import(&member.expression)
        }
        Expression::ChainExpression(chain) => {
            chain_element_requires_results_import(&chain.expression)
        }
        Expression::BinaryExpression(expression) => {
            expression_requires_results_import(&expression.left)
                || expression_requires_results_import(&expression.right)
        }
        Expression::LogicalExpression(expression) => {
            expression_requires_results_import(&expression.left)
                || expression_requires_results_import(&expression.right)
        }
        Expression::ConditionalExpression(expression) => {
            expression_requires_results_import(&expression.test)
                || expression_requires_results_import(&expression.consequent)
                || expression_requires_results_import(&expression.alternate)
        }
        Expression::SequenceExpression(expression) => expression
            .expressions
            .iter()
            .any(expression_requires_results_import),
        Expression::TaggedTemplateExpression(expression) => {
            expression_requires_results_import(&expression.tag)
        }
        Expression::UnaryExpression(expression) => {
            expression_requires_results_import(&expression.argument)
        }
        Expression::UpdateExpression(_) => false,
        Expression::YieldExpression(expression) => expression
            .argument
            .as_ref()
            .is_some_and(expression_requires_results_import),
        Expression::AssignmentExpression(expression) => {
            expression_requires_results_import(&expression.right)
        }
        Expression::ArrowFunctionExpression(function) => arrow_requires_results_import(function),
        Expression::FunctionExpression(function) => function_requires_results_import(function),
        Expression::TSAsExpression(expression) => {
            expression_requires_results_import(&expression.expression)
        }
        Expression::TSSatisfiesExpression(expression) => {
            expression_requires_results_import(&expression.expression)
        }
        Expression::TSNonNullExpression(expression) => {
            expression_requires_results_import(&expression.expression)
        }
        Expression::TSInstantiationExpression(expression) => {
            expression_requires_results_import(&expression.expression)
        }
        _ => false,
    }
}

fn call_requires_results_import(call: &CallExpression<'_>) -> bool {
    expression_requires_results_import(&call.callee)
        || call.arguments.iter().any(argument_requires_results_import)
}

fn argument_requires_results_import(argument: &Argument<'_>) -> bool {
    argument
        .as_expression()
        .is_some_and(expression_requires_results_import)
}

fn array_element_requires_results_import(element: &ArrayExpressionElement<'_>) -> bool {
    element
        .as_expression()
        .is_some_and(expression_requires_results_import)
}

fn chain_element_requires_results_import(element: &ChainElement<'_>) -> bool {
    match element {
        ChainElement::CallExpression(call) => call_requires_results_import(call),
        ChainElement::StaticMemberExpression(member) => {
            member_object_is_results(&member.object)
                || expression_requires_results_import(&member.object)
        }
        ChainElement::ComputedMemberExpression(member) => {
            member_object_is_results(&member.object)
                || expression_requires_results_import(&member.object)
                || expression_requires_results_import(&member.expression)
        }
        ChainElement::PrivateFieldExpression(member) => {
            expression_requires_results_import(&member.object)
        }
        ChainElement::TSNonNullExpression(expression) => {
            expression_requires_results_import(&expression.expression)
        }
    }
}

fn member_object_is_results(expression: &Expression<'_>) -> bool {
    match expression {
        Expression::Identifier(identifier) => identifier.name.as_str() == "Results",
        Expression::ParenthesizedExpression(parenthesized) => {
            member_object_is_results(&parenthesized.expression)
        }
        _ => false,
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
    let schema_replacements = handler_context_parameter_name_from_bindings(&bindings)
        .map(|ctx_name| {
            body_json_schema_argument_spans_arrow(function, &ctx_name, schema_names)
                .into_iter()
                .map(|edit| (edit.argument_span, "undefined"))
                .collect::<Vec<_>>()
        })
        .unwrap_or_default();
    let handler_source = crate::framework_runtime::typed_arrow_handler_source_with_replacements(
        function,
        source,
        &bindings,
        &schema_replacements,
    )?;
    Some(Handler {
        source: handler_source.original_source,
        emitted_source: handler_source.emitted_source,
        span: function.span,
        requires_results_import: arrow_requires_results_import(function),
        is_async: function.r#async,
        runtime_deferred: false,
        source_name: source_name.to_string(),
        source_text: source.to_string(),
        source_map_line_offset: handler_source.source_map_line_offset,
        source_map_column_offset: handler_source.source_map_column_offset,
        bindings,
        response: responses.first().cloned(),
        responses,
        effects: Vec::new(),
        schema_metadata_conflict: false,
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
    let schema_replacements = handler_context_parameter_name_from_bindings(&bindings)
        .map(|ctx_name| {
            body_json_schema_argument_spans_function(function, &ctx_name, schema_names)
                .into_iter()
                .map(|edit| (edit.argument_span, "undefined"))
                .collect::<Vec<_>>()
        })
        .unwrap_or_default();
    let handler_source = crate::framework_runtime::typed_function_handler_source_with_replacements(
        function,
        source,
        &bindings,
        &schema_replacements,
    )?;
    Some(Handler {
        source: handler_source.original_source,
        emitted_source: handler_source.emitted_source,
        span: function.span,
        requires_results_import: function_requires_results_import(function),
        is_async: function.r#async,
        runtime_deferred: false,
        source_name: source_name.to_string(),
        source_text: source.to_string(),
        source_map_line_offset: handler_source.source_map_line_offset,
        source_map_column_offset: handler_source.source_map_column_offset,
        bindings,
        response: responses.first().cloned(),
        responses,
        effects: Vec::new(),
        schema_metadata_conflict: false,
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
    } else if wrapper_name == "Config" {
        type_string_literal_value(target_type).or_else(|| Some(parameter_name.to_string()))
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
                "framework route handlers do not support rest parameters",
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
                    "framework route handler parameters must be simple identifiers",
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
                    "framework route handler parameters must have TypeScript type annotations",
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

fn handler_metadata_failure_can_fallback_to_dynamic(
    argument: &Argument<'_>,
    source: &str,
    state: &AppState,
    diagnostic: &Diagnostic,
) -> bool {
    if diagnostic.code != "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE" {
        return false;
    }
    if state.data_imported || state.sql_imported || !state.provider_bindings.is_empty() {
        return false;
    }

    let Some(span) = argument_span(argument) else {
        return false;
    };
    let handler_source = source_slice(source, span).unwrap_or_default();
    if handler_source.contains(".body.")
        || handler_source.contains(".body[")
        || handler_source.contains(".provider(")
        || handler_source.contains("app.provider(")
        || handler_source.contains("db.")
    {
        return false;
    }

    true
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

fn handler_parameters_are_unsupported_for_kind(
    parameters: &oxc_ast::ast::FormalParameters<'_>,
    route_kind: &str,
) -> bool {
    let max_parameters = if route_kind == "sse" || route_kind == "websocket" {
        2
    } else {
        1
    };
    if parameters.items.len() > max_parameters || parameters.rest.is_some() {
        return true;
    }
    parameters.items.iter().any(|parameter| {
        parameter.initializer.is_some()
            || !matches!(parameter.pattern, BindingPattern::BindingIdentifier(_))
    })
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

fn handler_context_parameter_name_from_bindings(bindings: &[RequestBinding]) -> Option<String> {
    bindings.iter().find_map(|binding| {
        (binding.kind == "context" && binding.name.as_deref() == Some("RequestContext"))
            .then(|| binding.parameter.clone())
            .flatten()
    })
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
        "ok" | "json" | "bytes" | "accepted" | "badRequest" | "unauthorized" | "notFound"
        | "problem" => 0,
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
            "{}:{}:{}:{}:{}:{}",
            response.helper,
            response.status,
            response.kind,
            response.body_schema.as_deref().unwrap_or(""),
            response.native_body.as_deref().unwrap_or(""),
            response.partial
        );
        if seen.insert(key) {
            deduped.push(response);
        }
    }
    deduped
}

fn response_metadata_from_call(call: &CallExpression<'_>) -> Option<ResponseMetadata> {
    let (receiver, helper) = static_member_name(&call.callee)?;
    let (status, mut kind) = match receiver {
        "Auth" => match helper {
            "signIn" => (200, "json"),
            "signOut" => (204, "empty"),
            _ => return None,
        },
        "Results" => match helper {
            "ok" => (200, "json"),
            "json" => (200, "json"),
            "text" => (200, "text"),
            "html" => (200, "html"),
            "bytes" => (200, "bytes"),
            "created" => (201, "json"),
            "accepted" => (202, "json"),
            "noContent" => (204, "empty"),
            "stream" => (200, "stream"),
            "badRequest" => (400, "json"),
            "unauthorized" => (401, "json"),
            "notFound" => (404, "json"),
            "problem" => (problem_result_code(call).unwrap_or(500), "problem"),
            "status" => (status_result_code(call)?, "json"),
            _ => return None,
        },
        _ => return None,
    };
    if receiver == "Results" && helper == "status" && status_result_has_no_body(call) {
        kind = "empty";
    }
    Some(ResponseMetadata {
        helper: helper.to_string(),
        status,
        kind: kind.to_string(),
        body_schema: None,
        native_body: native_response_body_from_call(helper, call),
        source_name: None,
        source_text: None,
        span: Some(call.span),
        partial: false,
    })
}

fn native_response_body_from_call(helper: &str, call: &CallExpression<'_>) -> Option<String> {
    match helper {
        "text" => {
            let Argument::StringLiteral(literal) = call.arguments.first()? else {
                return None;
            };
            Some(literal.value.to_string())
        }
        "noContent" => Some(String::new()),
        "status" => {
            if status_result_has_no_body(call) {
                return Some(String::new());
            }
            let value = json_value_from_argument(call.arguments.get(1)?)?;
            serde_json::to_string(&value).ok()
        }
        "problem" => {
            let value = json_value_from_argument(call.arguments.first()?)?;
            serde_json::to_string(&value).ok()
        }
        "json" | "ok" => {
            let value = json_value_from_argument(call.arguments.first()?)?;
            serde_json::to_string(&value).ok()
        }
        _ => None,
    }
}

fn json_value_from_argument(argument: &Argument<'_>) -> Option<Value> {
    match argument {
        Argument::StringLiteral(literal) => Some(Value::String(literal.value.to_string())),
        Argument::NumericLiteral(literal) => {
            serde_json::Number::from_f64(literal.value).map(Value::Number)
        }
        Argument::BooleanLiteral(literal) => Some(Value::Bool(literal.value)),
        Argument::NullLiteral(_) => Some(Value::Null),
        Argument::ArrayExpression(array) => {
            let mut values = Vec::with_capacity(array.elements.len());
            for element in &array.elements {
                values.push(json_value_from_array_element(element)?);
            }
            Some(Value::Array(values))
        }
        Argument::ObjectExpression(object) => json_value_from_object(object),
        Argument::ParenthesizedExpression(parenthesized) => {
            json_value_from_expression(&parenthesized.expression)
        }
        _ => None,
    }
}

fn json_value_from_expression(expression: &Expression<'_>) -> Option<Value> {
    match expression {
        Expression::StringLiteral(literal) => Some(Value::String(literal.value.to_string())),
        Expression::NumericLiteral(literal) => {
            serde_json::Number::from_f64(literal.value).map(Value::Number)
        }
        Expression::BooleanLiteral(literal) => Some(Value::Bool(literal.value)),
        Expression::NullLiteral(_) => Some(Value::Null),
        Expression::ArrayExpression(array) => {
            let mut values = Vec::with_capacity(array.elements.len());
            for element in &array.elements {
                values.push(json_value_from_array_element(element)?);
            }
            Some(Value::Array(values))
        }
        Expression::ObjectExpression(object) => json_value_from_object(object),
        Expression::ParenthesizedExpression(parenthesized) => {
            json_value_from_expression(&parenthesized.expression)
        }
        _ => None,
    }
}

fn json_value_from_array_element(element: &ArrayExpressionElement<'_>) -> Option<Value> {
    match element {
        ArrayExpressionElement::StringLiteral(literal) => {
            Some(Value::String(literal.value.to_string()))
        }
        ArrayExpressionElement::NumericLiteral(literal) => {
            serde_json::Number::from_f64(literal.value).map(Value::Number)
        }
        ArrayExpressionElement::BooleanLiteral(literal) => Some(Value::Bool(literal.value)),
        ArrayExpressionElement::NullLiteral(_) => Some(Value::Null),
        ArrayExpressionElement::ArrayExpression(array) => {
            let mut values = Vec::with_capacity(array.elements.len());
            for element in &array.elements {
                values.push(json_value_from_array_element(element)?);
            }
            Some(Value::Array(values))
        }
        ArrayExpressionElement::ObjectExpression(object) => json_value_from_object(object),
        _ => None,
    }
}

fn json_value_from_object(object: &oxc_ast::ast::ObjectExpression<'_>) -> Option<Value> {
    let mut map = serde_json::Map::new();
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
        let key = json_property_key_name(&property.key)?;
        map.insert(key, json_value_from_expression(&property.value)?);
    }
    Some(Value::Object(map))
}

fn json_property_key_name(key: &PropertyKey<'_>) -> Option<String> {
    match key {
        PropertyKey::StaticIdentifier(identifier) => Some(identifier.name.to_string()),
        PropertyKey::StringLiteral(literal) => Some(literal.value.to_string()),
        PropertyKey::NumericLiteral(literal) => {
            if literal.value.fract() == 0.0 {
                Some(format!("{}", literal.value as i64))
            } else {
                Some(literal.value.to_string())
            }
        }
        _ => None,
    }
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

fn status_result_has_no_body(call: &CallExpression<'_>) -> bool {
    call.arguments.len() == 1
}

fn problem_result_code(call: &CallExpression<'_>) -> Option<u16> {
    if let Some(status) = call
        .arguments
        .get(1)
        .and_then(json_value_from_argument)
        .and_then(|value| value.get("status").and_then(json_http_status_value))
    {
        return Some(status);
    }

    call.arguments
        .first()
        .and_then(json_value_from_argument)
        .and_then(|value| value.get("status").and_then(json_http_status_value))
}

fn json_http_status_value(value: &Value) -> Option<u16> {
    let number = value.as_f64()?;
    if number.fract() == 0.0 && (100.0..=599.0).contains(&number) {
        Some(number as u16)
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
        Statement::BlockStatement(block) => {
            for statement in &block.body {
                collect_statement_request_bindings(statement, ctx_name, schema_names, bindings);
            }
        }
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
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_expression_request_bindings(init, ctx_name, schema_names, bindings);
                }
            }
        }
        Statement::IfStatement(statement) => {
            collect_expression_request_bindings(&statement.test, ctx_name, schema_names, bindings);
            collect_statement_request_bindings(
                &statement.consequent,
                ctx_name,
                schema_names,
                bindings,
            );
            if let Some(alternate) = &statement.alternate {
                collect_statement_request_bindings(alternate, ctx_name, schema_names, bindings);
            }
        }
        Statement::ThrowStatement(statement) => {
            collect_expression_request_bindings(
                &statement.argument,
                ctx_name,
                schema_names,
                bindings,
            );
        }
        Statement::DoWhileStatement(statement) => {
            collect_statement_request_bindings(&statement.body, ctx_name, schema_names, bindings);
            collect_expression_request_bindings(&statement.test, ctx_name, schema_names, bindings);
        }
        Statement::WhileStatement(statement) => {
            collect_expression_request_bindings(&statement.test, ctx_name, schema_names, bindings);
            collect_statement_request_bindings(&statement.body, ctx_name, schema_names, bindings);
        }
        Statement::ForStatement(statement) => {
            if let Some(init) = &statement.init {
                collect_for_init_request_bindings(init, ctx_name, schema_names, bindings);
            }
            if let Some(test) = &statement.test {
                collect_expression_request_bindings(test, ctx_name, schema_names, bindings);
            }
            if let Some(update) = &statement.update {
                collect_expression_request_bindings(update, ctx_name, schema_names, bindings);
            }
            collect_statement_request_bindings(&statement.body, ctx_name, schema_names, bindings);
        }
        Statement::ForInStatement(statement) => {
            collect_expression_request_bindings(&statement.right, ctx_name, schema_names, bindings);
            collect_statement_request_bindings(&statement.body, ctx_name, schema_names, bindings);
        }
        Statement::ForOfStatement(statement) => {
            collect_expression_request_bindings(&statement.right, ctx_name, schema_names, bindings);
            collect_statement_request_bindings(&statement.body, ctx_name, schema_names, bindings);
        }
        Statement::SwitchStatement(statement) => {
            collect_expression_request_bindings(
                &statement.discriminant,
                ctx_name,
                schema_names,
                bindings,
            );
            for case in &statement.cases {
                if let Some(test) = &case.test {
                    collect_expression_request_bindings(test, ctx_name, schema_names, bindings);
                }
                for statement in &case.consequent {
                    collect_statement_request_bindings(statement, ctx_name, schema_names, bindings);
                }
            }
        }
        Statement::TryStatement(statement) => {
            for statement in &statement.block.body {
                collect_statement_request_bindings(statement, ctx_name, schema_names, bindings);
            }
            if let Some(handler) = &statement.handler {
                for statement in &handler.body.body {
                    collect_statement_request_bindings(statement, ctx_name, schema_names, bindings);
                }
            }
            if let Some(finalizer) = &statement.finalizer {
                for statement in &finalizer.body {
                    collect_statement_request_bindings(statement, ctx_name, schema_names, bindings);
                }
            }
        }
        _ => {}
    }
}

fn collect_for_init_request_bindings(
    init: &ForStatementInit<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    bindings: &mut Vec<RequestBinding>,
) {
    match init {
        ForStatementInit::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_expression_request_bindings(init, ctx_name, schema_names, bindings);
                }
            }
        }
        _ => {
            if let Some(expression) = init.as_expression() {
                collect_expression_request_bindings(expression, ctx_name, schema_names, bindings);
            }
        }
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
            } else {
                collect_expression_request_bindings(&call.callee, ctx_name, schema_names, bindings);
            }
            for argument in &call.arguments {
                collect_argument_request_bindings(argument, ctx_name, schema_names, bindings);
            }
        }
        Expression::AwaitExpression(expression) => {
            collect_expression_request_bindings(
                &expression.argument,
                ctx_name,
                schema_names,
                bindings,
            );
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
        Expression::StaticMemberExpression(member)
            if static_member_chain(expression)
                .is_none_or(|chain| chain.first() != Some(&ctx_name)) =>
        {
            collect_expression_request_bindings(&member.object, ctx_name, schema_names, bindings);
        }
        Expression::StaticMemberExpression(_) => {}
        Expression::ComputedMemberExpression(member) => {
            collect_expression_request_bindings(&member.object, ctx_name, schema_names, bindings);
            collect_expression_request_bindings(
                &member.expression,
                ctx_name,
                schema_names,
                bindings,
            );
        }
        Expression::BinaryExpression(expression) => {
            collect_expression_request_bindings(&expression.left, ctx_name, schema_names, bindings);
            collect_expression_request_bindings(
                &expression.right,
                ctx_name,
                schema_names,
                bindings,
            );
        }
        Expression::LogicalExpression(expression) => {
            collect_expression_request_bindings(&expression.left, ctx_name, schema_names, bindings);
            collect_expression_request_bindings(
                &expression.right,
                ctx_name,
                schema_names,
                bindings,
            );
        }
        Expression::ConditionalExpression(expression) => {
            collect_expression_request_bindings(&expression.test, ctx_name, schema_names, bindings);
            collect_expression_request_bindings(
                &expression.consequent,
                ctx_name,
                schema_names,
                bindings,
            );
            collect_expression_request_bindings(
                &expression.alternate,
                ctx_name,
                schema_names,
                bindings,
            );
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
            } else {
                collect_expression_request_bindings(&call.callee, ctx_name, schema_names, bindings);
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
        Argument::StaticMemberExpression(_) => {
            if let Some(expression) = argument.as_expression() {
                collect_expression_request_bindings(expression, ctx_name, schema_names, bindings);
            }
        }
        _ => {
            if let Some(expression) = argument.as_expression() {
                collect_expression_request_bindings(expression, ctx_name, schema_names, bindings);
            }
        }
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
            } else {
                collect_expression_request_bindings(&call.callee, ctx_name, schema_names, bindings);
            }
            for argument in &call.arguments {
                collect_argument_request_bindings(argument, ctx_name, schema_names, bindings);
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
        _ => {
            if let Some(expression) = element.as_expression() {
                collect_expression_request_bindings(expression, ctx_name, schema_names, bindings);
            }
        }
    }
}

fn request_binding_from_expression(
    expression: &Expression<'_>,
    ctx_name: &str,
) -> Option<RequestBinding> {
    if matches!(expression, Expression::Identifier(identifier) if identifier.name.as_str() == ctx_name)
    {
        return Some(context_request_binding());
    }

    let chain = static_member_chain(expression)?;
    if chain.len() == 3 && chain[0] == ctx_name && matches!(chain[1], "route" | "query" | "header")
    {
        let name = if chain[1] == "header" {
            header_facade_binding_name(chain[2])
        } else {
            chain[2].to_string()
        };
        return Some(RequestBinding {
            kind: chain[1].to_string(),
            name: Some(name),
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
    if chain.len() == 2 && chain[0] == ctx_name && matches!(chain[1], "route" | "query" | "header")
    {
        return Some(context_request_binding());
    }
    if chain.len() >= 2 && chain[0] == ctx_name {
        return Some(context_request_binding());
    }
    None
}

fn context_request_binding() -> RequestBinding {
    RequestBinding {
        kind: "context".to_string(),
        name: Some("RequestContext".to_string()),
        schema: None,
        parameter: None,
        type_name: Some("RequestContext".to_string()),
        source_name: None,
        source_text: None,
        span: None,
        wrapper: None,
        injection_kind: None,
        provider_kind: None,
        capability: None,
        semantic: None,
        redacted: false,
    }
}

fn header_facade_binding_name(property: &str) -> String {
    let mut output = String::with_capacity(property.len());
    let mut previous_was_lower_or_digit = false;

    for ch in property.chars() {
        if ch.is_ascii_uppercase() {
            if !output.is_empty() && previous_was_lower_or_digit {
                output.push('-');
            }
            output.push(ch.to_ascii_lowercase());
            previous_was_lower_or_digit = false;
        } else {
            output.push(ch.to_ascii_lowercase());
            previous_was_lower_or_digit = ch.is_ascii_lowercase() || ch.is_ascii_digit();
        }
    }
    output
}

fn request_binding_from_call(
    call: &CallExpression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<RequestBinding> {
    let chain = static_member_chain(&call.callee)?;
    if chain.len() == 3 && chain[0] == ctx_name && chain[1] == "request" {
        let kind = match chain[2] {
            "form" if call.arguments.is_empty() => Some("body.form"),
            "multipart" if call.arguments.is_empty() => Some("body.multipart"),
            _ => None,
        };
        if let Some(kind) = kind {
            return Some(RequestBinding {
                kind: kind.to_string(),
                name: None,
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
    }
    if chain.len() == 3 && chain[0] == ctx_name && chain[1] == "cookies" && chain[2] == "get" {
        if call.arguments.len() != 1 {
            return None;
        }
        let name = call.arguments.first().and_then(argument_string_literal)?;
        return Some(RequestBinding {
            kind: "cookie".to_string(),
            name: Some(name),
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
            redacted: true,
        });
    }
    if chain.len() >= 2 && chain[0] == ctx_name && chain[1] == "request" {
        return Some(context_request_binding());
    }
    if chain.len() == 3 && chain[0] == ctx_name && chain[1] == "body" {
        let schema = body_binding_schema(call, chain[2], schema_names)?;
        let kind = if chain[2] == "validate" {
            "body.json".to_string()
        } else {
            format!("body.{}", chain[2])
        };
        return Some(RequestBinding {
            kind,
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
        "form" if call.arguments.is_empty() => Some(None),
        "multipart" if call.arguments.is_empty() => Some(None),
        "json" if call.arguments.len() == 1 => {
            let schema = call.arguments.first().and_then(argument_identifier)?;
            if schema == "undefined" {
                Some(None)
            } else if schema_names.contains(schema) {
                Some(Some(schema.to_string()))
            } else {
                None
            }
        }
        "validate" if call.arguments.len() == 1 => {
            let schema = call.arguments.first().and_then(argument_identifier)?;
            if schema == "undefined" {
                Some(None)
            } else if schema_names.contains(schema) {
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
) -> Vec<SchemaReferenceEdit> {
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
) -> Vec<SchemaReferenceEdit> {
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
    spans: &mut Vec<SchemaReferenceEdit>,
) {
    match statement {
        Statement::BlockStatement(block) => {
            for statement in &block.body {
                collect_statement_schema_argument_spans(statement, ctx_name, schema_names, spans);
            }
        }
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
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_expression_schema_argument_spans(init, ctx_name, schema_names, spans);
                }
            }
        }
        Statement::IfStatement(statement) => {
            collect_expression_schema_argument_spans(
                &statement.test,
                ctx_name,
                schema_names,
                spans,
            );
            collect_statement_schema_argument_spans(
                &statement.consequent,
                ctx_name,
                schema_names,
                spans,
            );
            if let Some(alternate) = &statement.alternate {
                collect_statement_schema_argument_spans(alternate, ctx_name, schema_names, spans);
            }
        }
        Statement::ThrowStatement(statement) => {
            collect_expression_schema_argument_spans(
                &statement.argument,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Statement::DoWhileStatement(statement) => {
            collect_statement_schema_argument_spans(&statement.body, ctx_name, schema_names, spans);
            collect_expression_schema_argument_spans(
                &statement.test,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Statement::WhileStatement(statement) => {
            collect_expression_schema_argument_spans(
                &statement.test,
                ctx_name,
                schema_names,
                spans,
            );
            collect_statement_schema_argument_spans(&statement.body, ctx_name, schema_names, spans);
        }
        Statement::ForStatement(statement) => {
            if let Some(init) = &statement.init {
                collect_for_init_schema_argument_spans(init, ctx_name, schema_names, spans);
            }
            if let Some(test) = &statement.test {
                collect_expression_schema_argument_spans(test, ctx_name, schema_names, spans);
            }
            if let Some(update) = &statement.update {
                collect_expression_schema_argument_spans(update, ctx_name, schema_names, spans);
            }
            collect_statement_schema_argument_spans(&statement.body, ctx_name, schema_names, spans);
        }
        Statement::ForInStatement(statement) => {
            collect_expression_schema_argument_spans(
                &statement.right,
                ctx_name,
                schema_names,
                spans,
            );
            collect_statement_schema_argument_spans(&statement.body, ctx_name, schema_names, spans);
        }
        Statement::ForOfStatement(statement) => {
            collect_expression_schema_argument_spans(
                &statement.right,
                ctx_name,
                schema_names,
                spans,
            );
            collect_statement_schema_argument_spans(&statement.body, ctx_name, schema_names, spans);
        }
        Statement::SwitchStatement(statement) => {
            collect_expression_schema_argument_spans(
                &statement.discriminant,
                ctx_name,
                schema_names,
                spans,
            );
            for case in &statement.cases {
                if let Some(test) = &case.test {
                    collect_expression_schema_argument_spans(test, ctx_name, schema_names, spans);
                }
                for statement in &case.consequent {
                    collect_statement_schema_argument_spans(
                        statement,
                        ctx_name,
                        schema_names,
                        spans,
                    );
                }
            }
        }
        Statement::TryStatement(statement) => {
            for statement in &statement.block.body {
                collect_statement_schema_argument_spans(statement, ctx_name, schema_names, spans);
            }
            if let Some(handler) = &statement.handler {
                for statement in &handler.body.body {
                    collect_statement_schema_argument_spans(
                        statement,
                        ctx_name,
                        schema_names,
                        spans,
                    );
                }
            }
            if let Some(finalizer) = &statement.finalizer {
                for statement in &finalizer.body {
                    collect_statement_schema_argument_spans(
                        statement,
                        ctx_name,
                        schema_names,
                        spans,
                    );
                }
            }
        }
        _ => {}
    }
}

fn collect_for_init_schema_argument_spans(
    init: &ForStatementInit<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    spans: &mut Vec<SchemaReferenceEdit>,
) {
    match init {
        ForStatementInit::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_expression_schema_argument_spans(init, ctx_name, schema_names, spans);
                }
            }
        }
        _ => {
            if let Some(expression) = init.as_expression() {
                collect_expression_schema_argument_spans(expression, ctx_name, schema_names, spans);
            }
        }
    }
}

fn collect_expression_schema_argument_spans(
    expression: &Expression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    spans: &mut Vec<SchemaReferenceEdit>,
) {
    match expression {
        Expression::CallExpression(call) => {
            if let Some(edit) = body_json_schema_reference_edit(call, ctx_name, schema_names) {
                spans.push(edit);
            } else {
                collect_expression_schema_argument_spans(
                    &call.callee,
                    ctx_name,
                    schema_names,
                    spans,
                );
            }
            for argument in &call.arguments {
                collect_argument_schema_argument_spans(argument, ctx_name, schema_names, spans);
            }
        }
        Expression::AwaitExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.argument,
                ctx_name,
                schema_names,
                spans,
            );
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
        Expression::StaticMemberExpression(member)
            if static_member_chain(expression)
                .is_none_or(|chain| chain.first() != Some(&ctx_name)) =>
        {
            collect_expression_schema_argument_spans(&member.object, ctx_name, schema_names, spans);
        }
        Expression::StaticMemberExpression(_) => {}
        Expression::ComputedMemberExpression(member) => {
            collect_expression_schema_argument_spans(&member.object, ctx_name, schema_names, spans);
            collect_expression_schema_argument_spans(
                &member.expression,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::BinaryExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.left,
                ctx_name,
                schema_names,
                spans,
            );
            collect_expression_schema_argument_spans(
                &expression.right,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::LogicalExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.left,
                ctx_name,
                schema_names,
                spans,
            );
            collect_expression_schema_argument_spans(
                &expression.right,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::ConditionalExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.test,
                ctx_name,
                schema_names,
                spans,
            );
            collect_expression_schema_argument_spans(
                &expression.consequent,
                ctx_name,
                schema_names,
                spans,
            );
            collect_expression_schema_argument_spans(
                &expression.alternate,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::SequenceExpression(expression) => {
            for expression in &expression.expressions {
                collect_expression_schema_argument_spans(expression, ctx_name, schema_names, spans);
            }
        }
        Expression::TaggedTemplateExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.tag,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::UnaryExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.argument,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::YieldExpression(expression) => {
            if let Some(argument) = &expression.argument {
                collect_expression_schema_argument_spans(argument, ctx_name, schema_names, spans);
            }
        }
        Expression::AssignmentExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.right,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::TSAsExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.expression,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::TSSatisfiesExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.expression,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::TSNonNullExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.expression,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::TSInstantiationExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.expression,
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
    spans: &mut Vec<SchemaReferenceEdit>,
) {
    match argument {
        Argument::CallExpression(call) => {
            if let Some(edit) = body_json_schema_reference_edit(call, ctx_name, schema_names) {
                spans.push(edit);
            } else {
                collect_expression_schema_argument_spans(
                    &call.callee,
                    ctx_name,
                    schema_names,
                    spans,
                );
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
        Argument::StaticMemberExpression(_) => {
            if let Some(expression) = argument.as_expression() {
                collect_expression_schema_argument_spans(expression, ctx_name, schema_names, spans);
            }
        }
        _ => {
            if let Some(expression) = argument.as_expression() {
                collect_expression_schema_argument_spans(expression, ctx_name, schema_names, spans);
            }
        }
    }
}

fn collect_array_element_schema_argument_spans(
    element: &ArrayExpressionElement<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    spans: &mut Vec<SchemaReferenceEdit>,
) {
    match element {
        ArrayExpressionElement::CallExpression(call) => {
            if let Some(edit) = body_json_schema_reference_edit(call, ctx_name, schema_names) {
                spans.push(edit);
            } else {
                collect_expression_schema_argument_spans(
                    &call.callee,
                    ctx_name,
                    schema_names,
                    spans,
                );
            }
            for argument in &call.arguments {
                collect_argument_schema_argument_spans(argument, ctx_name, schema_names, spans);
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
        _ => {
            if let Some(expression) = element.as_expression() {
                collect_expression_schema_argument_spans(expression, ctx_name, schema_names, spans);
            }
        }
    }
}

fn body_json_schema_reference_edit(
    call: &CallExpression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<SchemaReferenceEdit> {
    let chain = static_member_chain(&call.callee)?;
    if chain.len() == 3
        && chain[0] == ctx_name
        && chain[1] == "body"
        && matches!(chain[2], "json" | "validate")
    {
        if call.arguments.len() != 1 {
            return None;
        }
        let Argument::Identifier(identifier) = call.arguments.first()? else {
            return None;
        };
        let schema = identifier.name.as_str();
        if schema != "undefined" && !schema_names.contains(schema) {
            return None;
        }
        return Some(SchemaReferenceEdit {
            argument_span: identifier.span,
        });
    }
    None
}

fn sanitize_handler_schema_references(
    mut source: String,
    handler_start: u32,
    edits: &[SchemaReferenceEdit],
) -> String {
    let mut replacements = Vec::new();
    for edit in edits {
        replacements.push((edit.argument_span, "undefined"));
    }
    replacements.sort_by_key(|(span, _)| std::cmp::Reverse(span.start));
    for (span, replacement) in replacements {
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
            source.replace_range(start..end, replacement);
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

fn argument_string_literal(argument: &Argument<'_>) -> Option<String> {
    match argument {
        Argument::StringLiteral(value) => Some(value.value.as_str().to_string()),
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
                || statement_is_supported_throw(statement)
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
                || statement_is_supported_throw(statement)
        })
}

fn return_statement_returns_supported_result(
    statement: &Statement<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    if return_statement_result_call(statement)
        .is_some_and(|call| results_call_arguments_are_supported(call, allowed_roots, schema_names))
    {
        return true;
    }

    return_statement_expression(statement).is_some_and(|expression| {
        expression_is_supported_handler_return_value(expression, allowed_roots, schema_names)
    })
}

fn expression_statement_is_supported_result(
    statement: &Statement<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    if expression_statement_result_call(statement)
        .is_some_and(|call| results_call_arguments_are_supported(call, allowed_roots, schema_names))
    {
        return true;
    }

    expression_statement_expression(statement).is_some_and(|expression| {
        expression_is_supported_handler_return_value(expression, allowed_roots, schema_names)
    })
}

fn statement_is_supported_throw(statement: &Statement<'_>) -> bool {
    matches!(statement, Statement::ThrowStatement(_))
}

fn return_statement_expression<'a>(statement: &'a Statement<'a>) -> Option<&'a Expression<'a>> {
    let Statement::ReturnStatement(return_statement) = statement else {
        return None;
    };
    return_statement.argument.as_ref()
}

fn expression_statement_expression<'a>(statement: &'a Statement<'a>) -> Option<&'a Expression<'a>> {
    let Statement::ExpressionStatement(expression_statement) = statement else {
        return None;
    };
    Some(&expression_statement.expression)
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
        (object == "Results" && results_helper_is_supported(property))
            || (object == "Auth" && auth_result_helper_is_supported(property))
    }) {
        Some(call)
    } else {
        None
    }
}

fn auth_result_helper_is_supported(property: &str) -> bool {
    matches!(property, "signIn" | "signOut")
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
            | "unauthorized"
            | "status"
            | "problem"
            | "stream"
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
    if object == "Auth" {
        return auth_result_call_arguments_are_supported(
            call,
            property,
            allowed_roots,
            schema_names,
        );
    }
    if object != "Results" {
        return false;
    }

    let argument_count_supported = match property {
        "text" | "html" | "bytes" => matches!(call.arguments.len(), 1 | 2),
        "json" | "ok" | "accepted" | "notFound" | "badRequest" | "unauthorized" => {
            call.arguments.len() <= 2
        }
        "stream" => matches!(call.arguments.len(), 1 | 2),
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

    if property == "stream" {
        return matches!(call.arguments.len(), 1 | 2)
            && call.arguments.first().is_some_and(|argument| {
                argument_is_stream_writer_callback(argument, allowed_roots)
            })
            && call.arguments.get(1).is_none_or(|argument| {
                argument_is_inline_json_safe_value(argument, allowed_roots, schema_names)
            });
    }

    argument_count_supported
        && call.arguments.iter().all(|argument| {
            argument_is_inline_json_safe_value(argument, allowed_roots, schema_names)
        })
}

fn auth_result_call_arguments_are_supported(
    call: &CallExpression<'_>,
    property: &str,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    match property {
        "signIn" => {
            call.arguments.len() == 2
                && call.arguments.first().is_some_and(|argument| {
                    argument_is_allowed_root_identifier(argument, allowed_roots)
                })
                && call.arguments.get(1).is_some_and(|argument| {
                    argument_is_inline_json_safe_value(argument, allowed_roots, schema_names)
                })
        }
        "signOut" => {
            call.arguments.len() == 1
                && call.arguments.first().is_some_and(|argument| {
                    argument_is_allowed_root_identifier(argument, allowed_roots)
                })
        }
        _ => false,
    }
}

fn argument_is_allowed_root_identifier(
    argument: &Argument<'_>,
    allowed_roots: &BTreeSet<String>,
) -> bool {
    match argument {
        Argument::Identifier(identifier) => allowed_roots.contains(identifier.name.as_str()),
        Argument::ParenthesizedExpression(parenthesized) => {
            expression_is_allowed_root_identifier(&parenthesized.expression, allowed_roots)
        }
        _ => false,
    }
}

fn expression_is_allowed_root_identifier(
    expression: &Expression<'_>,
    allowed_roots: &BTreeSet<String>,
) -> bool {
    match expression {
        Expression::Identifier(identifier) => allowed_roots.contains(identifier.name.as_str()),
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_is_allowed_root_identifier(&parenthesized.expression, allowed_roots)
        }
        _ => false,
    }
}

fn argument_is_stream_writer_callback(
    argument: &Argument<'_>,
    allowed_roots: &BTreeSet<String>,
) -> bool {
    let mut free_identifiers = service_factory_free_identifiers(argument);
    free_identifiers.remove("Results");
    free_identifiers.remove("Promise");
    free_identifiers.remove("Uint8Array");
    free_identifiers.retain(|identifier| !allowed_roots.contains(identifier));
    matches!(
        argument,
        Argument::ArrowFunctionExpression(_) | Argument::FunctionExpression(_)
    ) && free_identifiers.is_empty()
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
        Argument::TemplateLiteral(template) => template.expressions.iter().all(|expression| {
            expression_is_inline_json_safe_value(expression, allowed_roots, schema_names)
        }),
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
        Argument::AwaitExpression(await_expression) => expression_is_inline_json_safe_value(
            &await_expression.argument,
            allowed_roots,
            schema_names,
        ),
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
        Expression::TemplateLiteral(template) => template.expressions.iter().all(|expression| {
            expression_is_inline_json_safe_value(expression, allowed_roots, schema_names)
        }),
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
        Expression::AwaitExpression(await_expression) => expression_is_inline_json_safe_value(
            &await_expression.argument,
            allowed_roots,
            schema_names,
        ),
        Expression::StaticMemberExpression(member) => {
            static_member_root_name(&member.object).is_some_and(|root| allowed_roots.contains(root))
        }
        _ => false,
    }
}

fn expression_is_supported_handler_return_value(
    expression: &Expression<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    expression_is_inline_json_safe_value(expression, allowed_roots, schema_names)
        || expression_is_undefined_identifier(expression)
}

fn expression_is_undefined_identifier(expression: &Expression<'_>) -> bool {
    match expression {
        Expression::Identifier(identifier) => identifier.name == "undefined",
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_is_undefined_identifier(&parenthesized.expression)
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

fn write_artifacts(
    out_dir: &Path,
    app: &ExtractedApp,
    mut metrics: Option<&mut CompileMetrics>,
) -> Result<(), Diagnostic> {
    validate_output_dir(out_dir)?;
    fs::create_dir_all(out_dir).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_OUTPUT",
            format!("failed to create output directory: {error}"),
        )
        .with_path(out_dir)
    })?;

    let bundle_start = Instant::now();
    let app_js = emit_app_js(app);
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("bundleEmitMs", bundle_start.elapsed());
        metrics.set_artifact_bytes("appJsBytes", app_js.source.len());
    }
    let source_map_start = Instant::now();
    let source_map = emit_source_map(app, &app_js);
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("sourceMapMs", source_map_start.elapsed());
        metrics.set_artifact_bytes("sourceMapBytes", source_map.len());
    }
    let route_artifact_start = Instant::now();
    let route_artifact = emit_route_artifact(app)?;
    let route_artifact_metadata =
        route_artifact.as_ref().map(
            |bytes| crate::route_artifact::RouteDispatchArtifactMetadata {
                path: ROUTE_ARTIFACT_PATH.to_string(),
                hash: sha256_bytes_hex(bytes),
            },
        );
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("routeArtifactEmitMs", route_artifact_start.elapsed());
        if let Some(route_artifact) = &route_artifact {
            metrics.set_artifact_bytes("routeArtifactBytes", route_artifact.len());
        }
    }
    let plan_start = Instant::now();
    let plan = emit_plan_with_route_artifact(
        app,
        &sha256_hex(&app_js.source),
        &sha256_hex(&source_map),
        route_artifact_metadata.as_ref(),
    )?;
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("planEmitMs", plan_start.elapsed());
        metrics.set_artifact_bytes("planBytes", plan.len());
    }
    let dependency_graph = if app.dependency_graph.has_entries() {
        let dependency_graph = emit_dependency_graph(&app.dependency_graph)?;
        if let Some(metrics) = metrics.as_mut() {
            metrics.set_artifact_bytes("dependencyGraphBytes", dependency_graph.len());
        }
        Some(dependency_graph)
    } else {
        None
    };

    let write_start = Instant::now();
    write_artifact(out_dir, "app.js", &app_js.source)?;
    write_artifact(out_dir, "app.js.map", &source_map)?;
    if let Some(route_artifact) = &route_artifact {
        write_binary_artifact(out_dir, ROUTE_ARTIFACT_PATH, route_artifact)?;
    }
    write_artifact(out_dir, "app.plan.json", &plan)?;
    if let Some(dependency_graph) = &dependency_graph {
        write_artifact(out_dir, "deps.graph.json", dependency_graph)?;
    }
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("writeMs", write_start.elapsed());
    }
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

fn write_binary_artifact(out_dir: &Path, name: &str, contents: &[u8]) -> Result<(), Diagnostic> {
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

fn source_location_json(source_name: &str, source: &str, span: Span) -> Value {
    let (line, column) = line_column(source, span.start);
    json!({
        "path": source_name,
        "line": line,
        "column": column
    })
}

fn config_source_env_name(source: &str) -> Option<String> {
    if let Some(env) = source.strip_prefix("env:") {
        return (!env.is_empty()).then(|| env.to_string());
    }
    let start = source.find("${")?;
    let rest = &source[start + 2..];
    let end = rest.find('}')?;
    let env = &rest[..end];
    (!env.is_empty()).then(|| env.to_string())
}

fn framework_provider_connection_string_env(app: &ExtractedApp, key: &str) -> String {
    app.configuration
        .as_ref()
        .and_then(|configuration| {
            configuration
                .requirements
                .iter()
                .find(|requirement| {
                    normalize_config_key(&requirement.key) == normalize_config_key(key)
                })
                .and_then(|requirement| requirement.source.as_deref())
                .and_then(config_source_env_name)
        })
        .unwrap_or_else(|| config_key_to_env_name(key))
}

fn framework_provider_config_entries(app: &ExtractedApp) -> String {
    let entries = app
        .capabilities
        .iter()
        .filter(|capability| capability.capability_kind == "database")
        .map(|capability| {
            let connection_string_key = capability.config_key.clone().or_else(|| {
                if matches!(capability.provider.as_str(), "postgres" | "sqlserver") {
                    Some(format!(
                        "Sloppy:Providers:{}:{}:connectionString",
                        capability.provider,
                        provider_config_name(capability)
                    ))
                } else {
                    None
                }
            });
            let connection_string_env = connection_string_key
                .as_deref()
                .map(|key| framework_provider_connection_string_env(app, key));
            let value = json!({
                "providerKind": capability.provider,
                "access": capability.access,
                "connectionStringKey": connection_string_key,
                "connectionStringEnv": connection_string_env
            });
            format!(
                "[{}, {}]",
                serde_json::to_string(&capability.token).unwrap_or_else(|_| "\"\"".to_string()),
                serde_json::to_string(&value).unwrap_or_else(|_| "{}".to_string())
            )
        })
        .collect::<Vec<_>>()
        .join(", ");
    format!("[{entries}]")
}

fn framework_config_default_entries(app: &ExtractedApp) -> String {
    let mut entries = Vec::new();
    let mut seen = BTreeSet::new();
    for read in &app.config_reads {
        let Some(default_value) = &read.default_value else {
            continue;
        };
        if !seen.insert(normalize_config_key(&read.key)) {
            continue;
        }
        entries.push(format!(
            "[{}, {}]",
            serde_json::to_string(&read.key).unwrap_or_else(|_| "\"\"".to_string()),
            serde_json::to_string(default_value).unwrap_or_else(|_| "null".to_string())
        ));
    }
    format!("[{}]", entries.join(", "))
}

fn framework_queue_service_entries(app: &ExtractedApp) -> Vec<(String, String)> {
    let explicit_services = app
        .service_registrations
        .iter()
        .map(|registration| registration.token.clone())
        .collect::<BTreeSet<_>>();
    app.capabilities
        .iter()
        .filter(|capability| capability.capability_kind == "queue")
        .filter(|capability| !explicit_services.contains(&capability.token))
        .map(|capability| {
            let name = capability.config_name.clone().unwrap_or_else(|| {
                capability
                    .token
                    .strip_prefix("queue.")
                    .unwrap_or(&capability.token)
                    .to_string()
            });
            (capability.token.clone(), name)
        })
        .collect()
}

fn emit_app_js(app: &ExtractedApp) -> EmittedAppJs {
    if app.kind == ProjectKind::Program {
        return emit_program_app_js(app);
    }
    if let Some(source) = &app.dynamic_entry_source {
        return emit_dynamic_web_app_js(source, app);
    }
    let mut output = String::with_capacity(estimate_app_js_capacity(app));
    let mut mappings = Vec::with_capacity(app.routes.len());
    let mut handler_generated_starts = Vec::with_capacity(app.routes.len());
    let mut generated_line = 0usize;
    let needs_provider_open_helper = app.routes.iter().any(|route| {
        route
            .handler
            .emitted_source
            .contains("__sloppy_open_data_provider")
    });
    let needs_framework_arg_helper = app.routes.iter().any(|route| {
        route
            .handler
            .emitted_source
            .contains("__sloppy_framework_arg")
    });
    let needs_framework_config_bindings = app.routes.iter().any(|route| {
        route
            .handler
            .bindings
            .iter()
            .any(|binding| binding.kind == "config")
    });
    let queue_service_entries = framework_queue_service_entries(app);
    let needs_output_cache_runtime = app.routes.iter().any(|route| route.output_cache.is_some());
    let needs_framework_services = needs_framework_arg_helper
        || needs_output_cache_runtime
        || !app.service_registrations.is_empty()
        || !queue_service_entries.is_empty()
        || app.routes.iter().any(|route| {
            route
                .handler
                .emitted_source
                .contains("__sloppy_framework_services")
        });
    let needs_framework_pipeline = app.routes.iter().any(|route| {
        route
            .handler
            .emitted_source
            .contains("__sloppy_run_middleware")
    });
    let needs_request_id_helper = app
        .routes
        .iter()
        .any(|route| route.handler.emitted_source.contains("__sloppy_request_id"));
    let needs_request_logging_helper = app.routes.iter().any(|route| {
        route
            .handler
            .emitted_source
            .contains("__sloppy_request_logging")
    });
    let needs_cors_helper = app.routes.iter().any(|route| {
        route
            .handler
            .emitted_source
            .contains("__sloppy_finish_cors")
            || route
                .handler
                .emitted_source
                .contains("__sloppy_cors_preflight")
    });
    let needs_auth_helper = app.routes.iter().any(|route| {
        route
            .handler
            .emitted_source
            .contains("__sloppy_require_auth")
    }) || !app.auth.schemes.is_empty();
    let needs_framework_environment = app.routes.iter().any(|route| {
        route.handler.bindings.iter().any(|binding| {
            binding.kind == "config"
                || matches!(
                    binding.provider_kind.as_deref(),
                    Some("postgres") | Some("sqlserver")
                )
        })
    });

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
    if app.uses_realtime_runtime {
        runtime_exports.push("Realtime");
        runtime_exports.push("SloppyRealtimeError");
        runtime_exports.push("schema");
        runtime_exports.push("Schema");
    }
    if app.problem_details.is_some() {
        runtime_exports.push("ProblemDetails");
    }
    if app.uses_data_runtime {
        runtime_exports.push("data");
    }
    if app.uses_migrations_runtime {
        runtime_exports.push("Migrations");
    }
    if app.uses_provider_health_runtime {
        runtime_exports.push("ProviderHealth");
    }
    if app.uses_sql_runtime {
        runtime_exports.push("sql");
    }
    if app.uses_orm_runtime {
        runtime_exports.extend([
            "orm",
            "table",
            "column",
            "relation",
            "SloppyOrmError",
            "SloppyOrmConcurrencyError",
        ]);
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
    if needs_output_cache_runtime && !app.uses_codec_runtime {
        runtime_exports.push("Text");
    }
    if app.uses_cache_runtime || needs_output_cache_runtime {
        runtime_exports.extend(["Cache", "SloppyCacheError"]);
    }
    if app.uses_net_runtime {
        runtime_exports.extend([
            "TcpClient",
            "TcpListener",
            "TcpConnection",
            "LocalEndpoint",
            "UnixSocket",
            "NamedPipe",
            "NetworkAddress",
            "SloppyNetError",
        ]);
    }
    if app.uses_os_runtime {
        runtime_exports.extend([
            "System",
            "Environment",
            "Process",
            "ProcessHandle",
            "Signals",
            "OsError",
        ]);
    } else if needs_framework_environment {
        runtime_exports.push("Environment");
    }
    if app.uses_http_client_runtime {
        runtime_exports.extend([
            "HttpClient",
            "Http",
            "HttpClientFactory",
            "HttpError",
            "SloppyHttpClientError",
            "TestHttp",
        ]);
    }
    if app.uses_workers_runtime {
        runtime_exports.extend(WORKER_EXPORTS.iter().copied());
    }
    if app.uses_ffi_runtime || !app.ffi.is_empty() || !app.ffi_structs.is_empty() {
        runtime_exports.extend(["unsafeFfi", "t"]);
    }
    if needs_framework_services {
        runtime_exports.push("__createFrameworkServiceProvider");
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
        push_generated_line(
            &mut output,
            &mut generated_line,
            "const __sloppy_framework_services = __createFrameworkServiceProvider();",
        );
        for (token, name) in &queue_service_entries {
            let token = serde_json::to_string(token).unwrap_or_else(|_| "\"\"".to_string());
            let name = serde_json::to_string(name).unwrap_or_else(|_| "\"\"".to_string());
            push_generated_line(
                &mut output,
                &mut generated_line,
                &format!("__sloppy_framework_services.addSingleton({token}, () => WorkQueue.create({name}));"),
            );
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
    if needs_framework_config_bindings {
        let provider_configs = framework_provider_config_entries(app);
        let config_defaults = framework_config_default_entries(app);
        push_generated_line(
            &mut output,
            &mut generated_line,
            &format!("const __sloppy_framework_provider_configs = new Map({provider_configs});"),
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            &format!("const __sloppy_framework_config_defaults = new Map({config_defaults});"),
        );
    } else if needs_framework_arg_helper {
        let provider_configs = framework_provider_config_entries(app);
        push_generated_line(
            &mut output,
            &mut generated_line,
            &format!("const __sloppy_framework_provider_configs = new Map({provider_configs});"),
        );
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
            "  if (binding.kind === \"body.json\") { return ctx.body.json(); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.kind === \"body.form\") { return ctx.request.form(); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.kind === \"body.multipart\") { return ctx.request.multipart(); }",
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
        if needs_framework_config_bindings {
            push_generated_line(
                &mut output,
                &mut generated_line,
                "  if (binding.kind === \"config\") { const value = Environment.get(binding.name); if (value !== undefined) { return value; } if (__sloppy_framework_config_defaults.has(binding.name)) { return __sloppy_framework_config_defaults.get(binding.name); } throw new Error(`sloppy: Config injection for '${binding.name}' requires an environment value.`); }",
            );
        } else {
            push_generated_line(
                &mut output,
                &mut generated_line,
                "  if (binding.kind === \"config\") { const value = Environment.get(binding.name); if (value === undefined) { throw new Error(`sloppy: Config injection for '${binding.name}' requires an environment value.`); } return value; }",
            );
        }
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
            "  else if (binding.kind === \"header\") { value = ctx.header[__sloppy_framework_header_property(binding.name)]; }",
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
            "function __sloppy_framework_header_property(name) {",
        );
        push_generated_line(&mut output, &mut generated_line, "  let output = \"\";");
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  let uppercaseNext = false;",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  for (const ch of String(name)) {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    if (ch === \"-\") { uppercaseNext = output.length !== 0; continue; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    output += uppercaseNext ? ch.toUpperCase() : ch;",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    uppercaseNext = false;",
        );
        push_generated_line(&mut output, &mut generated_line, "  }");
        push_generated_line(&mut output, &mut generated_line, "  return output;");
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
            "  if (binding.injectionKind === \"provider\" && binding.providerKind === \"sqlite\" && typeof data.sqlite === \"function\") { return scope.track(data.sqlite(dependencyName)); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.injectionKind === \"provider\" && binding.providerKind === \"postgres\" && data.postgres !== undefined && typeof data.postgres.open === \"function\") { return scope.track(data.postgres.open(__sloppy_framework_provider_open_options(binding, dependencyName))); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.injectionKind === \"provider\" && binding.providerKind === \"sqlserver\" && data.sqlserver !== undefined && typeof data.sqlserver.open === \"function\") { return scope.track(data.sqlserver.open(__sloppy_framework_provider_open_options(binding, dependencyName))); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  throw new Error(`sloppy: ${binding.injectionKind} injection for '${binding.name}' is unavailable in this runtime lane.`);",
        );
        push_generated_line(&mut output, &mut generated_line, "}");
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_framework_provider_open_options(binding, token) {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  const config = __sloppy_framework_provider_configs.get(token);",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (config === undefined) { throw new Error(`sloppy: provider '${token}' is not configured for Framework injection.`); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (config.providerKind !== binding.providerKind) { throw new Error(`sloppy: provider '${token}' is configured as ${config.providerKind}, not ${binding.providerKind}.`); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  const key = config.connectionStringKey;",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  const env = config.connectionStringEnv;",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (typeof key !== \"string\" || key.length === 0 || typeof env !== \"string\" || env.length === 0) { throw new Error(`sloppy: provider '${token}' does not declare a connection string config key for Framework injection.`); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  const connectionString = Environment.get(env);",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (typeof connectionString !== \"string\" || connectionString.length === 0) { throw new Error(`sloppy: provider '${token}' requires config '${key}' from environment '${env}'.`); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  return { connectionString, capability: token, access: config.access === \"read\" ? \"read\" : \"readwrite\" };",
        );
        push_generated_line(&mut output, &mut generated_line, "}");
    }
    if needs_framework_pipeline
        || needs_request_id_helper
        || needs_request_logging_helper
        || needs_cors_helper
        || needs_auth_helper
    {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_is_plain_object(value) { return value !== null && typeof value === \"object\" && !Array.isArray(value); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_request_header(ctx, name) { const headers = ctx?.request?.headers; if (headers === undefined || headers === null) { return undefined; } const lower = String(name).toLowerCase(); if (typeof headers.get === \"function\") { const direct = headers.get(name) ?? headers.get(lower); if (direct !== undefined && direct !== null) { return direct; } if (typeof headers.entries === \"function\") { for (const [key, value] of headers.entries()) { if (String(key).toLowerCase() === lower) { return value; } } } return undefined; } if (__sloppy_is_plain_object(headers)) { for (const [key, value] of Object.entries(headers)) { if (key.toLowerCase() === lower) { return value; } } } return undefined; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_merge_headers(result, headers) { if (result === null || typeof result !== \"object\") { return result; } return Object.freeze({ ...result, headers: Object.freeze({ ...(__sloppy_is_plain_object(result.headers) ? result.headers : {}), ...headers }) }); }",
        );
    }
    if needs_framework_pipeline {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_run_middleware(ctx, middleware, terminal) { let index = -1; function dispatch(nextIndex) { if (nextIndex <= index) { throw new Error(\"Sloppy middleware next() must not be called more than once.\"); } index = nextIndex; const current = middleware[nextIndex]; if (current === undefined) { return terminal(); } let nextCalled = false; let downstreamPromise; function next() { if (nextCalled) { throw new Error(\"Sloppy middleware next() must not be called more than once.\"); } nextCalled = true; const downstream = dispatch(nextIndex + 1); downstreamPromise = Promise.resolve(downstream); return downstream; } const value = current(ctx, next); if (!nextCalled) { return value; } return Promise.resolve(value).then((returned) => downstreamPromise.then(() => returned), (error) => downstreamPromise.then(() => { throw error; }, () => { throw error; })); } return dispatch(0); }",
        );
    }
    if needs_auth_helper {
        push_generated_line(
            &mut output,
            &mut generated_line,
            &format!(
                "const __sloppy_auth_schemes = {};",
                auth_schemes_json(&app.auth)
            ),
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_policy(configure) { const requirements = []; const builder = { requireAuthenticated() { requirements.push([\"authenticated\"]); return builder; }, requireScope(...scopes) { requirements.push([\"scope\", scopes.flat()]); return builder; }, requireRole(...roles) { requirements.push([\"role\", roles.flat()]); return builder; }, requireClaim(name, value) { requirements.push([\"claim\", name, value]); return builder; }, custom(predicate) { requirements.push([\"custom\", predicate]); return builder; } }; configure(builder); return { async evaluate(user, ctx, resource) { for (const requirement of requirements) { if (requirement[0] === \"authenticated\" && user?.authenticated !== true) { return false; } if (requirement[0] === \"scope\" && !requirement[1].every((scope) => user?.hasScope(scope))) { return false; } if (requirement[0] === \"role\" && !requirement[1].some((role) => user?.hasRole(role))) { return false; } if (requirement[0] === \"claim\" && !user?.hasClaim(requirement[1], requirement[2])) { return false; } if (requirement[0] === \"custom\" && await requirement[1](user, ctx, resource) !== true) { return false; } } return true; } }; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_policy_function(policy) { if (typeof policy === \"function\") { return policy; } if (policy && typeof policy.evaluate === \"function\") { return (user, ctx, resource) => policy.evaluate(user, ctx, resource); } return () => false; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            &format!(
                "const __sloppy_auth_policies = {};",
                auth_policies_js(&app.auth)
            ),
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "const __sloppy_auth_default_scheme = __sloppy_auth_schemes[0]?.name;",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "const __sloppy_auth_memory_sessions = new Map(); let __sloppy_auth_session_counter = 0;",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_user(claims, scheme, authScheme = scheme) { const roles = Array.isArray(claims.roles) ? claims.roles.filter((role) => typeof role === \"string\") : (typeof claims.role === \"string\" ? [claims.role] : []); const scopes = []; if (typeof claims.scope === \"string\") { scopes.push(...claims.scope.split(/\\s+/).filter((scope) => scope.length !== 0)); } if (typeof claims.scp === \"string\") { scopes.push(...claims.scp.split(/\\s+/).filter((scope) => scope.length !== 0)); } if (Array.isArray(claims.scopes)) { scopes.push(...claims.scopes.filter((scope) => typeof scope === \"string\")); } if (Array.isArray(claims.scp)) { scopes.push(...claims.scp.filter((scope) => typeof scope === \"string\")); } const user = { authenticated: true, sub: typeof claims.sub === \"string\" ? claims.sub : \"\", name: typeof claims.name === \"string\" ? claims.name : \"\", roles: Object.freeze([...new Set(roles)]), scopes: Object.freeze([...new Set(scopes)]), claims: Object.freeze({ ...claims }), scheme, authScheme, hasRole(role) { return typeof role === \"string\" && user.roles.includes(role); }, hasScope(scope) { return typeof scope === \"string\" && user.scopes.includes(scope); }, hasClaim(name, value) { return typeof name === \"string\" && Object.prototype.hasOwnProperty.call(user.claims, name) && (value === undefined || Object.is(user.claims[name], value)); } }; return Object.freeze(user); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_anonymous() { return Object.freeze({ authenticated: false, sub: \"\", name: \"\", roles: Object.freeze([]), scopes: Object.freeze([]), claims: Object.freeze({}), scheme: \"\", authScheme: \"\", hasRole() { return false; }, hasScope() { return false; }, hasClaim() { return false; } }); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_attach_context(ctx) { ctx.user ??= __sloppy_auth_anonymous(); if (ctx.auth === undefined) { Object.defineProperties(ctx, { auth: { value: Object.freeze({ get user() { return ctx.user; } }), enumerable: true }, claims: { get() { return ctx.user?.claims ?? {}; }, enumerable: true }, requireUser: { value() { if (ctx.user?.authenticated !== true) { throw new Error(\"SLOPPY_E_AUTH_MISSING_CREDENTIALS: authenticated user is required.\"); } return ctx.user; }, enumerable: true }, hasScope: { value(scope) { return ctx.user?.hasScope(scope) === true; }, enumerable: true }, hasRole: { value(role) { return ctx.user?.hasRole(role) === true; }, enumerable: true }, hasClaim: { value(name, value) { return ctx.user?.hasClaim(name, value) === true; }, enumerable: true }, authorize: { async value(policy, resource) { const fn = __sloppy_auth_policies.get(policy); return typeof fn === \"function\" && (await fn(ctx.user, ctx, resource)) === true; }, enumerable: true } }); } return ctx; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_problem(status, title, code) { return Results.problem(Object.freeze({ status, title, code }), Object.freeze({ status })); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_ct_bytes(left, right) { if (!(left instanceof Uint8Array) || !(right instanceof Uint8Array)) { return false; } if (left.byteLength === 0 || right.byteLength === 0) { return left.byteLength === right.byteLength; } let diff = left.byteLength ^ right.byteLength; const length = Math.max(left.byteLength, right.byteLength); for (let index = 0; index < length; index += 1) { diff |= left[index % left.byteLength] ^ right[index % right.byteLength]; } return diff === 0; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_ct_string(left, right) { left = String(left); right = String(right); if (left.length === 0 || right.length === 0) { return left.length === right.length; } let diff = left.length ^ right.length; const length = Math.max(left.length, right.length); for (let index = 0; index < length; index += 1) { diff |= left.charCodeAt(index % left.length) ^ right.charCodeAt(index % right.length); } return diff === 0; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_json(part) { return JSON.parse(Text.utf8.decode(Base64Url.decode(part, { padding: \"optional\" }))); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_config_value(configKey, envKey) { if (typeof configKey !== \"string\" || configKey.length === 0) { return undefined; } const key = typeof envKey === \"string\" && envKey.length !== 0 ? envKey : configKey.split(\":\").join(\"__\"); return Environment.get(key); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "async function __sloppy_auth_jwt(token, scheme) { const parts = String(token).split(\".\"); if (parts.length !== 3 || parts.some((part) => part.length === 0)) { return undefined; } const header = __sloppy_auth_json(parts[0]); if (header.alg === \"none\" || header.alg !== \"HS256\") { return undefined; } if (typeof scheme.secretConfigKey !== \"string\" || scheme.secretConfigKey.length === 0) { return undefined; } const secret = __sloppy_auth_config_value(scheme.secretConfigKey, scheme.secretEnvKey); if (typeof secret !== \"string\" || secret.length === 0) { throw new Error(`sloppy: auth secret config '${scheme.secretConfigKey}' is required.`); } const expected = await Hmac.sha256(Secret.fromUtf8(secret), `${parts[0]}.${parts[1]}`); const actual = Base64Url.decode(parts[2], { padding: \"optional\" }); if (!__sloppy_ct_bytes(expected, actual)) { return undefined; } const claims = __sloppy_auth_json(parts[1]); if (claims === null || typeof claims !== \"object\" || Array.isArray(claims)) { return undefined; } const now = Math.floor(Date.now() / 1000); const skew = Number.isInteger(scheme.clockSkewSeconds) ? scheme.clockSkewSeconds : 0; if (scheme.issuer && claims.iss !== scheme.issuer) { return undefined; } if (scheme.audience && (Array.isArray(claims.aud) ? !claims.aud.includes(scheme.audience) : claims.aud !== scheme.audience)) { return undefined; } if (claims.exp !== undefined && (!Number.isFinite(claims.exp) || claims.exp <= now - skew)) { return undefined; } if (claims.nbf !== undefined && (!Number.isFinite(claims.nbf) || claims.nbf > now + skew)) { return undefined; } if (claims.iat !== undefined && (!Number.isFinite(claims.iat) || claims.iat > now + skew)) { return undefined; } if (claims.sub !== undefined && typeof claims.sub !== \"string\") { return undefined; } return __sloppy_auth_user(claims, \"jwtBearer\", scheme.name); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_cookie(ctx, name) { const cookies = ctx && ctx.cookies; if (cookies && typeof cookies.get === \"function\") { return cookies.get(name) ?? undefined; } const value = __sloppy_request_header(ctx, \"cookie\"); if (typeof value !== \"string\") { return undefined; } for (const pair of value.split(\";\")) { const equals = pair.indexOf(\"=\"); if (equals <= 0) { continue; } if (pair.slice(0, equals).trim() === name) { try { return decodeURIComponent(pair.slice(equals + 1).trim()); } catch { return undefined; } } } return undefined; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "async function __sloppy_auth_session(ctx, scheme) { const value = __sloppy_auth_cookie(ctx, scheme.cookie); if (typeof value !== \"string\") { return undefined; } const parts = value.split(\".\"); if (parts.length !== 2 || parts.some((part) => part.length === 0)) { return false; } if (typeof scheme.secretConfigKey !== \"string\" || scheme.secretConfigKey.length === 0) { return false; } const secret = __sloppy_auth_config_value(scheme.secretConfigKey, scheme.secretEnvKey); if (typeof secret !== \"string\" || secret.length === 0) { throw new Error(`sloppy: auth secret config '${scheme.secretConfigKey}' is required.`); } const expected = await Hmac.sha256(Secret.fromUtf8(secret), parts[0]); let actual; try { actual = Base64Url.decode(parts[1], { padding: \"optional\" }); } catch { return false; } if (!__sloppy_ct_bytes(expected, actual)) { return false; } const payload = __sloppy_auth_json(parts[0]); if (payload === null || typeof payload !== \"object\" || Array.isArray(payload)) { return false; } const nowMs = Date.now(); const now = Math.floor(nowMs / 1000); if (scheme.store === \"memory\") { if (typeof payload.sid !== \"string\" || payload.sid.length === 0) { return false; } const record = __sloppy_auth_memory_sessions.get(payload.sid); if (record === undefined || record.revokedAt !== undefined || (record.expiresAt !== undefined && record.expiresAt <= nowMs) || (record.idleExpiresAt !== undefined && record.idleExpiresAt <= nowMs)) { if (record !== undefined) { __sloppy_auth_memory_sessions.delete(payload.sid); } return false; } record.lastSeenAt = nowMs; if (Number.isInteger(scheme.idleTimeoutMs)) { record.idleExpiresAt = nowMs + scheme.idleTimeoutMs; } ctx.session = Object.freeze({ id: payload.sid, scheme: scheme.name, issuedAt: Math.floor(record.createdAt / 1000), expiresAt: Math.floor(record.expiresAt / 1000), revoke() { __sloppy_auth_memory_sessions.delete(payload.sid); } }); return __sloppy_auth_user(record.claims, \"cookieSession\", scheme.name); } if (payload.claims === null || typeof payload.claims !== \"object\" || Array.isArray(payload.claims)) { return false; } if (payload.exp !== undefined && (!Number.isFinite(payload.exp) || payload.exp <= now)) { return false; } ctx.session = Object.freeze({ scheme: scheme.name, issuedAt: payload.iat, expiresAt: payload.exp }); return __sloppy_auth_user(payload.claims, \"cookieSession\", scheme.name); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_session_scheme() { const scheme = __sloppy_auth_schemes.find((entry) => entry.kind === \"cookieSession\"); if (scheme === undefined) { throw new TypeError(\"Sloppy Auth.signIn/signOut requires Auth.cookieSession middleware.\"); } return scheme; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "async function __sloppy_auth_sign_in(ctx, claims) { const scheme = __sloppy_auth_session_scheme(); if (claims === null || typeof claims !== \"object\" || Array.isArray(claims)) { throw new TypeError(\"Sloppy Auth.signIn claims must be a plain object.\"); } const copied = { ...claims }; if (copied.claims !== undefined) { if (copied.claims === null || typeof copied.claims !== \"object\" || Array.isArray(copied.claims)) { throw new TypeError(\"Sloppy Auth.signIn claims.claims must be a plain object when provided.\"); } Object.assign(copied, copied.claims); delete copied.claims; } if (typeof scheme.secretConfigKey !== \"string\" || scheme.secretConfigKey.length === 0) { throw new Error(\"sloppy: auth session secret config is required.\"); } const secret = __sloppy_auth_config_value(scheme.secretConfigKey, scheme.secretEnvKey); if (typeof secret !== \"string\" || secret.length === 0) { throw new Error(`sloppy: auth secret config '${scheme.secretConfigKey}' is required.`); } const nowMs = Date.now(); const current = Math.floor(nowMs / 1000); let payload; let maxAgeSeconds = scheme.maxAgeSeconds; if (scheme.store === \"memory\") { const sid = `session-${nowMs}-${++__sloppy_auth_session_counter}`; const absolute = nowMs + (Number.isInteger(scheme.absoluteTimeoutMs) ? scheme.absoluteTimeoutMs : 86400000); const idle = Number.isInteger(scheme.idleTimeoutMs) ? nowMs + scheme.idleTimeoutMs : undefined; __sloppy_auth_memory_sessions.set(sid, { id: sid, claims: copied, createdAt: nowMs, lastSeenAt: nowMs, expiresAt: absolute, idleExpiresAt: idle }); payload = { sid }; maxAgeSeconds = Math.ceil((absolute - nowMs) / 1000); ctx.session = Object.freeze({ id: sid, scheme: scheme.name, issuedAt: current, expiresAt: Math.floor(absolute / 1000), revoke() { __sloppy_auth_memory_sessions.delete(sid); } }); } else { payload = { iat: current, claims: copied }; if (Number.isInteger(scheme.maxAgeSeconds)) { payload.exp = current + scheme.maxAgeSeconds; } } const body = Base64Url.encode(Text.utf8.encode(JSON.stringify(payload))); const signature = await Hmac.sha256(Secret.fromUtf8(secret), body); const value = `${body}.${Base64Url.encode(signature)}`; ctx.user = __sloppy_auth_user(copied, \"cookieSession\", scheme.name); const options = { path: scheme.path ?? \"/\", secure: scheme.secure !== false, httpOnly: scheme.httpOnly !== false, sameSite: scheme.sameSite ?? \"lax\" }; if (Number.isInteger(maxAgeSeconds)) { options.maxAgeSeconds = maxAgeSeconds; } return Results.ok({ ok: true }).cookie(scheme.cookie, value, options); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_sign_out(ctx) { const scheme = __sloppy_auth_session_scheme(); if (typeof ctx?.session?.id === \"string\") { __sloppy_auth_memory_sessions.delete(ctx.session.id); } ctx.user = __sloppy_auth_anonymous(); return Results.status(204).cookie(scheme.cookie, \"\", { path: scheme.path ?? \"/\", secure: scheme.secure !== false, httpOnly: scheme.httpOnly !== false, sameSite: scheme.sameSite ?? \"lax\", maxAgeSeconds: 0, expires: new Date(0) }); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "async function __sloppy_auth_rotate_session(ctx, result) { if (result === null || typeof result !== \"object\" || typeof result.cookie !== \"function\" || typeof ctx?.session?.id !== \"string\") { return result; } const scheme = __sloppy_auth_schemes.find((entry) => entry.kind === \"cookieSession\" && entry.store === \"memory\" && entry.rotation === true && entry.name === ctx.session.scheme); if (scheme === undefined) { return result; } const previous = __sloppy_auth_memory_sessions.get(ctx.session.id); if (previous === undefined || previous.revokedAt !== undefined) { return result; } if (typeof scheme.secretConfigKey !== \"string\" || scheme.secretConfigKey.length === 0) { throw new Error(\"sloppy: auth session secret config is required.\"); } const secret = __sloppy_auth_config_value(scheme.secretConfigKey, scheme.secretEnvKey); if (typeof secret !== \"string\" || secret.length === 0) { throw new Error(`sloppy: auth secret config '${scheme.secretConfigKey}' is required.`); } const nowMs = Date.now(); __sloppy_auth_memory_sessions.delete(ctx.session.id); const sid = `session-${nowMs}-${++__sloppy_auth_session_counter}`; const absolute = Number.isInteger(previous.expiresAt) ? previous.expiresAt : nowMs + (Number.isInteger(scheme.absoluteTimeoutMs) ? scheme.absoluteTimeoutMs : 86400000); const idle = Number.isInteger(scheme.idleTimeoutMs) ? nowMs + scheme.idleTimeoutMs : undefined; const record = { id: sid, claims: previous.claims, createdAt: previous.createdAt, lastSeenAt: nowMs, expiresAt: absolute, idleExpiresAt: idle }; __sloppy_auth_memory_sessions.set(sid, record); const body = Base64Url.encode(Text.utf8.encode(JSON.stringify({ sid }))); const signature = await Hmac.sha256(Secret.fromUtf8(secret), body); const value = `${body}.${Base64Url.encode(signature)}`; ctx.session = Object.freeze({ id: sid, scheme: scheme.name, issuedAt: Math.floor(record.createdAt / 1000), expiresAt: Math.floor(record.expiresAt / 1000), revoke() { __sloppy_auth_memory_sessions.delete(sid); } }); return result.cookie(scheme.cookie, value, { path: scheme.path ?? \"/\", secure: scheme.secure !== false, httpOnly: scheme.httpOnly !== false, sameSite: scheme.sameSite ?? \"lax\", maxAgeSeconds: Math.ceil((absolute - nowMs) / 1000) }); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "const Auth = Object.freeze({ signIn: __sloppy_auth_sign_in, signOut: __sloppy_auth_sign_out });",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "async function __sloppy_authenticate(ctx) { ctx.user ??= __sloppy_auth_anonymous(); for (const scheme of __sloppy_auth_schemes) { if (scheme.kind === \"jwtBearer\") { const value = __sloppy_request_header(ctx, \"authorization\"); if (typeof value !== \"string\") { continue; } const match = value.match(/^Bearer\\s+(.+)$/i); if (match === null) { return false; } const user = await __sloppy_auth_jwt(match[1], scheme); if (user === undefined) { return false; } ctx.user = user; return true; } if (scheme.kind === \"cookieSession\") { const user = await __sloppy_auth_session(ctx, scheme); if (user === undefined) { continue; } if (user === false) { return false; } ctx.user = user; return true; } if (scheme.kind === \"apiKey\") { const key = __sloppy_request_header(ctx, scheme.header); if (typeof key !== \"string\") { continue; } if (typeof scheme.configKey !== \"string\" || scheme.configKey.length === 0) { return false; } const expected = __sloppy_auth_config_value(scheme.configKey, scheme.configEnvKey); if (typeof expected !== \"string\" || expected.length === 0 || !__sloppy_ct_string(key, expected)) { return false; } ctx.user = __sloppy_auth_user({ sub: \"api-key\" }, \"apiKey\", scheme.name); return true; } } return false; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "async function __sloppy_require_auth(ctx, requirement, terminal) { __sloppy_auth_attach_context(ctx); if (requirement && requirement.required === false) { return await terminal(); } if (ctx.user === undefined || ctx.user.authenticated !== true) { const ok = await __sloppy_authenticate(ctx); if (!ok) { return __sloppy_auth_problem(401, \"Unauthorized\", \"SLOPPY_E_AUTH_UNAUTHORIZED\"); } __sloppy_auth_attach_context(ctx); } if (Array.isArray(requirement.schemes) && requirement.schemes.length !== 0 && !requirement.schemes.includes(ctx.user.scheme) && !requirement.schemes.includes(ctx.user.authScheme)) { return __sloppy_auth_problem(401, \"Unauthorized\", \"SLOPPY_E_AUTH_UNAUTHORIZED\"); } if ((!Array.isArray(requirement.schemes) || requirement.schemes.length === 0) && typeof __sloppy_auth_default_scheme === \"string\" && ctx.user.scheme !== __sloppy_auth_default_scheme && ctx.user.authScheme !== __sloppy_auth_default_scheme) { return __sloppy_auth_problem(401, \"Unauthorized\", \"SLOPPY_E_AUTH_UNAUTHORIZED\"); } if (Array.isArray(requirement.scopes) && requirement.scopes.length !== 0 && !requirement.scopes.every((scope) => ctx.user.hasScope(scope))) { return __sloppy_auth_problem(403, \"Forbidden\", \"SLOPPY_E_AUTH_FORBIDDEN\"); } if (Array.isArray(requirement.roles) && requirement.roles.length !== 0 && !requirement.roles.some((role) => ctx.user.hasRole(role))) { return __sloppy_auth_problem(403, \"Forbidden\", \"SLOPPY_E_AUTH_FORBIDDEN\"); } if (Array.isArray(requirement.claims) && requirement.claims.length !== 0 && !requirement.claims.every((claim) => ctx.user.hasClaim(claim))) { return __sloppy_auth_problem(403, \"Forbidden\", \"SLOPPY_E_AUTH_FORBIDDEN\"); } if (typeof requirement.policy === \"string\" && requirement.policy.length !== 0) { const policy = __sloppy_auth_policies.get(requirement.policy); if (typeof policy !== \"function\" || (await policy(ctx.user, ctx)) !== true) { return __sloppy_auth_problem(403, \"Forbidden\", \"SLOPPY_E_AUTH_FORBIDDEN\"); } } return await __sloppy_auth_rotate_session(ctx, await terminal()); }",
        );
    }
    if needs_request_id_helper {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_header_value_safe(value) { if (typeof value !== \"string\" || value.trim().length === 0) { return false; } for (let index = 0; index < value.length; index += 1) { const code = value.charCodeAt(index); if ((code < 0x20 && code !== 0x09) || code === 0x7f) { return false; } } return true; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_request_id(options) { let counter = 0; return async function(ctx, next) { const incoming = options.trustIncoming ? __sloppy_request_header(ctx, options.header) : undefined; const requestId = __sloppy_header_value_safe(incoming) ? incoming : `req-${++counter}`; if (!__sloppy_header_value_safe(requestId)) { throw new TypeError(\"Sloppy RequestId generator must return a safe non-empty value.\"); } Object.defineProperty(ctx, \"requestId\", { value: requestId, enumerable: true, writable: true, configurable: true }); const result = await next(); return options.responseHeader ? __sloppy_merge_headers(result, { [options.header]: requestId }) : result; }; }",
        );
    }
    if needs_request_logging_helper {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_request_method(ctx) { return String(ctx?.request?.method ?? ctx?.method ?? \"GET\").toUpperCase(); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_request_path(ctx) { const request = ctx?.request; return String(request?.rawTarget ?? request?.target ?? request?.path ?? ctx?.path ?? ctx?.routePattern ?? \"\"); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_response_status(result) { return Number.isInteger(result?.status) ? result.status : 200; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_write_request_log(ctx, options, startedAt, status) { if (ctx?.log === undefined || typeof ctx.log.info !== \"function\") { return; } const fields = { method: __sloppy_request_method(ctx), path: __sloppy_request_path(ctx), status }; if (options.includeRoute) { if (typeof ctx.routePattern === \"string\" && ctx.routePattern.length !== 0) { fields.route = ctx.routePattern; fields.routePattern = ctx.routePattern; } if (typeof ctx.routeName === \"string\" && ctx.routeName.length !== 0) { fields.routeName = ctx.routeName; } } if (options.includeRequestId && typeof ctx.requestId === \"string\") { fields.requestId = ctx.requestId; } if (options.includeDuration) { fields.durationMs = Math.max(0, Date.now() - startedAt); } ctx.log.info(\"request completed\", fields); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_request_logging(options) { return async function(ctx, next) { const startedAt = options.includeDuration ? Date.now() : 0; try { const result = await next(); __sloppy_write_request_log(ctx, options, startedAt, __sloppy_response_status(result)); return result; } catch (error) { __sloppy_write_request_log(ctx, options, startedAt, 500); throw error; } }; }",
        );
    }
    if needs_cors_helper {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_cors_allowed_origin(policy, origin) { if (typeof origin !== \"string\" || origin.length === 0) { return undefined; } if (policy.origins.includes(\"*\")) { return \"*\"; } return policy.origins.includes(origin) ? origin : undefined; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_cors_vary(existing, value) { if (existing === undefined || existing.length === 0) { return value; } return existing.split(\",\").map((token) => token.trim().toLowerCase()).includes(value.toLowerCase()) ? existing : `${existing}, ${value}`; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_finish_cors(result, policy, ctx) { const allowed = __sloppy_cors_allowed_origin(policy, __sloppy_request_header(ctx, \"origin\")); if (allowed === undefined) { return result; } const headers = { ...(__sloppy_is_plain_object(result?.headers) ? result.headers : {}), \"Access-Control-Allow-Origin\": allowed }; if (!policy.origins.includes(\"*\")) { headers.Vary = __sloppy_cors_vary(headers.Vary, \"Origin\"); } if (policy.credentials) { headers[\"Access-Control-Allow-Credentials\"] = \"true\"; } if (policy.exposedHeaders.length !== 0) { headers[\"Access-Control-Expose-Headers\"] = policy.exposedHeaders.join(\", \"); } return __sloppy_merge_headers(result, headers); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_cors_requested_headers_allowed(policy, requestedHeaders) { if (typeof requestedHeaders !== \"string\" || requestedHeaders.length === 0) { return true; } const requested = requestedHeaders.split(\",\").map((header) => header.trim().toLowerCase()).filter((header) => header.length !== 0); return requested.every((header) => policy.headers.includes(header)); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_cors_preflight(policy, routeMethods, ctx) { const allowed = __sloppy_cors_allowed_origin(policy, __sloppy_request_header(ctx, \"origin\")); const requestedMethod = (__sloppy_request_header(ctx, \"access-control-request-method\") ?? \"\").toUpperCase(); const requestedHeaders = __sloppy_request_header(ctx, \"access-control-request-headers\"); const methods = policy.methods.length === 0 ? routeMethods : policy.methods; if (allowed === undefined || !methods.includes(requestedMethod) || !__sloppy_cors_requested_headers_allowed(policy, requestedHeaders)) { return Results.status(403); } const headers = { \"Access-Control-Allow-Origin\": allowed, \"Access-Control-Allow-Methods\": methods.join(\", \") }; if (!policy.origins.includes(\"*\")) { headers.Vary = \"Origin, Access-Control-Request-Method, Access-Control-Request-Headers\"; } if (policy.credentials) { headers[\"Access-Control-Allow-Credentials\"] = \"true\"; } if (policy.headers.length !== 0) { headers[\"Access-Control-Allow-Headers\"] = policy.headers.join(\", \"); } if (policy.maxAgeSeconds !== null && policy.maxAgeSeconds !== undefined) { headers[\"Access-Control-Max-Age\"] = String(policy.maxAgeSeconds); } return Results.status(204, undefined, { headers }); }",
        );
    }
    if needs_output_cache_runtime {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_plain_object(value) { return value !== null && typeof value === \"object\" && !Array.isArray(value); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_header(result, value) { return { ...result, headers: { ...(__sloppy_output_cache_plain_object(result?.headers) ? result.headers : {}), \"X-Sloppy-Output-Cache\": value } }; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_request_value(value) { return value === undefined || value === null ? \"\" : String(value); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_request_header(ctx, name) { const headers = ctx?.request?.headers; if (headers === undefined || headers === null) { return undefined; } const lower = String(name).toLowerCase(); if (typeof headers.get === \"function\") { const direct = headers.get(name) ?? headers.get(lower); if (direct !== undefined && direct !== null) { return direct; } if (typeof headers.entries === \"function\") { for (const [key, value] of headers.entries()) { if (String(key).toLowerCase() === lower) { return value; } } } return undefined; } if (__sloppy_output_cache_plain_object(headers)) { for (const [key, value] of Object.entries(headers)) { if (key.toLowerCase() === lower) { return value; } } } return undefined; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_body_size(result) { if (result?.body === undefined) { return 0; } if (typeof result.body === \"string\") { return Text.utf8.encode(result.body).byteLength; } if (result.body instanceof Uint8Array) { return result.body.byteLength; } const serialized = JSON.stringify(result.body); return serialized === undefined ? 0 : Text.utf8.encode(serialized).byteLength; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_non_json(value, seen = new Set()) { if (value === null || typeof value === \"string\" || typeof value === \"number\" || typeof value === \"boolean\") { return false; } if (typeof value === \"function\" || typeof value === \"symbol\" || typeof value === \"undefined\" || typeof value === \"bigint\") { return true; } if (typeof value !== \"object\" || seen.has(value)) { return true; } seen.add(value); if (Array.isArray(value)) { return value.some((item) => __sloppy_output_cache_non_json(item, seen)); } if (!__sloppy_output_cache_plain_object(value)) { return true; } return Object.values(value).some((item) => __sloppy_output_cache_non_json(item, seen)); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_unsupported_reason(result) { if (result?.__sloppyResult !== true) { return \"unsupported-result\"; } if (result.kind === \"stream\") { return \"streaming\"; } if (result.kind === \"json\") { return __sloppy_output_cache_non_json(result.body) ? \"unsupported-body\" : undefined; } if (result.kind === \"text\") { return typeof result.body === \"string\" ? undefined : \"unsupported-body\"; } if (result.kind === \"bytes\") { return result.body instanceof Uint8Array ? undefined : \"unsupported-body\"; } if (result.kind === \"empty\") { return result.body === undefined ? undefined : \"unsupported-body\"; } return \"unsupported-result-kind\"; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_key(ctx, routeKey, options, authRequired) { const query = __sloppy_output_cache_plain_object(ctx?.query) ? ctx.query : {}; const queryParts = options.varyByQuery === \"all\" ? Object.entries(query).sort(([left], [right]) => String(left).localeCompare(String(right))) : (Array.isArray(options.varyByQuery) ? options.varyByQuery : []).map((name) => [name, __sloppy_output_cache_request_value(query[name])]); const headerParts = (Array.isArray(options.varyByHeader) ? options.varyByHeader : []).map((name) => [String(name).toLowerCase(), __sloppy_output_cache_request_value(__sloppy_output_cache_request_header(ctx, name))]); const route = __sloppy_output_cache_plain_object(ctx?.route) ? ctx.route : {}; const routeParts = (Array.isArray(options.varyByRouteParams) ? options.varyByRouteParams : []).map((name) => [name, __sloppy_output_cache_request_value(route[name])]); const partition = []; if (authRequired || options.allowAuthenticated === true) { if (options.varyByUser === true) { const sub = ctx?.user?.sub ?? ctx?.user?.id; if (typeof sub !== \"string\" || sub.length === 0) { return undefined; } partition.push([\"user\", sub]); } else if (options.allowSharedAuthenticated === true) { if (options.varyByRole === true) { const roles = Array.isArray(ctx?.user?.roles) ? ctx.user.roles.map(String).sort().join(\",\") : \"\"; partition.push([\"roles\", roles]); } if (Array.isArray(options.varyByClaim)) { for (const claim of options.varyByClaim) { const value = typeof ctx?.user?.claim === \"function\" ? ctx.user.claim(claim) : ctx?.user?.claims?.[claim]; partition.push([`claim:${claim}`, __sloppy_output_cache_request_value(value)]); } } if (partition.length === 0) { return undefined; } } else { return undefined; } } return `output:${JSON.stringify({ routeKey, query: queryParts, headers: headerParts, params: routeParts, partition })}`; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_bypass_reason(ctx, result, options, authRequired) { if ((ctx?.request?.method ?? \"GET\") !== \"GET\" && (ctx?.request?.method ?? \"GET\") !== \"HEAD\") { return \"method\"; } if (authRequired && options.allowAuthenticated !== true) { return \"auth-unsafe\"; } const unsupported = __sloppy_output_cache_unsupported_reason(result); if (unsupported !== undefined) { return unsupported; } const statusCodes = Array.isArray(options.statusCodes) ? options.statusCodes : [200, 203, 204]; if (!statusCodes.includes(result.status)) { return \"status\"; } if (Array.isArray(result.setCookies) && result.setCookies.length !== 0 && options.allowSetCookie !== true) { return \"set-cookie\"; } const headers = __sloppy_output_cache_plain_object(result.headers) ? result.headers : {}; if (Object.keys(headers).some((name) => name.toLowerCase() === \"set-cookie\") && options.allowSetCookie !== true) { return \"set-cookie\"; } const maxBodyBytes = Number.isInteger(options.maxBodyBytes) ? options.maxBodyBytes : 1048576; if (__sloppy_output_cache_body_size(result) > maxBodyBytes) { return \"body-too-large\"; } return undefined; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "async function __sloppy_output_cache(ctx, routeKey, options, authRequired, terminal) { const scope = __sloppy_framework_services.createScope(ctx); ctx.services = scope; try { const cache = scope.get(Cache.token(options.cacheName ?? \"default\")); const key = __sloppy_output_cache_key(ctx, routeKey, options, authRequired); if (key !== undefined) { const cached = await cache.get(key); if (cached !== undefined) { return __sloppy_output_cache_header(cached, \"HIT\"); } } const result = await terminal(ctx); const reason = key === undefined ? \"auth-unsafe\" : __sloppy_output_cache_bypass_reason(ctx, result, options, authRequired); if (reason !== undefined) { return __sloppy_output_cache_header(result, \"BYPASS\"); } await cache.set(key, result, { ttlMs: options.ttlMs, tags: Array.isArray(options.tags) ? options.tags : [`route:${routeKey}`] }); return __sloppy_output_cache_header(result, \"MISS\"); } finally { await scope.dispose(); } }",
        );
    }
    push_generated_line(&mut output, &mut generated_line, "");

    for helper_source in &app.helper_sources {
        push_generated_source(&mut output, &mut generated_line, helper_source);
    }
    if !app.helper_sources.is_empty() {
        push_generated_line(&mut output, &mut generated_line, "");
    }

    let source_indices = source_indices_by_name(app);
    for (index, route) in app.routes.iter().enumerate() {
        let id = index + 1;
        let prefix = format!("globalThis.__sloppy_handler_{id} = ");
        let handler_start_line = generated_line;
        let handler_start_column = prefix.len();
        let source_map_start_line = handler_start_line + route.handler.source_map_line_offset;
        let source_map_start_column = if route.handler.source_map_line_offset == 0 {
            handler_start_column + route.handler.source_map_column_offset
        } else {
            route.handler.source_map_column_offset
        };
        handler_generated_starts.push(HandlerGeneratedStart {
            generated_line: handler_start_line,
            generated_column: handler_start_column,
        });
        mappings.extend(handler_source_mappings(
            &route.handler.source_text,
            route.handler.span,
            &route.handler.source,
            source_map_start_line,
            source_map_start_column,
            source_index_for(&source_indices, &route.handler.source_name),
        ));

        output.push_str(&prefix);
        if let Some(output_cache) = &route.output_cache {
            let route_key =
                serde_json::to_string(&format!("output:{}:{}", route.method, route.pattern))
                    .unwrap_or_else(|_| "\"\"".to_string());
            let options_json =
                serde_json::to_string(output_cache).unwrap_or_else(|_| "{}".to_string());
            let auth_required = route.auth.is_some();
            let wrapped = format!(
                "async function(ctx) {{ return await __sloppy_output_cache(ctx, {route_key}, {options_json}, {auth_required}, ({})); }}",
                route.handler.emitted_source
            );
            output.push_str(&wrapped);
            output.push_str(";\n");
            generated_line += wrapped.matches('\n').count() + 1;
        } else {
            output.push_str(&route.handler.emitted_source);
            output.push_str(";\n");
            generated_line += route.handler.emitted_source.matches('\n').count() + 1;
        }
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

fn source_indices_by_name(app: &ExtractedApp) -> BTreeMap<&str, usize> {
    app.source_files
        .iter()
        .enumerate()
        .map(|(index, file)| (file.name.as_str(), index))
        .collect()
}

fn estimate_app_js_capacity(app: &ExtractedApp) -> usize {
    let handler_bytes = app
        .routes
        .iter()
        .map(|route| route.handler.emitted_source.len() + 96)
        .sum::<usize>();
    let helper_bytes = app.helper_sources.iter().map(String::len).sum::<usize>();
    handler_bytes + helper_bytes + 8192
}

fn emit_dynamic_web_app_js(source: &str, app: &ExtractedApp) -> EmittedAppJs {
    let mut output = String::with_capacity(source.len() + 8192);
    output.push_str("const __sloppyRuntime = globalThis.__sloppy_runtime;\n");
    output.push_str("if (__sloppyRuntime === undefined) { throw new Error(\"Sloppy bootstrap runtime was not loaded\"); }\n");
    output.push_str("const { Results, Realtime, SloppyRealtimeError, schema, Schema, Environment, data, sql, orm, table, column, relation, SloppyOrmError, SloppyOrmConcurrencyError, Time, File, Directory, Path, Random, Hash, Hmac, Password, ConstantTime, Secret, NonCryptoHash, Base64, Base64Url, Hex, Text, Binary, Compression, Checksums, TcpClient, TcpListener, TcpConnection, NetworkAddress, HttpClient, Http, HttpClientFactory, HttpError, SloppyHttpClientError, TestHttp, System, Process, Signals, OsError, BackgroundService, WorkQueue, WorkerPool, Worker, WorkerCancellationController, WorkerCancellationSignal, SloppyWorkerError, __createFrameworkServiceProvider } = __sloppyRuntime;\n");
    output.push_str(
        r#"const __sloppy_framework_services = __createFrameworkServiceProvider();
const __sloppy_framework_provider_configs = new Map([]);
function __sloppy_framework_arg(ctx, scope, binding) {
  if (binding.kind === "body.json") { return ctx.request.json(); }
  if (binding.kind === "body.form") { return ctx.request.form(); }
  if (binding.kind === "body.multipart") { return ctx.request.multipart(); }
  if (binding.kind === "context") { return ctx; }
  if (binding.kind === "injection") { return __sloppy_framework_injection(scope, binding); }
  if (binding.kind === "config") { const value = Environment.get(binding.name); if (value === undefined) { throw new Error(`sloppy: Config injection for '${binding.name}' requires an environment value.`); } return value; }
  let value;
  if (binding.kind === "route") { value = ctx.route[binding.name]; }
  else if (binding.kind === "query") { value = ctx.query[binding.name]; }
  else if (binding.kind === "header") { value = ctx.request.headers.get(binding.name); }
  else { throw new TypeError(`Sloppy Framework binding kind '${binding.kind}' is not supported.`); }
  return __sloppy_framework_coerce(value, binding);
}
function __sloppy_framework_coerce(value, binding) {
  if (value === null || value === undefined) { return value; }
  const type = String(binding.type || binding.schema || "");
  if (type.includes("boolean")) {
    const normalized = String(value).toLowerCase();
    if (normalized === "true") { return true; }
    if (normalized === "false") { return false; }
    throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected a boolean value.`);
  }
  if (type.includes("number") || type.includes("PositiveInt") || type === "int") {
    const parsed = Number(value);
    if (!Number.isFinite(parsed)) { throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected a numeric value.`); }
    if ((type.includes("PositiveInt") || type === "int") && !Number.isInteger(parsed)) { throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected an integer value.`); }
    if (type.includes("PositiveInt") && parsed <= 0) { throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected a positive integer value.`); }
    return parsed;
  }
  return value;
}
function __sloppy_framework_injection(scope, binding) {
  const dependencyName = binding.capability || (binding.name && binding.name.includes(".") ? binding.name : `data.${binding.name}`);
  if (binding.injectionKind === "service") { return scope.get(binding.name); }
  if (binding.injectionKind === "queue") { return scope.get(dependencyName); }
  if (binding.injectionKind === "provider" && binding.providerKind === "sqlite" && typeof data.sqlite === "function") { return scope.track(data.sqlite(dependencyName)); }
  throw new Error(`sloppy: ${binding.injectionKind} injection for '${binding.name}' is unavailable in this runtime lane.`);
}
"#,
    );
    output.push_str(
        r#"function __sloppy_create_dynamic_app() {
  const routes = [];
  let frozen = false;
  function assertOpen() { if (frozen) { throw new Error("Sloppy app is frozen"); } }
  function register(method, pattern, handler) {
    assertOpen();
    if (typeof pattern !== "string" || pattern.length === 0 || pattern[0] !== "/") { throw new TypeError("Sloppy app route pattern must be an absolute path string."); }
    if (typeof handler !== "function") { throw new TypeError("Sloppy app route handler must be callable."); }
    const route = { method, pattern, handler, name: undefined, metadata: Object.freeze({}) };
    routes.push(route);
    function metadataForSchema(schema) {
      if (schema === undefined || schema === null || typeof schema !== "object" || schema.metadata === undefined) { throw new TypeError("Sloppy endpoint schema metadata is required."); }
      return schema.metadata;
    }
    function setMetadata(key, value) {
      route.metadata = Object.freeze({ ...route.metadata, [key]: Object.freeze(value) });
    }
    return Object.freeze({
      withName(name) { route.name = String(name); return this; },
      accepts(schema) { setMetadata("accepts", { contentType: "application/json", schema: metadataForSchema(schema) }); return this; },
      returns(schema, options = undefined) { setMetadata("returns", { status: options?.status ?? 200, contentType: options?.contentType ?? "application/json", schema: metadataForSchema(schema) }); return this; },
      withTags() { return this; }
    });
  }
  const app = {
    services: __sloppy_framework_services,
    get(pattern, handler) { return register("GET", pattern, handler); },
    post(pattern, handler) { return register("POST", pattern, handler); },
    put(pattern, handler) { return register("PUT", pattern, handler); },
    patch(pattern, handler) { return register("PATCH", pattern, handler); },
    delete(pattern, handler) { return register("DELETE", pattern, handler); },
    mapGet(pattern, handler) { return register("GET", pattern, handler); },
    mapPost(pattern, handler) { return register("POST", pattern, handler); },
    mapPut(pattern, handler) { return register("PUT", pattern, handler); },
    mapPatch(pattern, handler) { return register("PATCH", pattern, handler); },
    mapDelete(pattern, handler) { return register("DELETE", pattern, handler); },
    useStaticFiles(options) {
      assertOpen();
      if (options === null || typeof options !== "object" || typeof options.requestPath !== "string" || typeof options.root !== "string") { throw new TypeError("Sloppy app.useStaticFiles requires literal requestPath and root options."); }
      throw new TypeError("Sloppy app.useStaticFiles is not supported for dynamic fallback routes yet.");
    },
    freeze() { frozen = true; return app; },
    __getRoutes() { return routes.slice(); }
  };
  return app;
}
const Sloppy = Object.freeze({ create: __sloppy_create_dynamic_app, createBuilder() { return { build: __sloppy_create_dynamic_app }; } });
"#,
    );
    output.push_str(source);
    output.push_str(
        r#"
function __sloppy_dynamic_match(pattern, path) {
  const patternParts = pattern.split("/").filter(Boolean);
  const pathParts = path.split("?")[0].split("/").filter(Boolean);
  if (patternParts.length !== pathParts.length) { return null; }
  const route = Object.create(null);
  for (let index = 0; index < patternParts.length; index += 1) {
    const segment = patternParts[index];
    const value = decodeURIComponent(pathParts[index] ?? "");
    if (segment.startsWith("{") && segment.endsWith("}")) {
      const parts = segment.slice(1, -1).split(":");
      if (parts.length > 2) { return null; }
      const [name, kind = "str"] = parts;
      if (value.length === 0) { return null; }
      if (kind !== "str" && kind !== "int" && kind !== "uuid" && kind !== "alpha" && kind !== "float") { return null; }
      if (kind === "int" && !/^[0-9]+$/u.test(value)) { return null; }
      if (kind === "uuid" && !/^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$/u.test(value)) { return null; }
      if (kind === "alpha" && !/^[A-Za-z]+$/u.test(value)) { return null; }
      if (kind === "float" && !(/^[0-9]*\.[0-9]+$/u.test(value) || /^[0-9]+\.[0-9]*$/u.test(value))) { return null; }
      route[name] = value;
    } else if (segment.startsWith(":")) {
      route[segment.slice(1).split(":")[0]] = value;
    } else if (segment !== value) {
      return null;
    }
  }
  return route;
}
function __sloppy_dynamic_response(result) {
  if (typeof result === "string") { return { __sloppyResult: true, kind: "text", status: 200, contentType: "text/plain; charset=utf-8", body: result }; }
  if (result !== null && typeof result === "object" && result.__sloppyResult === true) { return result; }
  if (result !== null && typeof result === "object" && result.kind === undefined && result.status === undefined && result.body === undefined) { return { __sloppyResult: true, kind: "json", status: 200, contentType: "application/json; charset=utf-8", body: result }; }
  const status = Number.isInteger(result?.status) ? result.status : 200;
  const kind = result?.kind ?? "text";
  if (kind === "empty") { return { __sloppyResult: true, kind: "empty", status, contentType: "text/plain; charset=utf-8", body: "" }; }
  if (kind === "json" || kind === "problem") { return { __sloppyResult: true, kind, status, contentType: result.contentType ?? "application/json; charset=utf-8", body: result.body ?? null }; }
  return { __sloppyResult: true, kind: "text", status, contentType: result?.contentType ?? "text/plain; charset=utf-8", body: String(result?.body ?? "") };
}
function __sloppy_dynamic_body_text(response) {
  if (response.kind === "json" || response.kind === "problem") { return response.__sloppyJsonText ?? JSON.stringify(response.body ?? null); }
  return String(response.body ?? "");
}
function __sloppy_dynamic_http_text(response) {
  const body = __sloppy_dynamic_body_text(response);
  return `HTTP/1.1 ${response.status} OK\r\ncontent-type: ${response.contentType}\r\ncontent-length: ${body.length}\r\n\r\n${body}`;
}
globalThis.__sloppy_dispatch_dynamic_result = async function(method, target) {
  const app = globalThis.__sloppy_dynamic_default_app;
  if (app === undefined || typeof app.__getRoutes !== "function") { return { __sloppyResult: true, kind: "text", status: 500, contentType: "text/plain; charset=utf-8", body: "Dynamic Sloppy app was not initialized\n" }; }
  app.freeze();
  const path = String(target).split("?")[0];
  for (const route of app.__getRoutes()) {
    if (route.method !== String(method).toUpperCase()) { continue; }
    const routeValues = __sloppy_dynamic_match(route.pattern, path);
    if (routeValues === null) { continue; }
    const result = await route.handler({ route: routeValues, query: Object.freeze({}), request: Object.freeze({ method, path }) });
    return __sloppy_dynamic_response(result);
  }
  return { __sloppyResult: true, kind: "text", status: 404, contentType: "text/plain; charset=utf-8", body: "Not Found\n" };
};
globalThis.__sloppy_dispatch_dynamic = async function(method, target) {
  return __sloppy_dynamic_http_text(await globalThis.__sloppy_dispatch_dynamic_result(method, target));
};
"#,
    );
    let mut mappings = Vec::with_capacity(app.routes.len());
    let mut handler_generated_starts = Vec::with_capacity(app.routes.len());
    let mut generated_line = output.matches('\n').count();
    let source_indices = source_indices_by_name(app);
    for (index, route) in app.routes.iter().enumerate() {
        let id = index + 1;
        let prefix = format!("globalThis.__sloppy_handler_{id} = ");
        let handler_start_line = generated_line;
        let handler_start_column = prefix.len();
        let source_map_start_line = handler_start_line + route.handler.source_map_line_offset;
        let source_map_start_column = if route.handler.source_map_line_offset == 0 {
            handler_start_column + route.handler.source_map_column_offset
        } else {
            route.handler.source_map_column_offset
        };
        handler_generated_starts.push(HandlerGeneratedStart {
            generated_line: handler_start_line,
            generated_column: handler_start_column,
        });
        mappings.extend(handler_source_mappings(
            &route.handler.source_text,
            route.handler.span,
            &route.handler.source,
            source_map_start_line,
            source_map_start_column,
            source_index_for(&source_indices, &route.handler.source_name),
        ));

        output.push_str(&prefix);
        output.push_str(&route.handler.emitted_source);
        output.push_str(";\n");
        generated_line += route.handler.emitted_source.matches('\n').count() + 1;
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

fn emit_program_app_js(app: &ExtractedApp) -> EmittedAppJs {
    let mut output = String::with_capacity(
        app.program_modules
            .iter()
            .map(|module| module.emitted_source.len())
            .sum::<usize>()
            + 8192,
    );
    output.push_str("(function() {\n");
    output.push_str("  if (!globalThis.__sloppy_runtime) {\n");
    output.push_str("    throw new Error(\"Sloppy bootstrap runtime was not loaded\");\n");
    output.push_str("  }\n");
    output.push_str("  const __sloppy_program_modules = Object.create(null);\n");
    output.push_str("  const __sloppy_program_cache = Object.create(null);\n");
    output.push_str("  const __sloppy_program_resolutions = Object.create(null);\n");
    output.push_str("  const __sloppy_program_package_roots = Object.create(null);\n");
    for package in &app.dependency_graph.packages {
        output.push_str("  __sloppy_program_package_roots[");
        output.push_str(&json_string(&package.name));
        output.push_str("] = ");
        output.push_str(&json_string(&package.root));
        output.push_str(";\n");
    }
    for module in &app.dependency_graph.modules {
        for resolved in &module.resolved_imports {
            output.push_str("  __sloppy_program_resolutions[");
            output.push_str(&json_string(&format!(
                "{}\0{}",
                module.id, resolved.specifier
            )));
            output.push_str("] = ");
            output.push_str(&json_string(&resolved.resolved_id));
            output.push_str(";\n");
        }
    }
    output.push_str("  function __sloppy_program_dirname(id) { const index = String(id).lastIndexOf('/'); return index < 0 ? '.' : String(id).slice(0, index); }\n");
    output.push_str("  function __sloppy_program_resolve(fromId, specifier) {\n");
    output.push_str("    if (__sloppy_program_modules[specifier]) { return specifier; }\n");
    output.push_str(
        "    const mapped = __sloppy_program_resolutions[`${fromId}\\u0000${specifier}`];\n",
    );
    output.push_str("    if (mapped) { return mapped; }\n");
    output.push_str(
        "    if (String(specifier).startsWith('./') || String(specifier).startsWith('../')) {\n",
    );
    output.push_str(
        "      const base = __sloppy_program_dirname(fromId).split('/').filter(Boolean);\n",
    );
    output.push_str("      for (const part of String(specifier).split('/')) { if (!part || part === '.') { continue; } if (part === '..') { base.pop(); } else { base.push(part); } }\n");
    output.push_str("      const raw = base.join('/');\n");
    output.push_str("      for (const candidate of [raw, `${raw}.js`, `${raw}.mjs`, `${raw}.cjs`, `${raw}.ts`, `${raw}.json`, `${raw}/index.js`, `${raw}/index.mjs`, `${raw}/index.cjs`, `${raw}/index.ts`]) { if (__sloppy_program_modules[candidate]) { return candidate; } }\n");
    output.push_str("    }\n");
    output.push_str("    throw new Error(`SLOPPY_E_MODULE_NOT_FOUND: Dynamic import or require resolved to '${specifier}', but that module was not included in the Sloppy artifact graph. Add a moduleInclude pattern for computed imports.`);\n");
    output.push_str("  }\n");
    output.push_str("  function __sloppy_program_require_from(fromId, specifier) { return __sloppy_program_require(__sloppy_program_resolve(fromId, specifier)); }\n");
    output.push_str("  function __sloppy_program_import_from(fromId, specifier) { return Promise.resolve(__sloppy_program_require_from(fromId, specifier)); }\n");
    output.push_str("  function __sloppy_program_canonical_from_id(fromId) {\n");
    output.push_str("    let baseId = String(fromId || \"\").replace(/^file:\\/\\//, \"\").replace(/\\\\/g, \"/\");\n");
    output.push_str("    const packageMatch = baseId.match(/(?:^|\\/)node_modules\\/(@[^\\/]+\\/[^\\/]+|[^\\/]+)(\\/.*)?$/);\n");
    output.push_str("    if (packageMatch && __sloppy_program_package_roots[packageMatch[1]]) { baseId = `${__sloppy_program_package_roots[packageMatch[1]]}${packageMatch[2] || \"/index.js\"}`; }\n");
    output.push_str("    if (__sloppy_program_modules[baseId]) { return baseId; }\n");
    output.push_str("    const suffix = baseId.replace(/^\\/+/, \"\");\n");
    output.push_str("    for (const id of Object.keys(__sloppy_program_modules)) { const normalized = String(id).replace(/\\\\/g, \"/\"); if (normalized === suffix || normalized.endsWith(`/${suffix}`)) { return id; } }\n");
    output.push_str("    return baseId;\n");
    output.push_str("  }\n");
    output.push_str("  function __sloppy_program_require(id) {\n");
    output.push_str(
        "    if (__sloppy_program_cache[id]) { return __sloppy_program_cache[id].exports; }\n",
    );
    output.push_str("    const factory = __sloppy_program_modules[id];\n");
    output.push_str("    if (typeof factory !== \"function\") { throw new Error(`Sloppy program module '${id}' was not found`); }\n");
    output.push_str("    const module = { exports: {} };\n");
    output.push_str("    __sloppy_program_cache[id] = module;\n");
    output.push_str("    const localRequire = function(specifier) { return __sloppy_program_require_from(id, specifier); };\n");
    output.push_str("    localRequire.resolve = function(specifier) { return __sloppy_program_resolve(id, specifier); };\n");
    output.push_str("    localRequire.cache = __sloppy_program_cache;\n");
    output.push_str(
        "    factory(module.exports, module, localRequire, id, __sloppy_program_dirname(id));\n",
    );
    output.push_str("    return module.exports;\n");
    output.push_str("  }\n");
    output.push_str("  globalThis.__sloppy_program_create_require = function(fromId) { const baseId = __sloppy_program_canonical_from_id(fromId); const req = function(specifier) { return __sloppy_program_require_from(baseId, specifier); }; req.resolve = function(specifier) { return __sloppy_program_resolve(baseId, specifier); }; req.cache = __sloppy_program_cache; return req; };\n");
    for module in &app.program_modules {
        output.push_str("  __sloppy_program_modules[");
        output.push_str(&json_string(&module.id));
        output.push_str("] = function(exports, module, require, __filename, __dirname) {\n");
        for line in module.emitted_source.lines() {
            output.push_str("    ");
            output.push_str(line);
            output.push('\n');
        }
        output.push_str("  };\n");
    }
    output.push_str("  function __sloppy_program_format(value) {\n");
    output.push_str("    if (typeof value === \"string\") { return value; }\n");
    output.push_str("    if (typeof value === \"number\" || typeof value === \"boolean\" || typeof value === \"bigint\") { return String(value); }\n");
    output.push_str("    if (value === undefined) { return \"undefined\"; }\n");
    output.push_str("    if (value === null) { return \"null\"; }\n");
    output.push_str("    if (typeof value === \"symbol\") { return String(value); }\n");
    output.push_str("    if (typeof value === \"function\") { return value.name ? `[Function: ${value.name}]` : \"[Function]\"; }\n");
    output.push_str("    if (value instanceof Error) {\n");
    output.push_str(
        "      let text = value.stack || `${value.name || \"Error\"}: ${value.message || \"\"}`;\n",
    );
    output.push_str("      if (typeof AggregateError !== \"undefined\" && value instanceof AggregateError && Array.isArray(value.errors)) {\n");
    output.push_str(
        "        const errors = value.errors.map(__sloppy_program_format).join(\"; \");\n",
    );
    output.push_str("        text = `${text}\\nErrors: ${errors}`;\n");
    output.push_str("      }\n");
    output.push_str("      return text;\n");
    output.push_str("    }\n");
    output.push_str("    const seen = [];\n");
    output.push_str("    try {\n");
    output.push_str("      const text = JSON.stringify(value, function(_key, nested) {\n");
    output.push_str("        if (typeof nested === \"bigint\") { return String(nested); }\n");
    output.push_str("        if (nested !== null && typeof nested === \"object\") {\n");
    output.push_str("          if (seen.indexOf(nested) !== -1) { return \"[Circular]\"; }\n");
    output.push_str("          seen.push(nested);\n");
    output.push_str("        }\n");
    output.push_str("        return nested;\n");
    output.push_str("      });\n");
    output.push_str("      return text === undefined ? String(value) : text;\n");
    output.push_str("    } catch (_error) {\n");
    output.push_str("      return \"[Circular]\";\n");
    output.push_str("    }\n");
    output.push_str("  }\n");
    output.push_str("  function __sloppy_program_console(events, stream, values) {\n");
    output.push_str(
        "    const parts = Array.prototype.slice.call(values).map(__sloppy_program_format);\n",
    );
    output.push_str("    events.push({ stream, text: `${parts.join(\" \")}\\n` });\n");
    output.push_str("  }\n");
    output.push_str("  function __sloppy_program_is_result(value) {\n");
    output.push_str("    return value !== null && typeof value === \"object\" && value.__sloppyResult === true && typeof value.kind === \"string\";\n");
    output.push_str("  }\n");
    output.push_str("  function __sloppy_program_has_exit_code(value) {\n");
    output.push_str("    return value !== null && typeof value === \"object\" && Object.prototype.hasOwnProperty.call(value, \"exitCode\");\n");
    output.push_str("  }\n");
    output.push_str("  function __sloppy_program_exit_code(value) {\n");
    output.push_str("    let code = 0;\n");
    output.push_str("    if (typeof value === \"number\") { code = value; }\n");
    output.push_str(
        "    else if (__sloppy_program_has_exit_code(value)) { code = value.exitCode; }\n",
    );
    output.push_str("    else { return 0; }\n");
    output.push_str("    if (!Number.isInteger(code) || code < 0 || code > 255) {\n");
    output.push_str(
        "      throw new Error(\"Sloppy program exit code must be an integer from 0 to 255\");\n",
    );
    output.push_str("    }\n");
    output.push_str("    return code;\n");
    output.push_str("  }\n");
    output.push_str("  function __sloppy_program_result(value, events) {\n");
    output.push_str("    const exitCode = __sloppy_program_exit_code(value);\n");
    output.push_str("    if (events.length === 0 && (typeof value === \"string\" || __sloppy_program_is_result(value))) { return value; }\n");
    output.push_str("    return { __sloppyResult: true, kind: \"json\", status: 200, contentType: \"application/json\", body: { __sloppyProgramResult: true, exitCode, events } };\n");
    output.push_str("  }\n");
    let entry = app
        .program_entry
        .as_deref()
        .or_else(|| app.program_modules.last().map(|module| module.id.as_str()))
        .unwrap_or("");
    output.push_str(
        "  globalThis.__sloppy_program_main = async function __sloppy_program_main() {\n",
    );
    output.push_str("    const events = [];\n");
    output.push_str("    const args = Array.isArray(globalThis.__sloppy_program_args) ? globalThis.__sloppy_program_args.slice() : [];\n");
    output.push_str("    const ctx = globalThis.__sloppy_program_context && typeof globalThis.__sloppy_program_context === \"object\" ? globalThis.__sloppy_program_context : { kind: \"program\", args, cwd: \"\", environment: \"Development\", plan: { kind: \"program\", metadataCompleteness: \"opaque\" } };\n");
    output.push_str("    const previousConsole = globalThis.console;\n");
    output.push_str("    const previousGlobal = globalThis.global;\n");
    output.push_str("    const previousProcess = globalThis.process;\n");
    output.push_str("    const previousBuffer = globalThis.Buffer;\n");
    output.push_str("    const programConsole = Object.create(previousConsole || null);\n");
    output.push_str(
        "    programConsole.log = function() { __sloppy_program_console(events, \"stdout\", arguments); };\n",
    );
    output.push_str(
        "    programConsole.info = function() { __sloppy_program_console(events, \"stdout\", arguments); };\n",
    );
    output.push_str(
        "    programConsole.debug = function() { __sloppy_program_console(events, \"stdout\", arguments); };\n",
    );
    output.push_str(
        "    programConsole.warn = function() { __sloppy_program_console(events, \"stderr\", arguments); };\n",
    );
    output.push_str(
        "    programConsole.error = function() { __sloppy_program_console(events, \"stderr\", arguments); };\n",
    );
    output.push_str("    Object.freeze(programConsole);\n");
    output.push_str("    let value;\n");
    output.push_str("    let failed = false;\n");
    output.push_str("    try {\n");
    output.push_str("    globalThis.console = programConsole;\n");
    output.push_str("    if (previousGlobal === undefined) { globalThis.global = globalThis; }\n");
    output.push_str("    if (__sloppy_program_modules[\"sloppy/node/process\"]) { globalThis.process = __sloppy_program_require(\"sloppy/node/process\"); }\n");
    output.push_str("    if (__sloppy_program_modules[\"sloppy/node/buffer\"]) { globalThis.Buffer = __sloppy_program_require(\"sloppy/node/buffer\").Buffer; }\n");
    output.push_str("    const entry = __sloppy_program_require(");
    output.push_str(&json_string(entry));
    output.push_str(");\n");
    output.push_str(
        "    if (typeof entry.main === \"function\") { value = await entry.main(args, ctx); }\n",
    );
    output.push_str(
        "    else if (typeof entry.default === \"function\") { value = await entry.default(args, ctx); }\n",
    );
    output.push_str("    } catch (error) {\n");
    output.push_str("      failed = true;\n");
    output.push_str("      __sloppy_program_console(events, \"stderr\", [error]);\n");
    output.push_str("      value = { exitCode: 1 };\n");
    output.push_str("    } finally {\n");
    output.push_str("      if (previousConsole === undefined) {\n");
    output.push_str("        try { delete globalThis.console; } catch (_error) { globalThis.console = undefined; }\n");
    output.push_str("      } else {\n");
    output.push_str("        globalThis.console = previousConsole;\n");
    output.push_str("      }\n");
    output.push_str("      if (previousGlobal === undefined) { try { delete globalThis.global; } catch (_error) { globalThis.global = undefined; } } else { globalThis.global = previousGlobal; }\n");
    output.push_str("      if (previousProcess === undefined) { try { delete globalThis.process; } catch (_error) { globalThis.process = undefined; } } else { globalThis.process = previousProcess; }\n");
    output.push_str("      if (previousBuffer === undefined) { try { delete globalThis.Buffer; } catch (_error) { globalThis.Buffer = undefined; } } else { globalThis.Buffer = previousBuffer; }\n");
    output.push_str("    }\n");
    output.push_str("    if (!failed) {\n");
    output.push_str("      try { return __sloppy_program_result(value, events); }\n");
    output.push_str("      catch (error) { __sloppy_program_console(events, \"stderr\", [error]); value = { exitCode: 1 }; }\n");
    output.push_str("    }\n");
    output.push_str("    return __sloppy_program_result(value, events);\n");
    output.push_str("  };\n");
    output.push_str("})();\n");
    EmittedAppJs {
        source: output,
        mappings: Vec::new(),
        handler_generated_starts: Vec::new(),
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
                .get(index)
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
            if route.kind != "http" {
                route_json["kind"] = json!(route.kind);
            }
            if let Some(websocket) = &route.websocket {
                route_json["websocket"] = websocket_options_json(websocket);
            }
            if let Some(realtime) = &route.realtime {
                route_json["realtime"] = json!({
                    "kind": "framework",
                    "transport": "websocket",
                    "metadataStatus": "partial",
                    "partialReason": "channel and option expressions are preserved; static event and schema extraction is not complete in this alpha",
                    "channelExpression": realtime.channel_source,
                    "optionsExpression": realtime.options_source
                });
            }
            if let Some(framework_path) = &route.framework_path {
                route_json["frameworkPath"] = json!(framework_path);
            }
            if !route.tags.is_empty() {
                route_json["tags"] = json!(route.tags);
            }
            if let Some(health) = &route.health {
                route_json["health"] = json!({
                    "kind": health.kind,
                    "checks": health.checks
                });
            }
            if let Some(output_cache) = &route.output_cache {
                route_json["outputCache"] = output_cache.clone();
            }
            if let Some(cache_headers) = &route.cache_headers {
                route_json["cacheHeaders"] = cache_headers.clone();
            }
            if !route.middleware.is_empty() {
                route_json["middleware"] = json!(route
                    .middleware
                    .iter()
                    .map(|middleware| {
                        json!({
                            "kind": middleware.kind,
                            "sequence": middleware.sequence,
                            "source": source_location_json(
                                &middleware.source_name,
                                &middleware.source_text,
                                middleware.span
                            )
                        })
                    })
                    .collect::<Vec<_>>());
            }
            if let Some(cors) = &route.cors {
                route_json["cors"] = json!({
                    "origins": cors.origins,
                    "methods": cors.methods,
                    "headers": cors.headers,
                    "exposedHeaders": cors.exposed_headers,
                    "credentials": cors.credentials,
                    "maxAgeSeconds": cors.max_age_seconds,
                    "preflight": route.cors_preflight
                });
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
    let mut x_sloppy = json!({
        "version": 1,
        "sourceFiles": source_files,
        "handlers": handlers,
        "routes": routes,
        "modules": modules,
        "schemas": schemas,
        "providers": providers,
        "capabilities": capabilities,
        "effects": effects
    });
    if app.kind == ProjectKind::Program {
        let program_modules = app
            .program_modules
            .iter()
            .map(|module| {
                json!({
                    "id": module.id,
                    "source": {
                        "path": module.source_name
                    },
                    "format": module.format.as_str(),
                    "hash": sha256_hex(&module.source)
                })
            })
            .collect::<Vec<_>>();
        x_sloppy["kind"] = json!(app.kind.as_str());
        x_sloppy["programModules"] = json!(program_modules);
        if app.dependency_graph.has_entries() {
            x_sloppy["dependencyGraph"] = dependency_graph_json(&app.dependency_graph);
        }
    }
    let value = json!({
        "version": 3,
        "file": "app.js",
        "sources": sources,
        "sourcesContent": sources_content,
        "names": [],
        "mappings": encode_source_map_mappings(mappings),
        "x_sloppy": x_sloppy
    });

    let json = serde_json::to_string_pretty(&value).unwrap_or_else(|_| "{}".to_string());
    format!("{json}\n")
}

fn websocket_options_json(options: &WebSocketRouteOptionsMetadata) -> Value {
    let origins = match &options.origins {
        Some(WebSocketOriginsMetadata::Any) => json!("*"),
        Some(WebSocketOriginsMetadata::List(origins)) => json!(origins),
        None => Value::Null,
    };
    json!({
        "protocols": &options.protocols,
        "origins": origins,
        "maxMessageBytes": options.max_message_bytes,
        "maxSendQueueBytes": options.max_send_queue_bytes,
        "heartbeatMs": options.heartbeat_ms,
        "idleTimeoutMs": options.idle_timeout_ms,
        "closeTimeoutMs": options.close_timeout_ms,
        "slowClientPolicy": options.slow_client_policy.as_ref(),
        "compression": options.compression.unwrap_or(false)
    })
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

fn source_index_for(source_indices: &BTreeMap<&str, usize>, source_name: &str) -> usize {
    source_indices.get(source_name).copied().unwrap_or(0)
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

#[cfg(test)]
#[path = "sloppyc_tests.rs"]
mod tests;
