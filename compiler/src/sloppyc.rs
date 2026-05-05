use std::{
    collections::{BTreeMap, BTreeSet},
    ffi::OsString,
    fs,
    path::{Path, PathBuf},
};

use oxc_allocator::Allocator;
use oxc_ast::ast::{
    Argument, ArrayExpressionElement, BindingPattern, CallExpression, ChainElement, Declaration,
    Expression, ExpressionStatement, ForStatementInit, ImportDeclarationSpecifier,
    ObjectPropertyKind, PropertyKey, PropertyKind, Statement,
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

const COMPILER_VERSION: &str = "sloppyc-0.8.0-engine-02";
const RUNTIME_MINIMUM_VERSION: &str = "0.1.0";
const STDLIB_VERSION: &str = "0.1.0";

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
}

impl CompileOptions {
    pub fn new() -> Self {
        Self {
            environment: None,
            host: None,
            port: None,
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
    source_name: String,
    source_text: String,
    bindings: Vec<RequestBinding>,
    response: Option<ResponseMetadata>,
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
    source_files: Vec<SourceFile>,
    routes: Vec<Route>,
    modules: Vec<FunctionModule>,
    helper_sources: Vec<String>,
    capabilities: Vec<DatabaseCapability>,
    configuration: Option<ConfigurationPlan>,
    schemas: Vec<SchemaMetadata>,
    config_reads: Vec<ConfigReadMetadata>,
    uses_time_runtime: bool,
    uses_fs_runtime: bool,
}

#[derive(Debug, Clone)]
struct RequestBinding {
    kind: String,
    name: Option<String>,
    schema: Option<String>,
}

#[derive(Debug, Clone)]
struct ResponseMetadata {
    helper: String,
    status: u16,
    kind: String,
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
    source_name: String,
    source: String,
    span: Span,
}

fn schema_names(state: &AppState) -> BTreeSet<String> {
    if state.schema_imported {
        state.schema_names.clone()
    } else {
        BTreeSet::new()
    }
}

#[derive(Debug, Clone)]
struct ConfigurationPlan {
    environment: String,
    keys: Vec<ConfigurationPlanKey>,
    providers: Vec<ConfigurationProviderPlan>,
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
    schema_imported: bool,
    time_imported: bool,
    fs_imported: bool,
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
            schema_imported: false,
            time_imported: false,
            fs_imported: false,
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
        "  sloppyc build <input.js> --out <directory> [--environment <name>] [--host <host>] [--port <port>]\n",
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
    let configuration = ConfigurationModel::load(input, options).map_err(|diagnostic| {
        let diagnostic_source = diagnostic_render_source(input, &source, &diagnostic);
        Box::new(CompileError {
            code: 1,
            diagnostic,
            source: diagnostic_source,
        })
    })?;
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
    fn load(input: &Path, options: &CompileOptions) -> Result<Self, Diagnostic> {
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
        model.apply_environment_variables()?;
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
                self.set(&next.join(":"), value.clone(), source);
            }
        }
        Ok(())
    }

    fn apply_environment_variables(&mut self) -> Result<(), Diagnostic> {
        for (name, value) in std::env::vars() {
            if !name.starts_with("SLOPPY_SLOPPY__") {
                continue;
            }
            let logical = &name["SLOPPY_".len()..];
            if logical.is_empty()
                || logical.contains("___")
                || logical.starts_with('_')
                || logical.split("__").any(|segment| segment.is_empty())
            {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_CONFIG_ENV",
                    format!("invalid Sloppy environment variable name '{name}'"),
                )
                .with_hint("Use SLOPPY_SLOPPY__SERVER__PORT style names."));
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
    }

    fn apply_to_app(&self, app: &mut ExtractedApp) -> Result<(), Diagnostic> {
        let mut provider_plans = Vec::new();
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
        }

