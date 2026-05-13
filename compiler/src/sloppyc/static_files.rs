// Static file and SPA route extraction.
use super::*;

#[derive(Debug, Clone)]
struct StaticFilesOptions {
    kind: StaticFilesKind,
    request_path: String,
    root: String,
    fallback: Option<String>,
    max_age_seconds: Option<u64>,
    cache_control: Option<String>,
    html_cache_control: Option<String>,
    assets_cache_control: Option<String>,
    precompressed: Vec<StaticEncoding>,
    index: Option<String>,
    max_file_bytes: u64,
}

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
enum StaticFilesKind {
    Static,
    Spa,
}

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
enum StaticEncoding {
    Brotli,
    Gzip,
}

struct StaticFilesRouteContext<'a> {
    path: &'a Path,
    source: &'a str,
    source_name: &'a str,
    span: Span,
    middleware: &'a [FrameworkMiddleware],
    cors: Option<&'a CorsPolicy>,
}

pub(super) fn app_use_static_files_call(
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
    if (property != "useStaticFiles" && property != "staticFiles" && property != "spa")
        || !state.app_vars.contains(receiver)
    {
        return Ok(false);
    }
    if (property == "useStaticFiles" && call.arguments.len() != 1)
        || (property != "useStaticFiles" && call.arguments.len() != 2)
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
            if property == "useStaticFiles" {
                "app.useStaticFiles requires one literal options object"
            } else {
                "app.staticFiles and app.spa require a literal mount path and options object"
            },
        )
        .with_path(path)
        .with_span(call.span));
    }
    let options = static_files_options_from_call(path, call, property)?;
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
    method_name: &str,
) -> Result<StaticFilesOptions, Diagnostic> {
    let mount_argument = if method_name == "useStaticFiles" {
        None
    } else {
        call.arguments.first()
    };
    let options_argument = if method_name == "useStaticFiles" {
        call.arguments.first()
    } else {
        call.arguments.get(1)
    };
    let mount = if let Some(argument) = mount_argument {
        let Some(Expression::StringLiteral(value)) = argument.as_expression() else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
                "app.staticFiles and app.spa mount path must be a string literal",
            )
            .with_path(path)
            .with_span(argument_span(argument).unwrap_or(call.span)));
        };
        Some(value.value.as_str().to_string())
    } else {
        None
    };
    let Some(argument) = options_argument else {
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
    let mut fallback = None;
    let mut max_age_seconds = None;
    let mut cache_control = None;
    let mut html_cache_control = None;
    let mut assets_cache_control = None;
    let mut precompressed = Vec::new();
    let mut index = if method_name == "spa" {
        None
    } else {
        Some("index.html".to_string())
    };
    let mut max_file_bytes = STATIC_ASSET_INLINE_MAX_BYTES;
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
            "fallback" => {
                let Expression::StringLiteral(value) = &property.value else {
                    return Err(static_files_options_diagnostic(path, property.value.span()));
                };
                fallback = Some(value.value.as_str().to_string());
            }
            "index" => match &property.value {
                Expression::StringLiteral(value) => index = Some(value.value.as_str().to_string()),
                Expression::BooleanLiteral(value) if !value.value => index = None,
                _ => return Err(static_files_options_diagnostic(path, property.value.span())),
            },
            "cache" => {
                max_age_seconds = static_files_cache_max_age(path, &property.value)?;
            }
            "cacheControl" => match &property.value {
                Expression::StringLiteral(value) => {
                    cache_control = Some(value.value.as_str().to_string());
                }
                Expression::ObjectExpression(value) if method_name == "spa" => {
                    for cache_property in &value.properties {
                        let ObjectPropertyKind::ObjectProperty(cache_property) = cache_property
                        else {
                            return Err(static_files_options_diagnostic(path, value.span));
                        };
                        let Some(cache_name) = property_key_name(&cache_property.key) else {
                            return Err(static_files_options_diagnostic(path, cache_property.span));
                        };
                        let Expression::StringLiteral(cache_value) = &cache_property.value else {
                            return Err(static_files_options_diagnostic(
                                path,
                                cache_property.value.span(),
                            ));
                        };
                        match cache_name {
                            "html" => {
                                html_cache_control = Some(cache_value.value.as_str().to_string())
                            }
                            "assets" => {
                                assets_cache_control = Some(cache_value.value.as_str().to_string());
                            }
                            _ => {
                                return Err(Diagnostic::new(
                                    "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
                                    format!(
                                        "unsupported app.spa cacheControl option '{cache_name}'"
                                    ),
                                )
                                .with_path(path)
                                .with_span(cache_property.span));
                            }
                        }
                    }
                }
                _ => return Err(static_files_options_diagnostic(path, property.value.span())),
            },
            "precompressed" => {
                precompressed = static_files_precompressed(path, &property.value)?;
            }
            "maxFileBytes" => {
                let Expression::NumericLiteral(value) = &property.value else {
                    return Err(static_files_options_diagnostic(path, property.value.span()));
                };
                if value.value <= 0.0
                    || value.value.fract() != 0.0
                    || value.value > STATIC_ASSET_INLINE_MAX_BYTES as f64
                {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
                        format!(
                            "app.{method_name} maxFileBytes must be an integer from 1 to {STATIC_ASSET_INLINE_MAX_BYTES}"
                        ),
                    )
                    .with_path(path)
                    .with_span(property.span));
                }
                max_file_bytes = value.value as u64;
            }
            "dotfiles" | "etag" | "lastModified" | "range" | "fallthrough"
            | "allowedExtensions" | "deniedExtensions" | "contentType" | "assetsPrefix" => {}
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
                    format!("unsupported app.{method_name} option '{name}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }

    let request_path = mount.or(request_path).ok_or_else(|| {
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
            format!("app.{method_name} mount path must be an absolute static route prefix"),
        )
        .with_path(path)
        .with_span(object.span)
        .with_hint("Use a value like \"/public\" without route parameters or a trailing slash."));
    }
    if !static_files_root_supported(&root) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
            format!("app.{method_name} root must be a safe project-relative directory"),
        )
        .with_path(path)
        .with_span(object.span)
        .with_hint(
            "Use a relative directory like \"public\"; traversal and absolute paths are rejected.",
        ));
    }
    if fallback
        .as_deref()
        .is_some_and(|value| !static_files_relative_path_supported(value))
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
            format!("app.{method_name} fallback must be a safe root-relative file path"),
        )
        .with_path(path)
        .with_span(object.span));
    }
    if index
        .as_deref()
        .is_some_and(|value| !static_files_relative_path_supported(value))
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
            format!("app.{method_name} index must be a safe root-relative file path"),
        )
        .with_path(path)
        .with_span(object.span));
    }

    Ok(StaticFilesOptions {
        kind: if method_name == "spa" {
            StaticFilesKind::Spa
        } else {
            StaticFilesKind::Static
        },
        request_path,
        root,
        fallback,
        max_age_seconds,
        cache_control,
        html_cache_control,
        assets_cache_control,
        precompressed,
        index,
        max_file_bytes,
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

fn static_files_precompressed(
    path: &Path,
    expression: &Expression<'_>,
) -> Result<Vec<StaticEncoding>, Diagnostic> {
    match expression {
        Expression::BooleanLiteral(value) => {
            if value.value {
                Ok(vec![StaticEncoding::Brotli, StaticEncoding::Gzip])
            } else {
                Ok(Vec::new())
            }
        }
        Expression::ArrayExpression(array) => {
            let mut encodings = Vec::new();
            for element in &array.elements {
                let Some(Expression::StringLiteral(value)) = element.as_expression() else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
                        "app.staticFiles precompressed entries must be string literals",
                    )
                    .with_path(path)
                    .with_span(oxc_span::GetSpan::span(element)));
                };
                let encoding = match value.value.as_str() {
                    "br" => StaticEncoding::Brotli,
                    "gzip" => StaticEncoding::Gzip,
                    _ => {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
                            "app.staticFiles precompressed entries must be 'br' or 'gzip'",
                        )
                        .with_path(path)
                        .with_span(value.span));
                    }
                };
                if !encodings.contains(&encoding) {
                    encodings.push(encoding);
                }
            }
            Ok(encodings)
        }
        _ => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_STATIC_FILES",
            "app.staticFiles precompressed must be a boolean or literal array",
        )
        .with_path(path)
        .with_span(expression.span())),
    }
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

