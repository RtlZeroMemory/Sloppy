use std::{
    collections::{BTreeMap, BTreeSet},
    ffi::OsString,
    fs,
    path::{Path, PathBuf},
};

use oxc_allocator::Allocator;
use oxc_ast::ast::{
    Argument, ArrayExpressionElement, BindingPattern, CallExpression, Declaration, Expression,
    ExpressionStatement, ImportDeclarationSpecifier, ObjectPropertyKind, PropertyKey, PropertyKind,
    Statement,
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
}

#[derive(Debug, Clone)]
struct DatabaseCapability {
    token: String,
    provider: &'static str,
    config_name: Option<String>,
    access: String,
    database: Option<String>,
    config_source: Option<String>,
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
struct EmittedAppJs {
    source: String,
    mappings: Vec<SourceMapMapping>,
}

#[derive(Debug)]
struct ExtractedApp {
    uses_data_runtime: bool,
    source_files: Vec<SourceFile>,
    routes: Vec<Route>,
    capabilities: Vec<DatabaseCapability>,
    configuration: Option<ConfigurationPlan>,
    schemas: Vec<SchemaMetadata>,
    config_reads: Vec<ConfigReadMetadata>,
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

#[derive(Debug)]
struct AppState {
    sloppy_imported: bool,
    results_imported: bool,
    data_imported: bool,
    schema_imported: bool,
    sqlite_imported: bool,
    unsupported_import_alias: bool,
    unsupported_import_name: Option<(String, Span)>,
    unsupported_import_specifier: Option<(String, Span)>,
    dynamic_import: Option<Span>,
    app_vars: BTreeSet<String>,
    builder_vars: BTreeSet<String>,
    group_vars: BTreeMap<String, String>,
    provider_bindings: BTreeMap<String, String>,
    app_provider_uses: BTreeSet<String>,
    imported_modules: Vec<ImportedModule>,
    used_modules: Vec<(String, Span)>,
    routes: Vec<Route>,
    capabilities: Vec<DatabaseCapability>,
    schemas: Vec<SchemaMetadata>,
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
            sqlite_imported: false,
            unsupported_import_alias: false,
            unsupported_import_name: None,
            unsupported_import_specifier: None,
            dynamic_import: None,
            app_vars: BTreeSet::new(),
            builder_vars: BTreeSet::new(),
            group_vars: BTreeMap::new(),
            provider_bindings: BTreeMap::new(),
            app_provider_uses: BTreeSet::new(),
            imported_modules: Vec::new(),
            used_modules: Vec::new(),
            routes: Vec::new(),
            capabilities: Vec::new(),
            schemas: Vec::new(),
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
            if capability.provider != "sqlite" {
                continue;
            }
            let provider_name = provider_config_name(capability);
            let prefix = format!("Sloppy:Providers:sqlite:{provider_name}");
            if capability.database.is_none() {
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
                            "sqlite provider '{provider_name}' is missing required config value {database_key}"
                        ),
                    )
                    .with_hint(
                        "Add Sloppy:Providers:sqlite:<name>:database to appsettings.json or pass inline sqlite options.",
                    ));
                }
            }
            if let Some(source) = capability.config_source.clone() {
                provider_plans.push(ConfigurationProviderPlan {
                    provider: "sqlite".to_string(),
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
    token.strip_prefix("sqlite:").unwrap_or(token).to_string()
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
            Statement::ExpressionStatement(statement) => {
                extract_expression_statement(path, source, &source_name, &mut state, statement)?
            }
            Statement::ExportDefaultDeclaration(export) => {
                state.default_export = export_default_identifier(&export.declaration);
            }
            _ => return Err(top_level_statement_diagnostic(path, source, statement)),
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

    Ok(ExtractedApp {
        uses_data_runtime: state.data_imported
            || state.sqlite_imported
            || !state.app_provider_uses.is_empty(),
        source_files: graph.source_files.clone(),
        routes: state.routes,
        capabilities: state.capabilities,
        configuration: None,
        schemas: state.schemas,
        config_reads: state.config_reads,
    })
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
        } else if let Some(provider) = sqlite_provider_call(init) {
            state
                .provider_bindings
                .insert(name.to_string(), provider.token);
        } else if let Some(token) = app_provider_lookup(init, state) {
            state.provider_bindings.insert(name.to_string(), token);
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

fn extract_expression_statement(
    path: &Path,
    source: &str,
    source_name: &str,
    state: &mut AppState,
    statement: &ExpressionStatement<'_>,
) -> Result<(), Diagnostic> {
    if let Some(capability) = database_capability_call(path, &statement.expression, state)? {
        state.capabilities.push(capability);
        return Ok(());
    }

    if let Some(provider) = app_use_provider_call(path, &statement.expression, state)? {
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

    let Some((receiver, method, pattern, handler)) =
        route_call(route_expr, source, source_name, state.data_imported)
    else {
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
        .and_then(|argument| handler_from_argument(argument, source, "", state.data_imported))
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
    if let Some((_, _, _, _)) = route_call(init, source, source_name, state.data_imported) {
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
    if provider != "sqlite" {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_DATA_PROVIDER",
            "compiler-emitted provider metadata currently supports sqlite only",
        )
        .with_path(path)
        .with_span(options.span)
        .with_hint("PostgreSQL and SQL Server JavaScript bridges remain deferred."));
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
        provider: "sqlite",
        config_name: None,
        access,
        database,
        config_source: None,
        from_provider_use: false,
    }))
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

fn app_provider_lookup(expression: &Expression<'_>, state: &AppState) -> Option<String> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if property != "provider" || !state.app_vars.contains(receiver) || call.arguments.len() != 1 {
        return None;
    }
    let token = string_argument(call.arguments.first()?)?;
    Some(normalize_sqlite_provider_token(
        token.strip_prefix("sqlite:").unwrap_or(token),
    ))
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
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    schema_definition_call(call)
}

fn schema_definition_call(call: &CallExpression<'_>) -> Option<Value> {
    if let Expression::StaticMemberExpression(member) = &call.callee {
        let property = member.property.name.as_str();
        if matches!(property, "optional" | "min" | "max" | "email") {
            let mut base = schema_definition(&member.object)?;
            if property == "optional" {
                base["optional"] = json!(true);
            } else if property == "email" {
                base["format"] = json!("email");
            } else if let Some(argument) = call.arguments.first() {
                if let Some(number) = numeric_argument_value(argument) {
                    base[property] = json!(number);
                } else {
                    return None;
                }
            } else {
                return None;
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
        _ => None,
    }
}

fn expression_mentions_schema(expression: &Expression<'_>) -> bool {
    match expression {
        Expression::CallExpression(call) => {
            static_member_name(&call.callee).is_some_and(|(object, _)| object == "schema")
                || match &call.callee {
                    Expression::StaticMemberExpression(member) => {
                        expression_mentions_schema(&member.object)
                    }
                    _ => false,
                }
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

fn sqlite_provider_call(expression: &Expression<'_>) -> Option<DatabaseCapability> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    sqlite_provider_call_expression(call)
}

fn sqlite_provider_call_expression(call: &CallExpression<'_>) -> Option<DatabaseCapability> {
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
        provider: "sqlite",
        config_name: Some(name.to_string()),
        access: "readwrite".to_string(),
        database: None,
        config_source: None,
        from_provider_use: true,
    })
}

fn app_use_provider_call(
    path: &Path,
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
    let Some(mut provider) = sqlite_provider_call_expression(provider_call) else {
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

fn normalize_sqlite_provider_token(name: &str) -> String {
    if name.contains('.') {
        name.to_string()
    } else {
        format!("data.{name}")
    }
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
    let mut providers = BTreeMap::<String, String>::new();
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
                    if let Some(token) = app_provider_call(init, app_name) {
                        providers.insert(name.to_string(), token);
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
                let Some((receiver, method, pattern, mut handler)) =
                    route_call(route_expr, source, source_name, true)
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
                if !route_pattern_supported(&full_pattern) {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_ROUTE_PATTERN",
                        "route pattern is outside the Plan v1 alpha route syntax",
                    )
                    .with_path(path)
                    .with_span(statement.span)
                    .with_hint("Use '/', static segments, {name}, {name:str}, or {name:int}."));
                }

                handler.source = wrap_module_handler_with_providers(
                    &handler.source,
                    &providers,
                    handler.is_async,
                );
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

fn app_provider_call(expression: &Expression<'_>, app_name: &str) -> Option<String> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if receiver != app_name || property != "provider" || call.arguments.len() != 1 {
        return None;
    }
    let token = string_argument(call.arguments.first()?)?;
    Some(normalize_sqlite_provider_token(
        token.strip_prefix("sqlite:").unwrap_or(token),
    ))
}

fn wrap_module_handler_with_providers(
    handler_source: &str,
    providers: &BTreeMap<String, String>,
    is_async: bool,
) -> String {
    if providers.is_empty() {
        return handler_source.to_string();
    }
    let provider_prefix = providers
        .iter()
        .map(|(name, token)| {
            format!(
                "const {name} = data.sqlite({});",
                serde_json::to_string(token).unwrap_or_else(|_| "\"main\"".to_string())
            )
        })
        .collect::<Vec<_>>()
        .join(" ");
    let close_calls = providers
        .keys()
        .map(|name| format!("{name}.close();"))
        .collect::<Vec<_>>()
        .join(" ");
    if is_async {
        return format!(
            "async function(ctx) {{ {provider_prefix} try {{ return await ({handler_source})(ctx); }} finally {{ {close_calls} }} }}"
        );
    }

    format!(
        "function(ctx) {{ {provider_prefix} try {{ return ({handler_source})(ctx); }} finally {{ {close_calls} }} }}"
    )
}

fn route_call<'a>(
    expression: &'a Expression<'a>,
    source: &str,
    source_name: &str,
    allow_data_handler_body: bool,
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
    let handler = handler_from_argument(handler_arg, source, source_name, allow_data_handler_body)?;
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
) -> Option<Handler> {
    match argument {
        Argument::ArrowFunctionExpression(function) => {
            if handler_parameters_are_unsupported(&function.params)
                || arrow_has_typescript_syntax(function)
                || (!allow_data_handler_body && !handler_body_is_supported_arrow(function))
            {
                return None;
            }
            let handler_source = source_slice(source, function.span)?;
            let schema_spans = handler_context_parameter_name(&function.params)
                .map(|ctx_name| body_json_schema_argument_spans_arrow(function, &ctx_name))
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
                bindings: request_bindings_from_arrow(function),
                response: response_metadata_from_arrow(function),
            })
        }
        Argument::FunctionExpression(function) => {
            if handler_parameters_are_unsupported(&function.params)
                || function_has_typescript_syntax(function)
                || (!allow_data_handler_body && !handler_body_is_supported_function(function))
            {
                return None;
            }
            let handler_source = source_slice(source, function.span)?;
            let schema_spans = handler_context_parameter_name(&function.params)
                .map(|ctx_name| body_json_schema_argument_spans_function(function, &ctx_name))
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
                bindings: request_bindings_from_function(function),
                response: response_metadata_from_function(function),
            })
        }
        _ => None,
    }
}

fn handler_diagnostic(path: &Path, argument: &Argument<'_>, fallback_span: Span) -> Diagnostic {
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
            } else if handler_result_uses_unsupported_values_arrow(function) {
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
            } else if handler_result_uses_unsupported_values_function(function) {
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
) -> bool {
    let roots = function_parameter_roots(&function.params);
    if function.expression {
        return function
            .body
            .statements
            .first()
            .and_then(expression_statement_result_call)
            .is_some_and(|call| !results_call_arguments_are_supported(call, &roots));
    }

    function
        .body
        .statements
        .first()
        .and_then(return_statement_result_call)
        .is_some_and(|call| !results_call_arguments_are_supported(call, &roots))
}

fn handler_result_uses_unsupported_values_function(function: &oxc_ast::ast::Function<'_>) -> bool {
    let roots = function_parameter_roots(&function.params);
    let Some(body) = &function.body else {
        return false;
    };
    body.statements
        .first()
        .and_then(return_statement_result_call)
        .is_some_and(|call| !results_call_arguments_are_supported(call, &roots))
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
        "status" => (status_result_code(call).unwrap_or(200), "json"),
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
) -> Vec<RequestBinding> {
    let Some(ctx_name) = handler_context_parameter_name(&function.params) else {
        return Vec::new();
    };
    let mut bindings = Vec::new();
    for statement in &function.body.statements {
        collect_statement_request_bindings(statement, &ctx_name, &mut bindings);
    }
    dedupe_request_bindings(bindings)
}

fn request_bindings_from_function(function: &oxc_ast::ast::Function<'_>) -> Vec<RequestBinding> {
    let Some(ctx_name) = handler_context_parameter_name(&function.params) else {
        return Vec::new();
    };
    let mut bindings = Vec::new();
    if let Some(body) = &function.body {
        for statement in &body.statements {
            collect_statement_request_bindings(statement, &ctx_name, &mut bindings);
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
    bindings: &mut Vec<RequestBinding>,
) {
    match statement {
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                collect_expression_request_bindings(argument, ctx_name, bindings);
            }
        }
        Statement::ExpressionStatement(statement) => {
            collect_expression_request_bindings(&statement.expression, ctx_name, bindings);
        }
        _ => {}
    }
}

fn collect_expression_request_bindings(
    expression: &Expression<'_>,
    ctx_name: &str,
    bindings: &mut Vec<RequestBinding>,
) {
    if let Some(binding) = request_binding_from_expression(expression, ctx_name) {
        bindings.push(binding);
    }
    match expression {
        Expression::CallExpression(call) => {
            if let Some(binding) = request_binding_from_call(call, ctx_name) {
                bindings.push(binding);
            }
            for argument in &call.arguments {
                collect_argument_request_bindings(argument, ctx_name, bindings);
            }
        }
        Expression::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_request_bindings(&property.value, ctx_name, bindings);
                }
            }
        }
        Expression::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_request_bindings(element, ctx_name, bindings);
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            collect_expression_request_bindings(&parenthesized.expression, ctx_name, bindings);
        }
        Expression::StaticMemberExpression(member) => {
            collect_expression_request_bindings(&member.object, ctx_name, bindings);
        }
        _ => {}
    }
}

