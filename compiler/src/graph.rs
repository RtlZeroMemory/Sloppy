//! Compiler-owned application graph model.
//!
//! These types are the internal IR between extraction and artifact emission. They
//! own normalized strings, source spans, and metadata copied out of the parser
//! lifetime; they are not part of the public compiler API or Plan JSON format.

use std::{
    collections::{BTreeMap, BTreeSet},
    path::PathBuf,
};

use oxc_span::Span;
use serde_json::Value;

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum ProjectKind {
    Web,
    Program,
}

impl ProjectKind {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Web => "web",
            Self::Program => "program",
        }
    }
}

#[derive(Debug, Clone)]
pub(crate) struct Route {
    pub(crate) method: &'static str,
    pub(crate) kind: &'static str,
    pub(crate) pattern: String,
    pub(crate) framework_path: Option<String>,
    pub(crate) name: Option<String>,
    pub(crate) tags: Vec<String>,
    pub(crate) health: Option<HealthRouteMetadata>,
    pub(crate) middleware: Vec<RouteMiddlewareMetadata>,
    pub(crate) auth: Option<AuthRequirementMetadata>,
    pub(crate) cors: Option<CorsPolicyMetadata>,
    pub(crate) cors_preflight: bool,
    pub(crate) span: Span,
    pub(crate) source_path: PathBuf,
    pub(crate) source_name: String,
    pub(crate) source: String,
    pub(crate) module: Option<String>,
    pub(crate) handler: Handler,
}

#[derive(Debug, Clone)]
pub(crate) struct DynamicRoute {
    pub(crate) method: Option<&'static str>,
    pub(crate) pattern: Option<String>,
    pub(crate) pattern_reason: &'static str,
    pub(crate) handler_known: bool,
    pub(crate) reason: &'static str,
    pub(crate) span: Span,
    pub(crate) source_name: String,
    pub(crate) source: String,
}

#[derive(Debug, Clone)]
pub(crate) struct Handler {
    pub(crate) source: String,
    pub(crate) emitted_source: String,
    pub(crate) span: Span,
    pub(crate) requires_results_import: bool,
    pub(crate) is_async: bool,
    pub(crate) runtime_deferred: bool,
    pub(crate) source_name: String,
    pub(crate) source_text: String,
    pub(crate) source_map_line_offset: usize,
    pub(crate) source_map_column_offset: usize,
    pub(crate) bindings: Vec<RequestBinding>,
    pub(crate) response: Option<ResponseMetadata>,
    pub(crate) responses: Vec<ResponseMetadata>,
    pub(crate) effects: Vec<EffectMetadata>,
    pub(crate) schema_metadata_conflict: bool,
}

#[derive(Debug, Clone)]
pub(crate) struct DatabaseCapability {
    pub(crate) token: String,
    pub(crate) capability_kind: String,
    pub(crate) provider: String,
    pub(crate) config_name: Option<String>,
    pub(crate) config_key: Option<String>,
    pub(crate) access: String,
    pub(crate) database: Option<String>,
    pub(crate) config_source: Option<String>,
    pub(crate) source_name: String,
    pub(crate) source: String,
    pub(crate) span: Span,
    pub(crate) from_provider_use: bool,
}

#[derive(Debug, Clone)]
pub(crate) struct ServiceRegistration {
    pub(crate) token: String,
    pub(crate) lifetime: &'static str,
    pub(crate) factory_source: String,
}

#[derive(Debug, Clone)]
pub(crate) struct SourceMapMapping {
    pub(crate) generated_line: usize,
    pub(crate) generated_column: usize,
    pub(crate) source_index: usize,
    pub(crate) original_line: usize,
    pub(crate) original_column: usize,
}

#[derive(Debug)]
pub(crate) struct HandlerGeneratedStart {
    pub(crate) generated_line: usize,
    pub(crate) generated_column: usize,
}

#[derive(Debug)]
pub(crate) struct EmittedAppJs {
    pub(crate) source: String,
    pub(crate) mappings: Vec<SourceMapMapping>,
    pub(crate) handler_generated_starts: Vec<HandlerGeneratedStart>,
}