fn static_files_relative_path_supported(path: &str) -> bool {
    !path.is_empty()
        && include_pattern_is_safe(path)
        && !path.contains('*')
        && !path.contains('?')
        && !path.contains('\\')
        && !path.starts_with('/')
        && !path.starts_with("//")
        && !path
            .split('/')
            .any(|segment| segment.is_empty() || segment == "." || segment == "..")
}

pub(super) const STATIC_ASSET_INLINE_MAX_BYTES: u64 = 1024 * 1024;

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
        if metadata.len() > options.max_file_bytes {
            return Err(Diagnostic::new(
                "SLOPPYC_E_STATIC_FILES",
                format!(
                    "app static file asset exceeds the configured inline limit of {} bytes",
                    options.max_file_bytes
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
            format!("app.staticFiles:{}:{}", options.request_path, options.root),
        );
        let variants = static_asset_variants(context, graph, options, &file)?;
        let mut route_paths = vec![route_path];
        if options.kind == StaticFilesKind::Static {
            if let Some(index_route) = static_asset_index_route_path(
                &options.request_path,
                relative,
                options.index.as_deref(),
            ) {
                if !route_paths.iter().any(|path| path == &index_route) {
                    route_paths.push(index_route);
                }
            }
        }
        for route_path in route_paths {
            routes.push(static_asset_route(
                context,
                route_path,
                content_type,
                &bytes,
                options,
                &variants,
                false,
            ));
        }
    }
    if options.kind == StaticFilesKind::Spa {
        let fallback = options.fallback.as_deref().unwrap_or("index.html");
        let fallback_path = canonical_root.join(fallback);
        let bytes = fs::read(&fallback_path).map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_STATIC_FILES",
                format!("failed to read app.spa fallback: {error}"),
            )
            .with_path(&fallback_path)
            .with_span(context.span)
        })?;
        if bytes.len() as u64 > options.max_file_bytes {
            return Err(Diagnostic::new(
                "SLOPPYC_E_STATIC_FILES",
                format!(
                    "app.spa fallback exceeds the configured inline limit of {} bytes",
                    options.max_file_bytes
                ),
            )
            .with_path(&fallback_path)
            .with_span(context.span));
        }
        let content_type =
            static_asset_content_type(&fallback_path).unwrap_or("text/html; charset=utf-8");
        let variants = static_asset_variants(context, graph, options, &fallback_path)?;
        for pattern in static_spa_fallback_patterns(&options.request_path) {
            routes.push(static_asset_route(
                context,
                pattern,
                content_type,
                &bytes,
                options,
                &variants,
                true,
            ));
        }
    }
    Ok(routes)
}