fn collect_argument_request_bindings(
    argument: &Argument<'_>,
    ctx_name: &str,
    bindings: &mut Vec<RequestBinding>,
) {
    match argument {
        Argument::CallExpression(call) => {
            if let Some(binding) = request_binding_from_call(call, ctx_name) {
                bindings.push(binding);
            }
            for argument in &call.arguments {
                collect_argument_request_bindings(argument, ctx_name, bindings);
            }
        }
        Argument::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_request_bindings(&property.value, ctx_name, bindings);
                }
            }
        }
        Argument::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_request_bindings(element, ctx_name, bindings);
            }
        }
        Argument::StaticMemberExpression(member) => {
            collect_expression_request_bindings(&member.object, ctx_name, bindings);
        }
        _ => {}
    }
}

fn collect_array_element_request_bindings(
    element: &ArrayExpressionElement<'_>,
    ctx_name: &str,
    bindings: &mut Vec<RequestBinding>,
) {
    match element {
        ArrayExpressionElement::CallExpression(call) => {
            if let Some(binding) = request_binding_from_call(call, ctx_name) {
                bindings.push(binding);
            }
        }
        ArrayExpressionElement::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_request_bindings(&property.value, ctx_name, bindings);
                }
            }
        }
        ArrayExpressionElement::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_request_bindings(element, ctx_name, bindings);
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

