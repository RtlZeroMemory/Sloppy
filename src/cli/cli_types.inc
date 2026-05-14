/*
 * src/cli/cli_types.inc
 *
 * Shared private CLI data structures.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
typedef enum SlCliFormat
{
    SL_CLI_FORMAT_TEXT,
    SL_CLI_FORMAT_JSON
} SlCliFormat;

typedef struct SlCliSpan
{
    const char* ptr;
    size_t length;
} SlCliSpan;

typedef struct SlCliRoute
{
    size_t consume_count;
    size_t produce_count;
    size_t header_count;
    yyjson_val* openapi_override_value;
    yyjson_val* output_cache_value;
    yyjson_val* cache_headers_value;
    size_t tag_count;
    size_t auth_scheme_count;
    size_t auth_scope_count;
    size_t auth_role_count;
    size_t auth_claim_count;
    size_t rate_limit_count;
    uint64_t source_line;
    uint64_t source_column;
    uint64_t response_status;
    size_t response_count;
    size_t binding_count;
    size_t effect_count;
    size_t source_order;
    SlCliSpan method;
    SlCliSpan kind;
    SlCliSpan pattern;
    SlCliSpan name;
    SlCliSpan module;
    SlCliSpan capability;
    SlCliSpan summary;
    SlCliSpan description;
    SlCliSpan deprecated_reason;
    SlCliSpan query_schema;
    SlCliSpan params_schema;
    SlCliSpan openapi_override;
    SlCliSpan output_cache;
    SlCliSpan cache_headers;
    SlCliSpan docs_kind;
    SlCliSpan auth_policy;
    SlCliSpan completeness;
    SlCliSpan source_path;
    SlCliSpan response_kind;
    SlCliSpan response_helper;
    SlCliSpan health_kind;
    SlCliSpan realtime_kind;
    SlCliSpan realtime_transport;
    SlCliSpan realtime_channel;
    SlCliSpan health_checks[SL_CLI_MAX_ROUTE_HEALTH_CHECKS];
    size_t health_check_count;
    SlCliSpan json_request_mode;
    SlCliSpan json_request_materialization;
    SlCliSpan json_request_schema;
    SlCliSpan json_request_fallback;
    SlCliSpan json_response_mode;
    SlCliSpan json_response_writer;
    SlCliSpan json_response_schema;
    SlCliSpan json_response_fallback;
    SlCliSpan dynamic_reason;
    SlCliSpan auth_schemes[SL_CLI_MAX_ROUTE_AUTH_SCHEMES];
    SlCliSpan auth_scopes[SL_CLI_MAX_ROUTE_AUTH_SCOPES];
    SlCliSpan auth_roles[SL_CLI_MAX_ROUTE_AUTH_ROLES];
    SlCliSpan auth_claims[SL_CLI_MAX_ROUTE_AUTH_CLAIMS];
    SlCliSpan rate_limit_names[SL_CLI_MAX_ROUTE_RATE_LIMITS];
    SlCliSpan rate_limit_algorithms[SL_CLI_MAX_ROUTE_RATE_LIMITS];
    SlCliSpan rate_limit_stores[SL_CLI_MAX_ROUTE_RATE_LIMITS];
    SlCliSpan rate_limit_partitions[SL_CLI_MAX_ROUTE_RATE_LIMITS];
    uint64_t response_statuses[SL_CLI_MAX_ROUTE_RESPONSES];
    SlCliSpan consumes[SL_CLI_MAX_ROUTE_TAGS];
    SlCliSpan produces[SL_CLI_MAX_ROUTE_TAGS];
    SlCliSpan header_names[SL_CLI_MAX_ROUTE_BINDINGS];
    SlCliSpan header_schemas[SL_CLI_MAX_ROUTE_BINDINGS];
    SlCliSpan header_descriptions[SL_CLI_MAX_ROUTE_BINDINGS];
    SlCliSpan tags[SL_CLI_MAX_ROUTE_TAGS];
    SlCliSpan response_kinds[SL_CLI_MAX_ROUTE_RESPONSES];
    SlCliSpan response_helpers[SL_CLI_MAX_ROUTE_RESPONSES];
    SlCliSpan response_body_schemas[SL_CLI_MAX_ROUTE_RESPONSES];
    SlCliSpan binding_kinds[SL_CLI_MAX_ROUTE_BINDINGS];
    SlCliSpan binding_names[SL_CLI_MAX_ROUTE_BINDINGS];
    SlCliSpan binding_schemas[SL_CLI_MAX_ROUTE_BINDINGS];
    SlCliSpan effect_providers[SL_CLI_MAX_ROUTE_EFFECTS];
    SlCliSpan effect_provider_kinds[SL_CLI_MAX_ROUTE_EFFECTS];
    SlCliSpan effect_capability_kinds[SL_CLI_MAX_ROUTE_EFFECTS];
    SlCliSpan effect_accesses[SL_CLI_MAX_ROUTE_EFFECTS];
    SlCliSpan effect_operations[SL_CLI_MAX_ROUTE_EFFECTS];
    SlCliSpan effect_reasons[SL_CLI_MAX_ROUTE_EFFECTS];
    SlHandlerId handler_id;
    bool deprecated;
    bool docs_internal;
    bool docs_strict;
    bool auth_required;
    bool auth_allow_anonymous;
    bool output_cache_vary_by_user;
    bool output_cache_allow_shared_authenticated;
    bool dynamic_metadata;
    bool rate_limit_partials[SL_CLI_MAX_ROUTE_RATE_LIMITS];
    bool header_required[SL_CLI_MAX_ROUTE_BINDINGS];
} SlCliRoute;

typedef struct SlCliHandler
{
    SlHandlerId id;
    SlCliSpan display_name;
} SlCliHandler;

typedef struct SlCliModule
{
    SlCliSpan name;
    SlCliSpan dependencies[SL_CLI_MAX_DEPS];
    size_t dependency_count;
} SlCliModule;

typedef struct SlCliProvider
{
    SlCliSpan token;
    SlCliSpan provider;
    SlCliSpan provider_kind;
    SlCliSpan capability;
    SlCliSpan required_access;
    SlCliSpan service;
    SlCliSpan database;
    SlCliSpan config_key;
} SlCliProvider;

typedef struct SlCliCapability
{
    SlCliSpan token;
    SlCliSpan kind;
    SlCliSpan access;
    SlCliSpan provider;
} SlCliCapability;

typedef struct SlCliDoctorCheck
{
    SlCliSpan id;
    SlCliSpan status;
    SlCliSpan message;
} SlCliDoctorCheck;

typedef struct SlCliHttpClient
{
    SlCliSpan name;
    SlCliSpan target;
    SlCliSpan base_url;
    bool strict_network;
} SlCliHttpClient;

typedef struct SlCliConfigRequirement
{
    SlCliSpan key;
    SlCliSpan type;
    SlCliSpan status;
    SlCliSpan source;
    SlCliSpan required_by;
    SlCliSpan redaction;
    bool required;
    bool secret;
} SlCliConfigRequirement;

typedef struct SlCliAuthScheme
{
    SlCliSpan kind;
    SlCliSpan name;
    SlCliSpan header;
    SlCliSpan cookie;
    SlCliSpan store;
} SlCliAuthScheme;

typedef struct SlCliFfiFunction
{
    SlCliSpan library;
    SlCliSpan id;
    SlCliSpan name;
    SlCliSpan symbol;
    SlCliSpan convention;
    SlCliSpan return_type;
    SlCliSpan parameters[SL_CLI_MAX_FFI_PARAMETERS];
    size_t parameter_count;
} SlCliFfiFunction;

typedef struct SlCliFfiLibrary
{
    SlCliSpan name;
    SlCliSpan convention;
    SlCliSpan package_path;
    SlCliSpan sha256;
    SlCliSpan hash_status;
    size_t first_function;
    size_t function_count;
} SlCliFfiLibrary;

typedef struct SlCliFfiStructField
{
    SlCliSpan name;
    SlCliSpan type;
    uint64_t offset;
    uint64_t size;
    uint64_t alignment;
} SlCliFfiStructField;

typedef struct SlCliFfiStruct
{
    SlCliSpan name;
    uint64_t size;
    uint64_t alignment;
    SlCliFfiStructField fields[SL_CLI_MAX_FFI_STRUCT_FIELDS];
    size_t field_count;
} SlCliFfiStruct;

typedef struct SlCliFfiHandle
{
    SlCliSpan name;
} SlCliFfiHandle;

typedef struct SlCliFfiCallback
{
    SlCliSpan id;
    SlCliSpan return_type;
    SlCliSpan thread;
    SlCliSpan parameters[SL_CLI_MAX_FFI_PARAMETERS];
    size_t parameter_count;
} SlCliFfiCallback;

typedef struct SlCliFfiDispatchTable
{
    SlCliSpan name;
    SlCliSpan resolver;
    SlCliSpan symbols[SL_CLI_MAX_FFI_DISPATCH_SYMBOLS];
    size_t symbol_count;
} SlCliFfiDispatchTable;

typedef struct SlCliSchemaProperty
{
    SlCliSpan name;
    SlCliSpan kind;
    SlCliSpan format;
    SlCliSpan item_kind;
    bool optional;
    bool nullable;
    bool has_min;
    bool has_max;
    double min_value;
    double max_value;
    yyjson_val* literal_value;
} SlCliSchemaProperty;

typedef struct SlCliSchema
{
    SlCliSpan name;
    SlCliSpan kind;
    SlCliSpan item_kind;
    SlCliSpan source_path;
    uint64_t source_line;
    uint64_t source_column;
    bool nullable;
    bool has_min;
    bool has_max;
    double min_value;
    double max_value;
    yyjson_val* literal_value;
    SlCliSchemaProperty properties[SL_CLI_MAX_SCHEMA_PROPERTIES];
    size_t property_count;
} SlCliSchema;

typedef struct SlCliRouteDispatch
{
    bool present;
    SlCliSpan mode;
    SlCliSpan artifact_kind;
    SlCliSpan artifact_path;
    SlCliSpan artifact_hash;
    SlCliSpan artifact_reason;
    uint64_t route_count;
    uint64_t endpoint_count;
    uint64_t static_routes;
    uint64_t parameter_routes;
    uint64_t catch_all_routes;
    uint64_t native_no_js_endpoints;
    uint64_t url_writers;
    uint64_t exact_static_paths;
    uint64_t parameter_candidate_buckets;
    uint64_t segment_trie_nodes;
    uint64_t dynamic_fallback_routes;
    uint64_t partial_fallback_routes;
    uint64_t json_request_native_routes;
    uint64_t json_request_generic_routes;
    uint64_t json_request_fallback_routes;
    uint64_t json_request_materialized_routes;
    uint64_t json_response_native_routes;
    uint64_t json_response_generic_routes;
    uint64_t json_response_fallback_routes;
    bool url_generation;
    bool classic_fallback_available;
} SlCliRouteDispatch;

typedef struct SlCliMetadata
{
    SlCliRoute routes[SL_CLI_MAX_ROUTES];
    size_t route_count;
    SlCliHandler handlers[SL_CLI_MAX_HANDLERS];
    size_t handler_count;
    SlCliModule modules[SL_CLI_MAX_MODULES];
    size_t module_count;
    SlCliProvider providers[SL_CLI_MAX_PROVIDERS];
    size_t provider_count;
    SlCliCapability capabilities[SL_CLI_MAX_CAPABILITIES];
    size_t capability_count;
    SlCliSchema schemas[SL_CLI_MAX_SCHEMAS];
    size_t schema_count;
    SlCliDoctorCheck doctor_checks[SL_CLI_MAX_DOCTOR_CHECKS];
    size_t doctor_check_count;
    SlCliSpan required_features[SL_CLI_MAX_REQUIRED_FEATURES];
    size_t required_feature_count;
    SlCliHttpClient http_clients[SL_CLI_MAX_HTTP_CLIENTS];
    size_t http_client_count;
    SlCliConfigRequirement config_requirements[SL_CLI_MAX_CONFIG_REQUIREMENTS];
    size_t config_requirement_count;
    SlCliAuthScheme auth_schemes[SL_CLI_MAX_AUTH_SCHEMES];
    size_t auth_scheme_count;
    SlCliSpan auth_policies[SL_CLI_MAX_AUTH_POLICIES];
    size_t auth_policy_count;
    SlCliFfiLibrary ffi_libraries[SL_CLI_MAX_FFI_LIBRARIES];
    size_t ffi_library_count;
    SlCliFfiFunction ffi_functions[SL_CLI_MAX_FFI_FUNCTIONS];
    size_t ffi_function_count;
    SlCliFfiStruct ffi_structs[SL_CLI_MAX_FFI_STRUCTS];
    size_t ffi_struct_count;
    SlCliFfiHandle ffi_handles[SL_CLI_MAX_FFI_HANDLES];
    size_t ffi_handle_count;
    SlCliFfiCallback ffi_callbacks[SL_CLI_MAX_FFI_CALLBACKS];
    size_t ffi_callback_count;
    SlCliFfiDispatchTable ffi_dispatch_tables[SL_CLI_MAX_FFI_DISPATCH_TABLES];
    size_t ffi_dispatch_table_count;
    SlCliRouteDispatch route_dispatch;
    SlCliSpan kind;
    SlCliSpan completeness;
} SlCliMetadata;

typedef struct SlCliOptions
{
    const char* command;
    char executable_path[SL_RUN_PATH_MAX_BYTES];
    char plan_path_storage[SL_RUN_PATH_MAX_BYTES];
    const char* plan_path;
    const char* output_path;
    const char* input_path;
    const char* out_dir;
    const char* create_name;
    const char* create_template;
    const char* artifacts_path;
    const char* stdlib_path;
    const char* host;
    const char* environment;
    const char* kind;
    const char* once_headers[SL_HTTP_DEFAULT_MAX_HEADERS];
    size_t once_header_count;
    const char* once_method;
    const char* once_target;
    const char* once_body;
    const char* once_body_file;
    const char* once_json;
    const char* db_action;
    const char* db_provider;
    const char* db_target_path;
    char** program_args;
    size_t program_arg_count;
    uint16_t port;
    SlCliFormat format;
    bool host_explicit;
    bool port_explicit;
    bool environment_explicit;
    bool kind_explicit;
    bool out_explicit;
    bool create_force;
    bool create_no_git;
    bool dev_watch;
    bool dev_watch_explicit;
    bool dev_clear;
    bool dev_openapi;
    bool program_args_explicit;
    bool deps_explain;
    bool dispatch_details;
    bool diagnostics_json;
    bool doctor_report;
    bool openapi_strict;
    bool openapi_summary;
    bool openapi_fail_on_partial;
    bool openapi_pretty;
    bool restore_locked;
    bool help;
} SlCliOptions;