fn static_spa_fallback_patterns(mount: &str) -> Vec<String> {
    let mut routes = Vec::new();
    if mount == "/" {
        routes.push("/".to_string());
    } else {
        routes.push(mount.to_string());
    }
    let prefix = if mount == "/" {
        String::new()
    } else {
        mount.to_string()
    };
    let mut path = prefix;
    for index in 0..32 {
        path.push_str(&format!("/{{sloppySpa{index}:str}}"));
        routes.push(path.clone());
    }
    routes
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

struct StaticAssetVariant {
    content_encoding: &'static str,
    bytes: Vec<u8>,
    content_hash: String,
}

fn static_asset_variants(
    context: &StaticFilesRouteContext<'_>,
    graph: &mut ModuleGraph,
    options: &StaticFilesOptions,
    file: &Path,
) -> Result<Vec<StaticAssetVariant>, Diagnostic> {
    let mut variants = Vec::new();
    for encoding in &options.precompressed {
        let (extension, content_encoding) = match encoding {
            StaticEncoding::Brotli => ("br", "br"),
            StaticEncoding::Gzip => ("gz", "gzip"),
        };
        let variant = PathBuf::from(format!("{}.{}", file.display(), extension));
        if !variant.exists() {
            continue;
        }
        let bytes = fs::read(&variant).map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_STATIC_FILES",
                format!("failed to read precompressed static asset: {error}"),
            )
            .with_path(&variant)
            .with_span(context.span)
        })?;
        if bytes.len() as u64 > options.max_file_bytes {
            return Err(Diagnostic::new(
                "SLOPPYC_E_STATIC_FILES",
                format!(
                    "precompressed static asset exceeds the configured inline limit of {} bytes",
                    options.max_file_bytes
                ),
            )
            .with_path(&variant)
            .with_span(context.span));
        }
        graph.add_dependency_asset(
            &variant,
            format!(
                "app.staticFiles:{}:{}:precompressed",
                options.request_path, options.root
            ),
        );
        let content_hash = sha256_bytes_hex(&bytes);
        variants.push(StaticAssetVariant {
            content_encoding,
            bytes,
            content_hash,
        });
    }
    Ok(variants)
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