fn request_binding_from_call(call: &CallExpression<'_>, ctx_name: &str) -> Option<RequestBinding> {
    let chain = static_member_chain(&call.callee)?;
    if chain.len() == 3 && chain[0] == ctx_name && chain[1] == "body" {
        let schema = body_binding_schema(call, chain[2])?;
        return Some(RequestBinding {
            kind: format!("body.{}", chain[2]),
            name: None,
            schema,
        });
    }
    None
}

fn body_binding_schema(call: &CallExpression<'_>, method: &str) -> Option<Option<String>> {
    match method {
        "text" if call.arguments.is_empty() => Some(None),
        "json" if call.arguments.len() <= 1 => Some(
            call.arguments
                .first()
                .and_then(argument_identifier)
                .map(str::to_string),
        ),
        _ => None,
    }
}

fn body_binding_call_is_supported(
    call: &CallExpression<'_>,
    allowed_roots: &BTreeSet<String>,
) -> bool {
    let Some(chain) = static_member_chain(&call.callee) else {
        return false;
    };
    chain.len() == 3
        && allowed_roots.contains(chain[0])
        && chain[1] == "body"
        && body_binding_schema(call, chain[2]).is_some()
}

fn body_json_schema_argument_spans_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    ctx_name: &str,
) -> Vec<Span> {
    let mut spans = Vec::new();
    for statement in &function.body.statements {
        collect_statement_schema_argument_spans(statement, ctx_name, &mut spans);
    }
    spans
}