        app.configuration = Some(ConfigurationPlan {
            environment: self.environment.clone(),
            keys: self.plan_keys(),
            providers: provider_plans,
        });
        Ok(())
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
    let normalized = key.to_ascii_lowercase();
    let has_sensitive_segment = normalized
        .split(':')
        .any(|segment| matches!(segment, "pwd" | "passwd"));
    normalized.contains("secret")
        || normalized.contains("password")
        || normalized.contains("token")
        || normalized.contains("connectionstring")
        || normalized.contains("connection_string")
        || normalized.contains("apikey")
        || normalized.contains("api_key")
        || has_sensitive_segment
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
        .with_hint(
            "Only the public bare import \"sloppy\" is accepted; Sloppy does not implement Node or npm resolution.",
        ));
    }

    if let Some((specifier, span)) = &state.unsupported_import_name {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT",
            format!("unsupported sloppy import \"{specifier}\""),
        )
        .with_path(path)
        .with_span(*span)
        .with_hint("Use import { Sloppy, Results } from \"sloppy\"; or add data only when provider metadata is needed."));
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
            || state.sqlite_imported
            || !state.app_provider_uses.is_empty()
            || state
                .routes
                .iter()
                .any(|route| !route.handler.effects.is_empty()),
        source_files: graph.source_files.clone(),
        routes: state.routes,
        modules: state.modules.into_values().collect(),
        helper_sources,
        capabilities: state.capabilities,
        configuration: None,
        schemas: state.schemas,
        config_reads: state.config_reads,
        uses_time_runtime: state.time_imported,
        uses_fs_runtime: state.fs_imported,
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
        let Statement::VariableDeclaration(declaration) = statement else {
            continue;
        };
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
    names
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

    if import_source == "sloppy/providers/sqlite" {
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
                } else {
                    state.unsupported_import_name = Some((imported.to_string(), specifier.span));
                }
            }
        }
        return Ok(());
    }

    if import_source == "sloppy/fs" {
        if let Some(specifiers) = &import.specifiers {
            if specifiers.is_empty() {
                state.unsupported_import_specifier =
                    Some((import_source.to_string(), import.source.span));
                return Ok(());
            }
            for specifier in specifiers {
                let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
                    state.unsupported_import_specifier =
                        Some((import_source.to_string(), import.source.span));
                    return Ok(());
                };
                let imported = specifier.imported.name().as_str();
                let local = specifier.local.name.as_str();
                if matches!(
                    imported,
                    "File" | "Directory" | "Path" | "FileHandle" | "FileWatcher"
                ) && imported == local
                {
                    state.fs_imported = true;
                } else {
                    if matches!(
                        imported,
                        "File" | "Directory" | "Path" | "FileHandle" | "FileWatcher"
                    ) {
                        state.unsupported_import_alias = true;
                    }
                    state.unsupported_import_name = Some((imported.to_string(), specifier.span));
                }
            }
        } else {
            state.unsupported_import_specifier =
                Some((import_source.to_string(), import.source.span));
        }
        return Ok(());
    }

    if import_source == "sloppy/time" {
        if let Some(specifiers) = &import.specifiers {
            if specifiers.is_empty() {
                state.unsupported_import_specifier =
                    Some((import_source.to_string(), import.source.span));
                return Ok(());
            }
            for specifier in specifiers {
                let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
                    state.unsupported_import_specifier =
                        Some((import_source.to_string(), import.source.span));
                    return Ok(());
                };
                let imported = specifier.imported.name().as_str();
                let local = specifier.local.name.as_str();
                if matches!(
                    imported,
                    "Time"
                        | "Deadline"
                        | "CancellationController"
                        | "TimeoutError"
                        | "CancelledError"
                        | "InvalidDeadlineError"
                        | "TimerDisposedError"
                ) && imported == local
                {
                    state.time_imported = true;
                } else {
                    if matches!(
                        imported,
                        "Time"
                            | "Deadline"
                            | "CancellationController"
                            | "TimeoutError"
                            | "CancelledError"
                            | "InvalidDeadlineError"
                            | "TimerDisposedError"
                    ) {
                        state.unsupported_import_alias = true;
                    }
                    state.unsupported_import_name = Some((imported.to_string(), specifier.span));
                }
            }
        } else {
            state.unsupported_import_specifier =
                Some((import_source.to_string(), import.source.span));
        }
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
            if matches!(imported, "Sloppy" | "Results" | "data" | "schema") && imported != local {
                state.unsupported_import_alias = true;
                state.unsupported_import_name = Some((imported.to_string(), specifier.span));
            }
            match (imported, local) {
                ("Sloppy", "Sloppy") => state.sloppy_imported = true,
                ("Results", "Results") => state.results_imported = true,
                ("data", "data") => state.data_imported = true,
                ("schema", "schema") => state.schema_imported = true,
                ("Sloppy" | "Results" | "data" | "schema", _) => {}
                _ => {
                    state.unsupported_import_name = Some((imported.to_string(), specifier.span));
                }
            }
        }
    }
    Ok(())
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
            } else if let Some(config_read) =
                config_read_metadata(path, source, source_name, state, init)?
            {
                state.config_reads.push(config_read);
            } else if let Some(diagnostic) = malformed_config_read_diagnostic(path, state, init) {
                return Err(diagnostic);
            } else {
                validate_supported_initializer(path, source, source_name, state, init)?;
            }
        } else if let Some(config_read) =
            config_read_metadata(path, source, source_name, state, init)?
        {
            state.config_reads.push(config_read);
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

    let (route_expr, name) = match &statement.expression {
        Expression::CallExpression(call) => match with_name_call(call)? {
            Some((inner, name)) => (inner, Some(name)),
            None => (&statement.expression, None),
        },
        _ => (&statement.expression, None),
    };

    let schema_names = schema_names(state);
    let Some((receiver, method, pattern, handler)) = route_call(
        route_expr,
        source,
        source_name,
        state.data_imported,
        &schema_names,
        &state.provider_bindings,
        &state.helper_effects,
    ) else {
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

    if !route_pattern_supported(&full_pattern) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_PATTERN",
            "route pattern is outside the Plan v1 alpha route syntax",
        )
        .with_path(path)
        .with_span(statement.span)
        .with_hint("Use '/', static segments, {name}, {name:str}, or {name:int}."));
    }

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
                "Provider handle '{name}' is recognized for Plan metadata, but only the SQLite generated bridge is executable in this compiler slice."
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
        pattern: full_pattern,
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
            handler_from_argument(
                argument,
                source,
                "",
                state.data_imported,
                &schema_names(state),
                &state.provider_bindings,
                &state.helper_effects,
            )
        })
        .is_none()
    {
        let handler_argument = call.arguments.get(1)?;
        return Some(handler_diagnostic(path, handler_argument, call.span));
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
        .with_hint("Use a first-party database provider value or a future provider plugin task."));
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
) -> Result<Option<ConfigReadMetadata>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((key, value_type, has_default)) = config_call_metadata(call, state) else {
        return Ok(None);
    };
    Ok(Some(ConfigReadMetadata {
        key,
        value_type,
        has_default,
        source_name: source_name.to_string(),
        source: source.to_string(),
        span: call.span,
    }))
}