fn static_asset_index_route_path(
    request_path: &str,
    relative: &Path,
    index: Option<&str>,
) -> Option<String> {
    let index = index?;
    if relative.file_name()?.to_str()? != index {
        return None;
    }
    let parent = relative.parent().unwrap_or_else(|| Path::new(""));
    let parent = resolver::normalized_artifact_id(parent, Path::new(""));
    if parent.split('/').any(|segment| {
        !segment.is_empty()
            && (segment == "." || segment == ".." || segment.contains('{') || segment.contains('}'))
    }) {
        return None;
    }
    let path = if parent.is_empty() {
        request_path.to_string()
    } else if request_path == "/" {
        format!("/{parent}")
    } else {
        format!("{request_path}/{parent}")
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
    options: &StaticFilesOptions,
    variants: &[StaticAssetVariant],
    spa_fallback: bool,
) -> Route {
    let byte_array = bytes
        .iter()
        .map(u8::to_string)
        .collect::<Vec<_>>()
        .join(",");
    let content_hash = sha256_bytes_hex(bytes);
    let variants_source = variants
        .iter()
        .map(|variant| {
            let bytes = variant
                .bytes
                .iter()
                .map(u8::to_string)
                .collect::<Vec<_>>()
                .join(",");
            format!(
                "{{ contentEncoding: {}, contentHash: {}, bytes: new Uint8Array([{}]) }}",
                json_string(variant.content_encoding),
                json_string(&variant.content_hash),
                bytes
            )
        })
        .collect::<Vec<_>>()
        .join(",");
    let cache_control = if spa_fallback {
        options
            .html_cache_control
            .as_deref()
            .or(options.cache_control.as_deref())
    } else {
        options
            .assets_cache_control
            .as_deref()
            .or(options.cache_control.as_deref())
    };
    let handler_source = format!(
        "function(ctx) {{ return __sloppyStaticAssetResponse(ctx, {{ contentType: {}, contentHash: {}, bytes: new Uint8Array([{byte_array}]), cacheControl: {}, variants: [{}], range: true }}); }}",
        json_string(content_type),
        json_string(&content_hash),
        cache_control
            .or_else(|| options
                .max_age_seconds
                .map(|_| "")
                .filter(|_| false))
            .map(json_string)
            .unwrap_or_else(|| {
                options
                    .max_age_seconds
                    .map(|seconds| json_string(&format!("public, max-age={seconds}")))
                    .unwrap_or_else(|| "undefined".to_string())
            }),
        variants_source,
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
        rate_limits: Vec::new(),
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
