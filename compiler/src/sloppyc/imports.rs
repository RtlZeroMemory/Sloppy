// Sloppy stdlib and package import validation/runtime feature marking.
use super::*;

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

pub(super) fn noncrypto_hash_security_context_visible(source: &str) -> bool {
    source_contains_noncrypto_xxhash64_member(source) && source_has_security_identifier(source)
}

fn source_contains_noncrypto_xxhash64_member(source: &str) -> bool {
    source_contains_ascii_member(source, "NonCryptoHash", "xxHash64")
}

pub(super) fn checksum_security_context_visible(source: &str) -> bool {
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

fn sloppy_webhooks_import_name_supported(name: &str) -> bool {
    matches!(name, "Webhooks" | "SloppyWebhookError")
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

fn sloppy_redis_import_name_supported(name: &str) -> bool {
    matches!(name, "Redis" | "SloppyRedisError")
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
    Webhooks,
    Redis,
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
            "sloppy/webhooks" => Some(Self::Webhooks),
            "sloppy/redis" => Some(Self::Redis),
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
            Self::Webhooks => sloppy_webhooks_import_name_supported(name),
            Self::Redis => sloppy_redis_import_name_supported(name),
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

pub(super) fn validate_module_sloppy_root_import(
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

pub(super) fn mark_sloppy_root_runtime_usage(
    graph: &mut ModuleGraph,
    import: &ImportDeclaration<'_>,
) {
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
            "Redis" | "SloppyRedisError" => {
                graph.uses_redis_runtime = true;
                graph.uses_net_runtime = true;
            }
            "Http" | "HttpClientFactory" | "HttpError" | "SloppyHttpClientError" | "TestHttp" => {
                graph.uses_http_client_runtime = true;
            }
            "Webhooks" | "SloppyWebhookError" => {
                graph.uses_data_runtime = true;
                graph.uses_crypto_runtime = true;
                graph.uses_http_client_runtime = true;
                graph.uses_workers_runtime = true;
                graph.uses_webhooks_runtime = true;
            }
            _ => {}
        }
    }
}

pub(super) fn missing_results_import_diagnostic(path: &Path, span: Span) -> Diagnostic {
    Diagnostic::new(
        "SLOPPYC_E_UNSUPPORTED_IMPORT",
        "route handlers that call Results must import Results from \"sloppy\" in the same source file",
    )
    .with_path(path)
    .with_span(span)
    .with_hint("Add `import { Results } from \"sloppy\";` to the file that contains the handler.")
}

pub(super) fn validate_module_sloppy_time_import(
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

pub(super) fn validate_module_sloppy_crypto_import(
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

pub(super) fn validate_module_sloppy_codec_import(
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

pub(super) fn validate_module_sloppy_cache_import(
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

pub(super) fn validate_module_sloppy_fs_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(path, import, "sloppy/fs", sloppy_fs_import_name_supported)
}

pub(super) fn validate_module_sloppy_data_import(
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

pub(super) fn validate_module_sloppy_orm_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(path, import, "sloppy/orm", sloppy_orm_import_name_supported)
}

pub(super) fn validate_module_sloppy_sqlite_provider_import(
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

pub(super) fn validate_module_sloppy_net_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(path, import, "sloppy/net", sloppy_net_import_name_supported)
}

pub(super) fn validate_module_sloppy_redis_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(
        path,
        import,
        "sloppy/redis",
        sloppy_redis_import_name_supported,
    )
}

pub(super) fn validate_module_sloppy_http_import(
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

pub(super) fn validate_module_sloppy_webhooks_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(
        path,
        import,
        "sloppy/webhooks",
        sloppy_webhooks_import_name_supported,
    )
}

pub(super) fn validate_module_sloppy_os_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
) -> Result<(), Diagnostic> {
    validate_module_sloppy_import(path, import, "sloppy/os", sloppy_os_import_name_supported)
}

pub(super) fn validate_module_sloppy_workers_import(
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

pub(super) fn validate_module_sloppy_ffi_import(
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

pub(super) fn import_specifier_is_runtime_value(
    import: &ImportDeclaration<'_>,
    specifier: &oxc_ast::ast::ImportSpecifier<'_>,
) -> bool {
    import.import_kind != ImportOrExportKind::Type
        && specifier.import_kind != ImportOrExportKind::Type
}

pub(super) fn import_has_runtime_value_specifier(import: &ImportDeclaration<'_>) -> bool {
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
        SloppyStdlibImport::Webhooks => {
            state.data_imported = true;
            state.crypto_imported = true;
            state.http_client_imported = true;
            state.workers_imported = true;
            state.webhooks_imported = true;
        }
        SloppyStdlibImport::Redis => {
            state.redis_imported = true;
            state.net_imported = true;
        }
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

pub(super) fn mark_sloppy_net_runtime_usage(
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

pub(super) fn mark_sloppy_data_runtime_usage(
    graph: &mut ModuleGraph,
    import: &ImportDeclaration<'_>,
) {
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

pub(super) fn extract_import(
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
                ("Webhooks", "Webhooks") | ("SloppyWebhookError", "SloppyWebhookError") => {
                    if import_specifier_is_runtime_value(import, specifier) {
                        state.data_imported = true;
                        state.crypto_imported = true;
                        state.http_client_imported = true;
                        state.workers_imported = true;
                        state.webhooks_imported = true;
                    }
                }
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

pub(super) fn extract_package_helper_imports(
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

pub(super) fn sloppy_root_import_name_supported(name: &str) -> bool {
    matches!(
        name,
        "Sloppy"
            | "Results"
            | "Auth"
            | "Cache"
            | "SloppyCacheError"
            | "RateLimit"
            | "Realtime"
            | "SloppyRealtimeError"
            | "Redis"
            | "SloppyRedisError"
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
            | "Webhooks"
            | "SloppyWebhookError"
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