fn config_call_metadata(
    call: &CallExpression<'_>,
    state: &AppState,
) -> Option<(String, String, bool)> {
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
        "getBool" => "bool",
        _ => return None,
    };
    Some((key, value_type.to_string(), call.arguments.len() > 1))
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
    if !matches!(method, "getString" | "getInt" | "getNumber" | "getBool") {
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
        .with_hint("Keep function modules acyclic for the framework MVP."));
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
                let Some((receiver, method, pattern, mut handler)) = route_call(
                    route_expr,
                    source,
                    source_name,
                    false,
                    &BTreeSet::new(),
                    &providers,
                    &BTreeMap::new(),
                ) else {
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
                if !route_pattern_supported(&full_pattern) {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_ROUTE_PATTERN",
                        "route pattern is outside the Plan v1 alpha route syntax",
                    )
                    .with_path(path)
                    .with_span(statement.span)
                    .with_hint("Use '/', static segments, {name}, {name:str}, or {name:int}."));
                }

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
                            "Provider handle '{name}' is recognized for Plan metadata, but only the SQLite generated bridge is executable in this compiler slice."
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
                    pattern: full_pattern,
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
    let handler = handler_from_argument(
        handler_arg,
        source,
        source_name,
        allow_data_handler_body,
        schema_names,
        provider_bindings,
        helper_effects,
    )?;
    Some((receiver, method, pattern, handler))
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
    source: &str,
    source_name: &str,
    allow_data_handler_body: bool,
    schema_names: &BTreeSet<String>,
    provider_bindings: &BTreeMap<String, ProviderBinding>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
) -> Option<Handler> {
    match argument {
        Argument::ArrowFunctionExpression(function) => {
            let effects = function_effects_from_arrow(
                function,
                provider_bindings,
                helper_effects,
                source,
                source_name,
            );
            if handler_parameters_are_unsupported(&function.params)
                || arrow_has_typescript_syntax(function)
                || effects.unknown_provider_usage
                || (!allow_data_handler_body
                    && effects.effects.is_empty()
                    && !handler_body_is_supported_arrow(function, schema_names))
            {
                return None;
            }
            let handler_source = source_slice(source, function.span)?;
            let schema_spans = handler_context_parameter_name(&function.params)
                .map(|ctx_name| {
                    body_json_schema_argument_spans_arrow(function, &ctx_name, schema_names)
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
                source_name: source_name.to_string(),
                source_text: source.to_string(),
                bindings: request_bindings_from_arrow(function, schema_names),
                response: response_metadata_from_arrow(function),
                effects: effects.effects,
            })
        }
        Argument::FunctionExpression(function) => {
            let effects = function_effects_from_function(
                function,
                provider_bindings,
                helper_effects,
                source,
                source_name,
            );
            if handler_parameters_are_unsupported(&function.params)
                || function_has_typescript_syntax(function)
                || effects.unknown_provider_usage
                || (!allow_data_handler_body
                    && effects.effects.is_empty()
                    && !handler_body_is_supported_function(function, schema_names))
            {
                return None;
            }
            let handler_source = source_slice(source, function.span)?;
            let schema_spans = handler_context_parameter_name(&function.params)
                .map(|ctx_name| {
                    body_json_schema_argument_spans_function(function, &ctx_name, schema_names)
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
                source_name: source_name.to_string(),
                source_text: source.to_string(),
                bindings: request_bindings_from_function(function, schema_names),
                response: response_metadata_from_function(function),
                effects: effects.effects,
            })
        }
        _ => None,
    }
}