fn body_json_schema_argument_spans_function(
    function: &oxc_ast::ast::Function<'_>,
    ctx_name: &str,
) -> Vec<Span> {
    let mut spans = Vec::new();
    if let Some(body) = &function.body {
        for statement in &body.statements {
            collect_statement_schema_argument_spans(statement, ctx_name, &mut spans);
        }
    }
    spans
}

fn collect_statement_schema_argument_spans(
    statement: &Statement<'_>,
    ctx_name: &str,
    spans: &mut Vec<Span>,
) {
    match statement {
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                collect_expression_schema_argument_spans(argument, ctx_name, spans);
            }
        }
        Statement::ExpressionStatement(statement) => {
            collect_expression_schema_argument_spans(&statement.expression, ctx_name, spans);
        }
        _ => {}
    }
}

fn collect_expression_schema_argument_spans(
    expression: &Expression<'_>,
    ctx_name: &str,
    spans: &mut Vec<Span>,
) {
    match expression {
        Expression::CallExpression(call) => {
            if let Some(span) = body_json_schema_argument_span(call, ctx_name) {
                spans.push(span);
            }
            for argument in &call.arguments {
                collect_argument_schema_argument_spans(argument, ctx_name, spans);
            }
        }
        Expression::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_schema_argument_spans(&property.value, ctx_name, spans);
                }
            }
        }
        Expression::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_schema_argument_spans(element, ctx_name, spans);
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            collect_expression_schema_argument_spans(&parenthesized.expression, ctx_name, spans);
        }
        _ => {}
    }
}