#[derive(Debug)]
pub(crate) struct AppGraph {
    pub(crate) kind: ProjectKind,
    pub(crate) program_entry: Option<String>,
    pub(crate) program_modules: Vec<ProgramModule>,
    pub(crate) uses_data_runtime: bool,
    pub(crate) uses_sql_runtime: bool,
    pub(crate) uses_migrations_runtime: bool,
    pub(crate) uses_provider_health_runtime: bool,
    pub(crate) source_files: Vec<SourceFile>,
    pub(crate) routes: Vec<Route>,
    pub(crate) dynamic_routes: Vec<DynamicRoute>,
    pub(crate) dynamic_entry_source: Option<String>,
    pub(crate) service_registrations: Vec<ServiceRegistration>,
    pub(crate) modules: Vec<FunctionModule>,
    pub(crate) helper_sources: Vec<String>,
    pub(crate) capabilities: Vec<DatabaseCapability>,
    pub(crate) configuration: Option<ConfigurationPlan>,
    pub(crate) schemas: Vec<SchemaMetadata>,
    pub(crate) config_reads: Vec<ConfigReadMetadata>,
    pub(crate) uses_time_runtime: bool,
    pub(crate) uses_fs_runtime: bool,
    pub(crate) uses_crypto_runtime: bool,
    pub(crate) noncrypto_hash_security_context_visible: bool,
    pub(crate) uses_codec_runtime: bool,
    pub(crate) checksum_security_context_visible: bool,
    pub(crate) uses_net_runtime: bool,
    pub(crate) uses_os_runtime: bool,
    pub(crate) uses_http_client_runtime: bool,
    pub(crate) uses_realtime_runtime: bool,
    pub(crate) uses_workers_runtime: bool,
    pub(crate) uses_ffi_runtime: bool,
    pub(crate) ffi: Vec<FfiLibraryMetadata>,
    pub(crate) ffi_structs: Vec<FfiStructMetadata>,
    pub(crate) uses_health: bool,
    pub(crate) auth: AuthMetadata,
    pub(crate) problem_details: Option<ProblemDetailsDescriptor>,
    pub(crate) dependency_graph: DependencyGraph,
}

pub(crate) type ExtractedApp = AppGraph;

#[derive(Debug, Clone)]
pub(crate) struct ProgramModule {
    pub(crate) id: String,
    pub(crate) source_name: String,
    pub(crate) source: String,
    pub(crate) emitted_source: String,
    pub(crate) format: ModuleFormat,
}

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub(crate) enum ModuleFormat {
    Esm,
    CommonJs,
    Json,
}

impl ModuleFormat {
    pub(crate) fn as_str(self) -> &'static str {
        match self {
            Self::Esm => "esm",
            Self::CommonJs => "commonjs",
            Self::Json => "json",
        }
    }
}

#[derive(Debug, Clone, Default)]
pub(crate) struct DependencyGraph {
    pub(crate) resolver_profiles: Vec<String>,
    pub(crate) resolver_conditions: Vec<String>,
    pub(crate) packages: Vec<PackageRecord>,
    pub(crate) modules: Vec<DependencyModuleRecord>,
    pub(crate) assets: Vec<AssetRecord>,
    pub(crate) node_builtins: Vec<NodeBuiltinRecord>,
    pub(crate) compatibility_findings: Vec<CompatibilityFinding>,
}

impl DependencyGraph {
    pub(crate) fn has_entries(&self) -> bool {
        !self.packages.is_empty()
            || !self.modules.is_empty()
            || !self.assets.is_empty()
            || !self.node_builtins.is_empty()
            || !self.compatibility_findings.is_empty()
    }

