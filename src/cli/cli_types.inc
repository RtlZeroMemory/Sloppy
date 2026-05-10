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
    SlCliSpan method;
    SlCliSpan pattern;
    SlHandlerId handler_id;
    SlCliSpan name;
    SlCliSpan module;
    SlCliSpan capability;
    SlCliSpan tags[SL_CLI_MAX_ROUTE_TAGS];
    size_t tag_count;
    SlCliSpan completeness;
    SlCliSpan source_path;
    uint64_t source_line;
    uint64_t source_column;
    SlCliSpan response_kind;
    SlCliSpan response_helper;
    uint64_t response_status;
    SlCliSpan response_kinds[SL_CLI_MAX_ROUTE_RESPONSES];
    SlCliSpan response_helpers[SL_CLI_MAX_ROUTE_RESPONSES];
    uint64_t response_statuses[SL_CLI_MAX_ROUTE_RESPONSES];
    size_t response_count;
    SlCliSpan binding_kinds[SL_CLI_MAX_ROUTE_BINDINGS];
    SlCliSpan binding_names[SL_CLI_MAX_ROUTE_BINDINGS];
    SlCliSpan binding_schemas[SL_CLI_MAX_ROUTE_BINDINGS];
    size_t binding_count;
    SlCliSpan effect_providers[SL_CLI_MAX_ROUTE_EFFECTS];
    SlCliSpan effect_provider_kinds[SL_CLI_MAX_ROUTE_EFFECTS];
    SlCliSpan effect_capability_kinds[SL_CLI_MAX_ROUTE_EFFECTS];
    SlCliSpan effect_accesses[SL_CLI_MAX_ROUTE_EFFECTS];
    SlCliSpan effect_operations[SL_CLI_MAX_ROUTE_EFFECTS];
    SlCliSpan effect_reasons[SL_CLI_MAX_ROUTE_EFFECTS];
    size_t effect_count;
    size_t source_order;
    bool dynamic_metadata;
    SlCliSpan dynamic_reason;
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
    size_t first_function;
    size_t function_count;
} SlCliFfiLibrary;

typedef struct SlCliFfiStructField
{
    SlCliSpan name;
    SlCliSpan type;
} SlCliFfiStructField;

typedef struct SlCliFfiStruct
{
    SlCliSpan name;
    SlCliFfiStructField fields[SL_CLI_MAX_FFI_STRUCT_FIELDS];
    size_t field_count;
} SlCliFfiStruct;

typedef struct SlCliSchemaProperty
{
    SlCliSpan name;
    SlCliSpan kind;
    SlCliSpan format;
    SlCliSpan item_kind;
    bool optional;
} SlCliSchemaProperty;

typedef struct SlCliSchema
{
    SlCliSpan name;
    SlCliSpan kind;
    SlCliSpan item_kind;
    SlCliSpan source_path;
    uint64_t source_line;
    uint64_t source_column;
    SlCliSchemaProperty properties[SL_CLI_MAX_SCHEMA_PROPERTIES];
    size_t property_count;
} SlCliSchema;

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
    SlCliFfiLibrary ffi_libraries[SL_CLI_MAX_FFI_LIBRARIES];
    size_t ffi_library_count;
    SlCliFfiFunction ffi_functions[SL_CLI_MAX_FFI_FUNCTIONS];
    size_t ffi_function_count;
    SlCliFfiStruct ffi_structs[SL_CLI_MAX_FFI_STRUCTS];
    size_t ffi_struct_count;
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
    const char* once_method;
    const char* once_target;
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
    bool program_args_explicit;
    bool help;
} SlCliOptions;