fn collect_argument_schema_argument_spans(
    argument: &Argument<'_>,
    ctx_name: &str,
    spans: &mut Vec<Span>,
) {
    match argument {
        Argument::CallExpression(call) => {
            if let Some(span) = body_json_schema_argument_span(call, ctx_name) {
                spans.push(span);
            }
            for argument in &call.arguments {
                collect_argument_schema_argument_spans(argument, ctx_name, spans);
            }
        }
        Argument::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_schema_argument_spans(&property.value, ctx_name, spans);
                }
            }
        }
        Argument::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_schema_argument_spans(element, ctx_name, spans);
            }
        }
        _ => {}
    }
}

fn collect_array_element_schema_argument_spans(
    element: &ArrayExpressionElement<'_>,
    ctx_name: &str,
    spans: &mut Vec<Span>,
) {
    match element {
        ArrayExpressionElement::CallExpression(call) => {
            if let Some(span) = body_json_schema_argument_span(call, ctx_name) {
                spans.push(span);
            }
        }
        ArrayExpressionElement::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_schema_argument_spans(&property.value, ctx_name, spans);
                }
            }
        }
        ArrayExpressionElement::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_schema_argument_spans(element, ctx_name, spans);
            }
        }
        _ => {}
    }
}

fn body_json_schema_argument_span(call: &CallExpression<'_>, ctx_name: &str) -> Option<Span> {
    let chain = static_member_chain(&call.callee)?;
    if chain.len() == 3 && chain[0] == ctx_name && chain[1] == "body" && chain[2] == "json" {
        let Argument::Identifier(identifier) = call.arguments.first()? else {
            return None;
        };
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

fn handler_body_is_supported_arrow(function: &oxc_ast::ast::ArrowFunctionExpression<'_>) -> bool {
    let roots = function_parameter_roots(&function.params);
    if function.expression {
        return function.body.statements.len() == 1
            && function.body.statements.first().is_some_and(|statement| {
                expression_statement_is_supported_result(statement, &roots)
            });
    }

    function.body.statements.len() == 1
        && function
            .body
            .statements
            .first()
            .is_some_and(|statement| return_statement_returns_supported_result(statement, &roots))
}

fn handler_body_is_supported_function(function: &oxc_ast::ast::Function<'_>) -> bool {
    let roots = function_parameter_roots(&function.params);
    if function.generator || function.body.is_none() {
        return false;
    }
    let Some(body) = &function.body else {
        return false;
    };
    body.statements.len() == 1
        && body
            .statements
            .first()
            .is_some_and(|statement| return_statement_returns_supported_result(statement, &roots))
}

fn return_statement_returns_supported_result(
    statement: &Statement<'_>,
    allowed_roots: &BTreeSet<String>,
) -> bool {
    return_statement_result_call(statement)
        .is_some_and(|call| results_call_arguments_are_supported(call, allowed_roots))
}

fn expression_statement_is_supported_result(
    statement: &Statement<'_>,
    allowed_roots: &BTreeSet<String>,
) -> bool {
    expression_statement_result_call(statement)
        .is_some_and(|call| results_call_arguments_are_supported(call, allowed_roots))
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
        && call
            .arguments
            .iter()
            .all(|argument| argument_is_inline_json_safe_value(argument, allowed_roots))
}

fn argument_is_inline_json_safe_value(
    argument: &Argument<'_>,
    allowed_roots: &BTreeSet<String>,
) -> bool {
    match argument {
        Argument::StringLiteral(_)
        | Argument::NumericLiteral(_)
        | Argument::BooleanLiteral(_)
        | Argument::NullLiteral(_) => true,
        Argument::ArrayExpression(array) => array
            .elements
            .iter()
            .all(|element| array_element_is_inline_json_safe_value(element, allowed_roots)),
        Argument::ObjectExpression(object) => {
            object.properties.iter().all(|property| match property {
                ObjectPropertyKind::ObjectProperty(property) => {
                    property.kind == PropertyKind::Init
                        && !property.method
                        && !property.shorthand
                        && !property.computed
                        && property_key_is_inline_json_safe(&property.key)
                        && expression_is_inline_json_safe_value(&property.value, allowed_roots)
                }
                ObjectPropertyKind::SpreadProperty(_) => false,
            })
        }
        Argument::StaticMemberExpression(member) => {
            static_member_root_name(&member.object).is_some_and(|root| allowed_roots.contains(root))
        }
        Argument::CallExpression(call) => body_binding_call_is_supported(call, allowed_roots),
        Argument::ParenthesizedExpression(parenthesized) => {
            expression_is_inline_json_safe_value(&parenthesized.expression, allowed_roots)
        }
        _ => false,
    }
}

fn array_element_is_inline_json_safe_value(
    element: &ArrayExpressionElement<'_>,
    allowed_roots: &BTreeSet<String>,
) -> bool {
    match element {
        ArrayExpressionElement::StringLiteral(_)
        | ArrayExpressionElement::NumericLiteral(_)
        | ArrayExpressionElement::BooleanLiteral(_)
        | ArrayExpressionElement::NullLiteral(_) => true,
        ArrayExpressionElement::ArrayExpression(array) => array
            .elements
            .iter()
            .all(|element| array_element_is_inline_json_safe_value(element, allowed_roots)),
        ArrayExpressionElement::ObjectExpression(object) => {
            object.properties.iter().all(|property| match property {
                ObjectPropertyKind::ObjectProperty(property) => {
                    property.kind == PropertyKind::Init
                        && !property.method
                        && !property.shorthand
                        && !property.computed
                        && property_key_is_inline_json_safe(&property.key)
                        && expression_is_inline_json_safe_value(&property.value, allowed_roots)
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
) -> bool {
    match expression {
        Expression::StringLiteral(_)
        | Expression::NumericLiteral(_)
        | Expression::BooleanLiteral(_)
        | Expression::NullLiteral(_) => true,
        Expression::ArrayExpression(array) => array
            .elements
            .iter()
            .all(|element| array_element_is_inline_json_safe_value(element, allowed_roots)),
        Expression::ObjectExpression(object) => {
            object.properties.iter().all(|property| match property {
                ObjectPropertyKind::ObjectProperty(property) => {
                    property.kind == PropertyKind::Init
                        && !property.method
                        && !property.shorthand
                        && !property.computed
                        && property_key_is_inline_json_safe(&property.key)
                        && expression_is_inline_json_safe_value(&property.value, allowed_roots)
                }
                ObjectPropertyKind::SpreadProperty(_) => false,
            })
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_is_inline_json_safe_value(&parenthesized.expression, allowed_roots)
        }
        Expression::CallExpression(call) => body_binding_call_is_supported(call, allowed_roots),
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
    let source_map = emit_source_map(app, &app_js.mappings);
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

fn emit_plan(
    app: &ExtractedApp,
    bundle_hash: &str,
    source_map_hash: &str,
) -> Result<String, Diagnostic> {
    let has_async_handlers = app.routes.iter().any(|route| route.handler.is_async);
    let emits_app_metadata = !app.schemas.is_empty() || !app.config_reads.is_empty();
    let emits_metadata = emits_app_metadata
        || app
            .routes
            .iter()
            .any(|route| !route.handler.bindings.is_empty());
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

    let routes = app
        .routes
        .iter()
        .enumerate()
        .map(|(index, route)| {
            let id = index + 1;
            let (line, column) = line_column(&route.source, route.span.start);
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
            let emits_route_metadata = emits_app_metadata || !route.handler.bindings.is_empty();
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
            route_json
        })
        .collect::<Vec<_>>();

    let data_providers = app
        .capabilities
        .iter()
        .map(|capability| {
            let mut provider = json!({
                "token": capability.token,
                "provider": capability.provider,
                "capability": capability.token,
                "service": null
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
                "kind": "database",
                "access": capability.access,
                "provider": capability.token
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
        "dataProviders": data_providers,
        "capabilities": capabilities,
        "features": {
            "asyncHandlers": has_async_handlers,
            "dataProviders": !app.capabilities.is_empty(),
            "capabilities": !app.capabilities.is_empty(),
            "sourceMaps": true
        }
    });
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

fn emit_app_js(app: &ExtractedApp) -> EmittedAppJs {
    let mut output = String::new();
    let mut mappings = Vec::new();
    let mut generated_line = 0usize;

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
    if app.uses_data_runtime {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "const { Results, data } = __sloppyRuntime;",
        );
    } else {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "const { Results } = __sloppyRuntime;",
        );
    }
    push_generated_line(&mut output, &mut generated_line, "");

    for (index, route) in app.routes.iter().enumerate() {
        let id = index + 1;
        let prefix = format!("globalThis.__sloppy_handler_{id} = ");
        let handler_start_line = generated_line;
        let handler_start_column = prefix.len();
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
    }
}

fn emit_source_map(app: &ExtractedApp, mappings: &[SourceMapMapping]) -> String {
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
    let value = json!({
        "version": 3,
        "file": "app.js",
        "sources": sources,
        "sourcesContent": sources_content,
        "names": [],
        "mappings": encode_source_map_mappings(mappings)
    });

    let json = serde_json::to_string_pretty(&value).unwrap_or_else(|_| "{}".to_string());
    format!("{json}\n")
}

fn push_generated_line(output: &mut String, generated_line: &mut usize, line: &str) {
    output.push_str(line);
    output.push('\n');
    *generated_line += 1;
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
            capabilities: vec![super::DatabaseCapability {
                token: "data.main".to_string(),
                provider: "sqlite",
                config_name: Some("main".to_string()),
                access: "readwrite".to_string(),
                database: None,
                config_source: None,
                from_provider_use: true,
            }],
            configuration: None,
            schemas: Vec::new(),
            config_reads: Vec::new(),
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
        let emitted_source_map = super::emit_source_map(&app, &emitted_js.mappings);
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
        let emitted_source_map = super::emit_source_map(&app, &emitted_js.mappings);
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
        let emitted_source_map = super::emit_source_map(&app, &emitted_js.mappings);
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
        let emitted_source_map = super::emit_source_map(&app, &emitted_js.mappings);
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
        let emitted_source_map = super::emit_source_map(&app, &emitted_js.mappings);
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
        let emitted_source_map = super::emit_source_map(&app, &emitted_js.mappings);
        let plan = super::emit_plan(
            &app,
            &super::sha256_hex(&emitted_js.source),
            &super::sha256_hex(&emitted_source_map),
        )
        .expect("plan should emit");
        assert!(plan.contains("\"database\": \":memory:\""));
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
        assert!(emitted_js.source.contains("data.sqlite(\"data.main\")"));

        let emitted_source_map = super::emit_source_map(&app, &emitted_js.mappings);
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
            "source-map",
        ] {
            let fixture = root
                .join("tests/fixtures")
                .join(fixture_name)
                .join("input.js");
            let source = fs::read_to_string(&fixture).expect("fixture input should exist");
            let app = extract(&fixture, &source).expect("fixture should extract");

            let emitted_js = super::emit_app_js(&app);
            let expected_js = fs::read_to_string(
                root.join("tests/fixtures")
                    .join(fixture_name)
                    .join("expected/app.js"),
            )
            .expect("expected app.js should exist");
            assert_eq!(emitted_js.source, expected_js, "{fixture_name} app.js");

            let emitted_source_map = super::emit_source_map(&app, &emitted_js.mappings);
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