fn handler_diagnostic(path: &Path, argument: &Argument<'_>, fallback_span: Span) -> Diagnostic {
    let schema_names = BTreeSet::new();
    let (code, message, hint) = match argument {
        Argument::ArrowFunctionExpression(function) => {
            if handler_parameters_are_unsupported(&function.params) {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS",
                    "route handlers compiled by this MVP may declare zero parameters or one simple context parameter",
                    Some("The current runtime passes one plain request context object."),
                )
            } else if arrow_has_typescript_syntax(function) {
                (
                    "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_HANDLER",
                    "TypeScript handler syntax is not supported by the supported app compiler",
                    Some("Use JavaScript handler syntax until TypeScript lowering is implemented."),
                )
            } else if handler_result_uses_unsupported_values_arrow(function, &schema_names) {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE",
                    "route handler result arguments must be inline JSON-safe values",
                    Some("Use literals, arrays, and object literals instead of closed-over bindings."),
                )
            } else if function.r#async {
                (
                    "SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY",
                    "async handler extraction currently supports one direct Results.* return",
                    Some("Keep async compiler fixtures to direct Results.* returns until ENGINE-03 owns Promise settlement."),
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
                    "route handlers compiled by this MVP may declare zero parameters or one simple context parameter",
                    Some("The current runtime passes one plain request context object."),
                )
            } else if function_has_typescript_syntax(function) {
                (
                    "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_HANDLER",
                    "TypeScript handler syntax is not supported by the supported app compiler",
                    Some("Use JavaScript handler syntax until TypeScript lowering is implemented."),
                )
            } else if handler_result_uses_unsupported_values_function(function, &schema_names) {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE",
                    "route handler result arguments must be inline JSON-safe values",
                    Some("Use literals, arrays, and object literals instead of closed-over bindings."),
                )
            } else if function.r#async {
                (
                    "SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY",
                    "async handler extraction currently supports one direct Results.* return",
                    Some("Keep async compiler fixtures to direct Results.* returns until ENGINE-03 owns Promise settlement."),
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
        Expression::Identifier(identifier) => {
            if summary
                .provider_bindings
                .contains_key(identifier.name.as_str())
            {
                summary.unknown_provider_usage = true;
            }
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

fn response_metadata_from_call(call: &CallExpression<'_>) -> Option<ResponseMetadata> {
    let (_, helper) = static_member_name(&call.callee)?;
    let (status, kind) = match helper {
        "ok" => (200, "json"),
        "json" => (200, "json"),
        "text" => (200, "text"),
        "html" => (200, "html"),
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
        "text" | "html" => matches!(call.arguments.len(), 1 | 2),
        "json" | "ok" | "accepted" | "notFound" | "badRequest" => call.arguments.len() <= 2,
        "created" | "status" => (1..=3).contains(&call.arguments.len()),
        "noContent" => call.arguments.is_empty(),
        "problem" => call.arguments.len() <= 2,
        _ => false,
    };

    argument_count_supported
        && call.arguments.iter().all(|argument| {
            argument_is_inline_json_safe_value(argument, allowed_roots, schema_names)
        })
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
                || !route.handler.effects.is_empty()
        });
    let handlers = app
        .routes
        .iter()
        .enumerate()
        .map(|(index, route)| {
            let id = index + 1;
            let (line, column) = line_column(&route.handler.source_text, route.handler.span.start);
            json!({
                "async": route.handler.is_async,
                "id": id,
                "exportName": format!("__sloppy_handler_{id}"),
                "displayName": route.name.clone().unwrap_or_else(|| format!("{} {}", route.method, route.pattern)),
                "source": {
                    "path": route.handler.source_name,
                    "line": line,
                    "column": column
                }
            })
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
                runtime_only: false,
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
            let emits_route_metadata = emits_app_metadata
                || !route.handler.bindings.is_empty()
                || route.handler.response.is_some()
                || !route.handler.effects.is_empty();
            if !route.handler.bindings.is_empty() {
                route_json["bindings"] = json!(route
                    .handler
                    .bindings
                    .iter()
                    .map(|binding| {
                        json!({
                            "kind": binding.kind,
                            "name": binding.name,
                            "schema": binding.schema
                        })
                    })
                    .collect::<Vec<_>>());
            }
            if emits_route_metadata {
                if let Some(response) = &route.handler.response {
                    route_json["response"] = json!({
                        "helper": response.helper,
                        "status": response.status,
                        "kind": response.kind
                    });
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
        json!({
            "environment": configuration.environment,
            "keys": keys,
            "providers": providers
        })
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
    if !required_features.is_empty() {
        value["requiredFeatures"] = json!(required_features);
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
    push_generated_line(
        &mut output,
        &mut generated_line,
        &format!(
            "const {{ {} }} = __sloppyRuntime;",
            runtime_exports.join(", ")
        ),
    );
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
            json!({
                "handlerId": index + 1,
                "method": route.method,
                "pattern": route.pattern,
                "module": route.module,
                "source": source_location_json(&route.source_name, &route.source, route.span)
            })
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
        canonical_config_key, command_from_args, extract, route_pattern_supported, CliCommand,
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
            ]),
            CliCommand::Build {
                input: std::path::PathBuf::from("app.js"),
                out_dir: std::path::PathBuf::from(".sloppy"),
                options: CompileOptions {
                    environment: Some("Development".to_string()),
                    host: Some("127.0.0.1".to_string()),
                    port: Some(5173),
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
        };
        let config =
            super::ConfigurationModel::load(&input, &options).expect("configuration should load");
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
            source_files: Vec::new(),
            routes: Vec::new(),
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
        };
        config
            .apply_to_app(&mut app)
            .expect("provider config should bind");
        assert_eq!(app.capabilities[0].database.as_deref(), Some("dev.db"));
        assert!(app.configuration.is_some());

        fs::remove_dir_all(&root).expect("config test directory should be removable");
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

        let diagnostic = super::ConfigurationModel::load(&input, &super::CompileOptions::new())
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
export default app;
"#;
        let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
        assert_eq!(app.routes.len(), 9);
        assert_eq!(app.routes[0].pattern, "/ok");
        assert_eq!(app.routes[1].pattern, "/empty");
        assert_eq!(app.routes[2].pattern, "/created");
        assert_eq!(app.routes[7].pattern, "/problem");
        assert_eq!(app.routes[8].pattern, "/html");
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
    fn extracts_engine_02_metadata_without_runtime_claims() {
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
            super::ConfigurationModel::load(&fixture, &CompileOptions::new())
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