    pub(crate) fn ensure_defaults(&mut self) {
        if self.resolver_profiles.is_empty() {
            self.resolver_profiles = vec![
                "sloppy-stdlib".to_string(),
                "relative-source".to_string(),
                "installed-packages".to_string(),
                "node-compat-shims".to_string(),
            ];
        }
        if self.resolver_conditions.is_empty() {
            self.resolver_conditions = vec![
                "sloppy".to_string(),
                "import".to_string(),
                "require".to_string(),
                "node".to_string(),
                "development".to_string(),
                "production".to_string(),
                "default".to_string(),
            ];
        }
    }
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub(crate) struct PackageRecord {
    pub(crate) name: String,
    pub(crate) version: Option<String>,
    pub(crate) root: String,
    pub(crate) package_json: Option<String>,
    pub(crate) entry: String,
    pub(crate) format: ModuleFormat,
    pub(crate) source: String,
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub(crate) struct DependencyModuleRecord {
    pub(crate) id: String,
    pub(crate) source: String,
    pub(crate) format: ModuleFormat,
    pub(crate) package: Option<String>,
    pub(crate) imports: Vec<String>,
    pub(crate) resolved_imports: Vec<ResolvedImportRecord>,
    pub(crate) dynamic_imports: Vec<DynamicImportRecord>,
    pub(crate) included_by: Option<String>,
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub(crate) struct ResolvedImportRecord {
    pub(crate) specifier: String,
    pub(crate) resolved_id: String,
    pub(crate) kind: String,
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub(crate) struct DynamicImportRecord {
    pub(crate) specifier: Option<String>,
    pub(crate) resolved_id: Option<String>,
    pub(crate) kind: String,
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub(crate) struct AssetRecord {
    pub(crate) path: String,
    pub(crate) included_by: String,
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub(crate) struct NodeBuiltinRecord {
    pub(crate) specifier: String,
    pub(crate) status: String,
    pub(crate) backing: Option<String>,
    pub(crate) capability: Option<String>,
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub(crate) struct CompatibilityFinding {
    pub(crate) code: String,
    pub(crate) severity: String,
    pub(crate) message: String,
    pub(crate) source: Option<String>,
    pub(crate) package: Option<String>,
    pub(crate) specifier: Option<String>,
    pub(crate) hint: Option<String>,
}

#[derive(Debug, Clone)]
pub(crate) struct ProblemDetailsDescriptor {
    pub(crate) detail: String,
}

#[derive(Debug, Clone, Default)]
pub(crate) struct AuthMetadata {
    pub(crate) schemes: Vec<AuthSchemeMetadata>,
    pub(crate) policies: Vec<AuthPolicyMetadata>,
}

#[derive(Debug, Clone)]
pub(crate) enum AuthSchemeMetadata {
    JwtBearer {
        name: String,
        issuer: Option<String>,
        audience: Option<String>,
        clock_skew_seconds: i64,
        secret_config_key: Option<String>,
    },
    ApiKey {
        name: String,
        header: String,
        config_key: Option<String>,
    },
    CookieSession {
        name: String,
        cookie: String,
        secure: bool,
        http_only: bool,
        same_site: String,
        path: String,
        max_age_seconds: Option<i64>,
        secret_config_key: Option<String>,
    },
}

#[derive(Debug, Clone)]
pub(crate) struct AuthPolicyMetadata {
    pub(crate) name: String,
    pub(crate) source: Option<String>,
}

#[derive(Debug, Clone, Default)]
pub(crate) struct AuthRequirementMetadata {
    pub(crate) required: bool,
    pub(crate) roles: Vec<String>,
    pub(crate) claims: Vec<String>,
    pub(crate) policy: Option<String>,
}

#[derive(Debug, Clone)]
pub(crate) struct HealthRouteMetadata {
    pub(crate) kind: &'static str,
    pub(crate) checks: Vec<String>,
}

#[derive(Debug, Clone)]
pub(crate) struct RouteMiddlewareMetadata {
    pub(crate) kind: String,
    pub(crate) source: String,
    pub(crate) sequence: usize,
    pub(crate) source_name: String,
    pub(crate) source_text: String,
    pub(crate) span: Span,
}

#[derive(Debug, Clone)]
pub(crate) struct CorsPolicyMetadata {
    pub(crate) origins: Vec<String>,
    pub(crate) methods: Vec<String>,
    pub(crate) headers: Vec<String>,
    pub(crate) exposed_headers: Vec<String>,
    pub(crate) credentials: bool,
    pub(crate) max_age_seconds: Option<u64>,
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
pub(crate) struct ResponseMetadata {
    pub(crate) helper: String,
    pub(crate) status: u16,
    pub(crate) kind: String,
    pub(crate) body_schema: Option<String>,
    pub(crate) native_body: Option<String>,
    pub(crate) source_name: Option<String>,
    pub(crate) source_text: Option<String>,
    pub(crate) span: Option<Span>,
    pub(crate) partial: bool,
}

#[derive(Debug, Clone)]
pub(crate) struct EffectMetadata {
    pub(crate) provider: String,
    pub(crate) capability_kind: String,
    pub(crate) provider_kind: String,
    pub(crate) access: &'static str,
    pub(crate) operation: String,
    pub(crate) reason: String,
    pub(crate) source_name: String,
    pub(crate) source_text: String,
    pub(crate) span: Span,
}

#[derive(Debug, Clone, Default)]
pub(crate) struct FunctionEffectSummary {
    pub(crate) effects: Vec<EffectMetadata>,
    pub(crate) provider_bindings: BTreeMap<String, ProviderBinding>,
    pub(crate) helper_calls: BTreeSet<String>,
    pub(crate) parameters: Vec<String>,
    pub(crate) unknown_provider_usage: bool,
    pub(crate) source_name: String,
    pub(crate) source_text: String,
}

#[derive(Debug, Clone)]
pub(crate) struct ProviderBinding {
    pub(crate) token: String,
    pub(crate) capability_kind: String,
    pub(crate) provider: String,
}

#[derive(Debug, Clone)]
pub(crate) struct SchemaMetadata {
    pub(crate) name: String,
    pub(crate) definition: Value,
    pub(crate) source_name: String,
    pub(crate) source: String,
    pub(crate) span: Span,
}

#[derive(Debug, Clone)]
pub(crate) struct ConfigReadMetadata {
    pub(crate) key: String,
    pub(crate) value_type: String,
    pub(crate) has_default: bool,
    pub(crate) default_value: Option<Value>,
    pub(crate) required: bool,
    pub(crate) sensitive: bool,
    pub(crate) source_name: String,
    pub(crate) source: String,
    pub(crate) span: Span,
}

#[derive(Debug, Clone)]
pub(crate) struct FfiLibraryMetadata {
    pub(crate) name: String,
    pub(crate) convention: String,
    pub(crate) functions: Vec<FfiFunctionMetadata>,
    pub(crate) source_name: String,
    pub(crate) source: String,
    pub(crate) span: Span,
}

#[derive(Debug, Clone)]
pub(crate) struct FfiFunctionMetadata {
    pub(crate) id: String,
    pub(crate) name: String,
    pub(crate) symbol: String,
    pub(crate) convention: String,
    pub(crate) return_type: String,
    pub(crate) parameters: Vec<String>,
    pub(crate) source_name: String,
    pub(crate) source: String,
    pub(crate) span: Span,
}

#[derive(Debug, Clone)]
pub(crate) struct FfiStructMetadata {
    pub(crate) name: String,
    pub(crate) layout: String,
    pub(crate) pack: Option<u32>,
    pub(crate) fields: Vec<FfiStructFieldMetadata>,
    pub(crate) source_name: String,
    pub(crate) source: String,
    pub(crate) span: Span,
}

#[derive(Debug, Clone)]
pub(crate) struct FfiStructFieldMetadata {
    pub(crate) name: String,
    pub(crate) type_name: String,
}

#[derive(Debug, Clone)]
pub(crate) struct ConfigurationPlan {
    pub(crate) environment: String,
    pub(crate) keys: Vec<ConfigurationPlanKey>,
    pub(crate) providers: Vec<ConfigurationProviderPlan>,
    pub(crate) requirements: Vec<ConfigurationRequirementPlan>,
    pub(crate) package_manifest: ConfigurationPackageManifest,
}

#[derive(Debug, Clone)]
pub(crate) struct ConfigurationPlanKey {
    pub(crate) key: String,
    pub(crate) source: String,
    pub(crate) value: Value,
    pub(crate) sensitive: bool,
}

#[derive(Debug, Clone)]
pub(crate) struct ConfigurationProviderPlan {
    pub(crate) provider: String,
    pub(crate) name: String,
    pub(crate) prefix: String,
    pub(crate) source: String,
}

#[derive(Debug, Clone)]
pub(crate) struct ConfigurationRequirementPlan {
    pub(crate) key: String,
    pub(crate) value_type: String,
    pub(crate) required: bool,
    pub(crate) sensitive: bool,
    pub(crate) status: String,
    pub(crate) source: Option<String>,
    pub(crate) required_by: String,
    pub(crate) default_value: Option<Value>,
}

#[derive(Debug, Clone, Default)]
pub(crate) struct ConfigurationPackageManifest {
    pub(crate) required: Vec<ConfigurationPackageEntry>,
    pub(crate) optional: Vec<ConfigurationPackageEntry>,
}

#[derive(Debug, Clone)]
pub(crate) struct ConfigurationPackageEntry {
    pub(crate) key: String,
    pub(crate) env: String,
    pub(crate) value_type: String,
    pub(crate) sensitive: bool,
    pub(crate) default_value: Option<Value>,
}

#[derive(Debug, Clone)]
pub(crate) struct SourceFile {
    pub(crate) name: String,
    pub(crate) source: String,
}

#[derive(Debug, Clone)]
pub(crate) struct ImportedModule {
    pub(crate) local_name: String,
    pub(crate) export_name: String,
    pub(crate) path: PathBuf,
    pub(crate) span: Span,
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub(crate) struct FunctionModule {
    pub(crate) name: String,
    pub(crate) source_name: String,
}

pub(crate) fn route_parameter_names(pattern: &str) -> BTreeSet<String> {
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

pub(crate) fn route_pattern_has_params(pattern: &str) -> bool {
    route_pattern_param_count(pattern) != 0
}

pub(crate) fn route_pattern_param_count(pattern: &str) -> usize {
    pattern
        .split('/')
        .skip(1)
        .filter(|segment| segment.starts_with('{') && segment.ends_with('}'))
        .count()
}

#[cfg(test)]
mod tests {
    use super::{route_pattern_has_params, route_pattern_param_count};

    #[test]
    fn route_pattern_params_require_complete_braced_segments() {
        assert!(!route_pattern_has_params("/users/{id"));
        assert_eq!(route_pattern_param_count("/users/{id"), 0);
        assert!(route_pattern_has_params("/orgs/{org}/users/{id:int}"));
        assert_eq!(route_pattern_param_count("/orgs/{org}/users/{id:int}"), 2);
    }
}
