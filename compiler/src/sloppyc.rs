use std::{
    collections::{BTreeMap, BTreeSet},
    ffi::OsString,
    fs,
    path::{Path, PathBuf},
};

use oxc_allocator::Allocator;
use oxc_ast::ast::{
    Argument, ArrayExpressionElement, BindingPattern, CallExpression, Expression,
    ExpressionStatement, ImportDeclarationSpecifier, ObjectPropertyKind, PropertyKey, PropertyKind,
    Statement,
};
use oxc_parser::Parser;
use oxc_span::{SourceType, Span};
use serde_json::{json, Value};

const COMPILER_VERSION: &str = "sloppyc-0.8.0-compiler-extraction-mvp";
const RUNTIME_MINIMUM_VERSION: &str = "0.1.0";
const STDLIB_VERSION: &str = "0.1.0";

#[derive(Debug, Eq, PartialEq)]
enum CliCommand {
    Help,
    Version,
    Build { input: PathBuf, out_dir: PathBuf },
    Invalid(String),
}

pub enum CliExit {
    Success,
    Output(String),
    Failure { code: i32, diagnostic: String },
}

#[derive(Debug, Clone)]
struct Diagnostic {
    code: &'static str,
    path: Option<PathBuf>,
    span: Option<Span>,
    message: String,
    hint: Option<String>,
}

#[derive(Debug)]
struct BuildFailure {
    code: i32,
    diagnostic: Diagnostic,
    source: Option<String>,
}

impl Diagnostic {
    fn new(code: &'static str, message: impl Into<String>) -> Self {
        Self {
            code,
            path: None,
            span: None,
            message: message.into(),
            hint: None,
        }
    }

    fn with_path(mut self, path: &Path) -> Self {
        self.path = Some(path.to_path_buf());
        self
    }

    fn with_span(mut self, span: Span) -> Self {
        self.span = Some(span);
        self
    }

    fn with_hint(mut self, hint: impl Into<String>) -> Self {
        self.hint = Some(hint.into());
        self
    }

    fn render(&self, source: Option<&str>) -> String {
        let location = match (&self.path, self.span, source) {
            (Some(path), Some(span), Some(source_text)) => {
                let (line, column) = line_column(source_text, span.start);
                format!("{}:{line}:{column}", display_path(path))
            }
            (Some(path), _, _) => display_path(path),
            _ => "sloppyc".to_string(),
        };

        let mut output = format!("{location}: {}: {}", self.code, self.message);
        if let (Some(path), Some(span), Some(source_text)) = (&self.path, self.span, source) {
            if let Some(frame) = source_frame(path, source_text, span) {
                output.push('\n');
                output.push_str(&frame);
            }
        }
        if let Some(hint) = &self.hint {
            output.push_str("\nhint: ");
            output.push_str(hint);
        }
        output
    }
}

#[derive(Debug, Clone)]
struct Route {
    method: &'static str,
    pattern: String,
    name: Option<String>,
    handler: Handler,
}

#[derive(Debug, Clone)]
struct Handler {
    source: String,
    span: Span,
}

#[derive(Debug)]
struct ExtractedApp {
    routes: Vec<Route>,
}

#[derive(Debug)]
struct AppState {
    sloppy_imported: bool,
    results_imported: bool,
    unsupported_import_alias: bool,
    unsupported_import_specifier: Option<(String, Span)>,
    app_vars: BTreeSet<String>,
    builder_vars: BTreeSet<String>,
    group_vars: BTreeMap<String, String>,
    routes: Vec<Route>,
    default_export: Option<String>,
}

impl AppState {
    fn new() -> Self {
        Self {
            sloppy_imported: false,
            results_imported: false,
            unsupported_import_alias: false,
            unsupported_import_specifier: None,
            app_vars: BTreeSet::new(),
            builder_vars: BTreeSet::new(),
            group_vars: BTreeMap::new(),
            routes: Vec::new(),
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
        CliCommand::Build { input, out_dir } => match build(&input, &out_dir) {
            Ok(()) => CliExit::Success,
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
        (Some(input), Some(out_dir)) => CliCommand::Build { input, out_dir },
        (None, _) => CliCommand::Invalid("build requires an input file".to_string()),
        (_, None) => CliCommand::Invalid("build requires --out <directory>".to_string()),
    }
}

fn version_text() -> String {
    "sloppyc 0.8.0-compiler-extraction-mvp\n".to_string()
}

fn help_text() -> String {
    let mut text = version_text();
    text.push_str(
        "Compiler extraction MVP: parses a narrow Sloppy app shape and emits deterministic artifacts.\n",
    );
    text.push('\n');
    text.push_str("Usage:\n");
    text.push_str("  sloppyc --help\n");
    text.push_str("  sloppyc --version\n");
    text.push_str("  sloppyc build <input.js> --out <directory>\n");
    text
}

fn build(input: &Path, out_dir: &Path) -> Result<(), Box<BuildFailure>> {
    let source = fs::read_to_string(input).map_err(|error| {
        Box::new(BuildFailure {
            code: 1,
            diagnostic: Diagnostic::new(
                "SLOPPYC_E_INPUT",
                format!("failed to read compiler input: {error}"),
            )
            .with_path(input),
            source: None,
        })
    })?;

    let extracted = extract(input, &source).map_err(|diagnostic| {
        Box::new(BuildFailure {
            code: 1,
            diagnostic,
            source: Some(source.clone()),
        })
    })?;
    write_artifacts(out_dir, &extracted).map_err(|diagnostic| {
        Box::new(BuildFailure {
            code: 1,
            diagnostic,
            source: Some(source),
        })
    })?;
    Ok(())
}

fn extract(path: &Path, source: &str) -> Result<ExtractedApp, Diagnostic> {
    if has_typescript_extension(path) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_INPUT",
            "compiler extraction MVP accepts JavaScript input only",
        )
        .with_path(path)
        .with_hint(
            "Use a .js/.mjs source file and omit TypeScript-only handler syntax for this MVP.",
        ));
    }

    let source_type = SourceType::from_path(path).map_err(|_| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_INPUT",
            "compiler input must use a JavaScript file extension",
        )
        .with_path(path)
    })?;
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, source_type).parse();

    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(
            Diagnostic::new("SLOPPYC_E_PARSE", format!("failed to parse input: {error}"))
                .with_path(path),
        );
    }

    let mut state = AppState::new();
    for statement in &parsed.program.body {
        match statement {
            Statement::ImportDeclaration(import) => extract_import(&mut state, import),
            Statement::VariableDeclaration(declaration) => {
                extract_variable_declaration(path, source, &mut state, declaration)?
            }
            Statement::ExpressionStatement(statement) => {
                extract_expression_statement(path, source, &mut state, statement)?
            }
            Statement::ExportDefaultDeclaration(export) => {
                state.default_export = export_default_identifier(&export.declaration);
            }
            _ => return Err(top_level_statement_diagnostic(path, source, statement)),
        }
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
            "compiler extraction MVP supports exactly one app object",
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
            .with_path(path)
            .with_span(route.handler.span));
        }
        if let Some(name) = &route.name {
            if !route_names.insert(name.clone()) {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_DUPLICATE_ROUTE_NAME",
                    "duplicate route name",
                )
                .with_path(path)
                .with_span(route.handler.span));
            }
        }
    }

    Ok(ExtractedApp {
        routes: state.routes,
    })
}

fn extract_import(state: &mut AppState, import: &oxc_ast::ast::ImportDeclaration<'_>) {
    if import.source.value.as_str() != "sloppy" {
        state.unsupported_import_specifier =
            Some((import.source.value.as_str().to_string(), import.source.span));
        return;
    }

    if let Some(specifiers) = &import.specifiers {
        for specifier in specifiers {
            if let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier {
                let imported = specifier.imported.name().as_str();
                let local = specifier.local.name.as_str();
                if matches!(imported, "Sloppy" | "Results") && imported != local {
                    state.unsupported_import_alias = true;
                }
                if imported == "Sloppy" && local == "Sloppy" {
                    state.sloppy_imported = true;
                }
                if imported == "Results" && local == "Results" {
                    state.results_imported = true;
                }
            }
        }
    }
}

fn extract_variable_declaration(
    path: &Path,
    source: &str,
    state: &mut AppState,
    declaration: &oxc_ast::ast::VariableDeclaration<'_>,
) -> Result<(), Diagnostic> {
    for declarator in &declaration.declarations {
        let Some(name) = binding_identifier(&declarator.id) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_BINDING",
                "compiler extraction MVP only supports identifier bindings",
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
        } else if let Some((app_name, prefix)) = app_group_call(init) {
            if !state.app_vars.contains(app_name) {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_ROUTE_GROUP",
                    "route groups must be created from the extracted app object",
                )
                .with_path(path)
                .with_span(init.span()));
            }
            state
                .group_vars
                .insert(name.to_string(), prefix.to_string());
        } else {
            validate_supported_initializer(path, source, state, init)?;
        }
    }
    Ok(())
}

fn extract_expression_statement(
    path: &Path,
    source: &str,
    state: &mut AppState,
    statement: &ExpressionStatement<'_>,
) -> Result<(), Diagnostic> {
    let (route_expr, name) = match &statement.expression {
        Expression::CallExpression(call) => match with_name_call(call)? {
            Some((inner, name)) => (inner, Some(name)),
            None => (&statement.expression, None),
        },
        _ => (&statement.expression, None),
    };

    let Some((receiver, pattern, handler)) = map_get_call(route_expr, source) else {
        if let Some(diagnostic) = unsupported_route_call_diagnostic(path, route_expr, source, state)
        {
            return Err(diagnostic);
        }

        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_TOP_LEVEL",
            "unsupported top-level expression in compiler extraction MVP",
        )
        .with_path(path)
        .with_span(statement.span)
        .with_hint("Use app.mapGet(\"/literal\", handler) or app.mapGroup(\"/prefix\").mapGet(\"/child\", handler)."));
    };

    let full_pattern = if state.app_vars.contains(receiver) {
        pattern.to_string()
    } else if let Some(prefix) = state.group_vars.get(receiver) {
        join_route_patterns(prefix, pattern)
    } else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_TARGET",
            "mapGet must be called on the extracted app or a route group variable",
        )
        .with_path(path)
        .with_span(statement.span));
    };

    state.routes.push(Route {
        method: "GET",
        pattern: full_pattern,
        name,
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
    if property != "mapGet" {
        if property.starts_with("map")
            && (state.app_vars.contains(receiver) || state.group_vars.contains_key(receiver))
        {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HTTP_METHOD",
                    "only app.mapGet(...) route extraction is supported",
                )
                .with_path(path)
                .with_span(call.span)
                .with_hint("Non-GET methods are deferred from the compiler extraction MVP."),
            );
        }
        return None;
    }

    if call.arguments.len() != 2 {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_SHAPE",
                "mapGet requires a literal pattern and one handler",
            )
            .with_path(path)
            .with_span(call.span),
        );
    }

    if call.arguments.first().and_then(string_argument).is_none() {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_DYNAMIC_ROUTE_PATTERN",
                "mapGet route pattern must be a string literal",
            )
            .with_path(path)
            .with_span(call.span)
            .with_hint("Dynamic route strings are not part of the compiler extraction MVP."),
        );
    }

    if call
        .arguments
        .get(1)
        .and_then(|argument| handler_from_argument(argument, source))
        .is_none()
    {
        let handler_argument = call.arguments.get(1)?;
        return Some(handler_diagnostic(path, handler_argument, call.span));
    }

    None
}

fn validate_supported_initializer(
    path: &Path,
    source: &str,
    state: &AppState,
    init: &Expression<'_>,
) -> Result<(), Diagnostic> {
    if let Some((_, _, _)) = map_get_call(init, source) {
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
        .with_hint("List compiler-extracted routes as explicit top-level mapGet calls.");
    }

    Diagnostic::new(
        "SLOPPYC_E_UNSUPPORTED_TOP_LEVEL",
        "unsupported top-level syntax in compiler extraction MVP",
    )
    .with_path(path)
    .with_span(span)
    .with_hint(
        "Use imports, const app/builder/group declarations, mapGet calls, and export default app.",
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
    if property != "mapGroup" || call.arguments.len() != 1 {
        return None;
    }
    let prefix = string_argument(call.arguments.first()?)?;
    Some((object, prefix))
}

fn map_get_call<'a>(
    expression: &'a Expression<'a>,
    source: &str,
) -> Option<(&'a str, &'a str, Handler)> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if property != "mapGet" || call.arguments.len() != 2 {
        return None;
    }

    let pattern = string_argument(call.arguments.first()?)?;
    let handler_arg = call.arguments.get(1)?;
    let handler = handler_from_argument(handler_arg, source)?;
    Some((receiver, pattern, handler))
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
    let Expression::StaticMemberExpression(member) = expression else {
        return None;
    };
    let object = match &member.object {
        Expression::Identifier(identifier) => identifier.name.as_str(),
        _ => return None,
    };
    Some((object, member.property.name.as_str()))
}

fn computed_member_receiver<'a>(expression: &'a Expression<'a>) -> Option<&'a str> {
    let Expression::ComputedMemberExpression(member) = expression else {
        return None;
    };
    match &member.object {
        Expression::Identifier(identifier) => Some(identifier.name.as_str()),
        _ => None,
    }
}

fn string_argument<'a>(argument: &'a Argument<'a>) -> Option<&'a str> {
    match argument {
        Argument::StringLiteral(literal) => Some(literal.value.as_str()),
        _ => None,
    }
}

fn handler_from_argument(argument: &Argument<'_>, source: &str) -> Option<Handler> {
    match argument {
        Argument::ArrowFunctionExpression(function) => {
            if handler_parameters_are_unsupported(&function.params)
                || arrow_has_typescript_syntax(function)
                || !handler_body_is_supported_arrow(function)
            {
                return None;
            }
            Some(Handler {
                source: source_slice(source, function.span)?,
                span: function.span,
            })
        }
        Argument::FunctionExpression(function) => {
            if handler_parameters_are_unsupported(&function.params)
                || function_has_typescript_syntax(function)
                || !handler_body_is_supported_function(function)
            {
                return None;
            }
            Some(Handler {
                source: source_slice(source, function.span)?,
                span: function.span,
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
                    "TypeScript handler syntax is not supported by the compiler extraction MVP",
                    Some("Use JavaScript handler syntax until TypeScript lowering is implemented."),
                )
            } else if handler_result_uses_unsupported_values_arrow(function) {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE",
                    "route handler result arguments must be inline JSON-safe values",
                    Some("Use literals, arrays, and object literals instead of closed-over bindings."),
                )
            } else {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER",
                    "route handler must be a simple function returning Results.text(...), Results.json(...), Results.ok(...), or Results.noContent()",
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
                    "TypeScript handler syntax is not supported by the compiler extraction MVP",
                    Some("Use JavaScript handler syntax until TypeScript lowering is implemented."),
                )
            } else if handler_result_uses_unsupported_values_function(function) {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE",
                    "route handler result arguments must be inline JSON-safe values",
                    Some("Use literals, arrays, and object literals instead of closed-over bindings."),
                )
            } else {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER",
                    "route handler must be a simple function returning Results.text(...), Results.json(...), Results.ok(...), or Results.noContent()",
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

fn has_typescript_extension(path: &Path) -> bool {
    path.extension()
        .and_then(|extension| extension.to_str())
        .is_some_and(|extension| {
            matches!(
                extension.to_ascii_lowercase().as_str(),
                "ts" | "tsx" | "mts" | "cts"
            )
        })
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
    if function.r#async {
        return false;
    }

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
    if function.r#async || function.generator || function.body.is_none() {
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
        object == "Results" && matches!(property, "text" | "json" | "ok" | "noContent")
    }) {
        Some(call)
    } else {
        None
    }
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

    if property == "noContent" {
        return call.arguments.is_empty();
    }

    matches!(property, "text" | "json" | "ok")
        && matches!(call.arguments.len(), 1 | 2)
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
    let source_map = emit_source_map();
    let plan = emit_plan(app)?;

    write_artifact(out_dir, "app.js", &app_js)?;
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

fn emit_plan(app: &ExtractedApp) -> Result<String, Diagnostic> {
    let handlers = app
        .routes
        .iter()
        .enumerate()
        .map(|(index, route)| {
            let id = index + 1;
            json!({
                "id": id,
                "exportName": format!("__sloppy_handler_{id}"),
                "displayName": route.name.clone().unwrap_or_else(|| format!("GET {}", route.pattern)),
            })
        })
        .collect::<Vec<_>>();

    let routes = app
        .routes
        .iter()
        .enumerate()
        .map(|(index, route)| {
            let id = index + 1;
            json!({
                "method": route.method,
                "pattern": route.pattern,
                "handlerId": id,
                "name": route.name,
            })
        })
        .collect::<Vec<_>>();

    let value = json!({
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
            "id": "compiler-mvp-app-js",
            "hash": "sha256-compiler-mvp-placeholder"
        },
        "sourceMap": {
            "path": "app.js.map",
            "id": "compiler-mvp-app-js-map",
            "hash": "sha256-compiler-mvp-placeholder"
        },
        "handlers": handlers,
        "routes": routes
    });

    serde_json::to_string_pretty(&value)
        .map(|json| format!("{json}\n"))
        .map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_EMIT",
                format!("failed to emit app.plan.json: {error}"),
            )
        })
}

fn emit_app_js(app: &ExtractedApp) -> String {
    let mut output = String::new();
    output.push_str("const __sloppyRuntime = globalThis.__sloppy_runtime;\n");
    output.push_str("if (__sloppyRuntime === undefined) {\n");
    output.push_str("  throw new Error(\"Sloppy bootstrap runtime was not loaded\");\n");
    output.push_str("}\n");
    output.push_str("const { Results } = __sloppyRuntime;\n\n");

    for (index, route) in app.routes.iter().enumerate() {
        let id = index + 1;
        output.push_str(&format!("globalThis.__sloppy_handler_{id} = "));
        output.push_str(&route.handler.source);
        output.push_str(";\n");
        output.push_str(&format!(
            "globalThis.__sloppy_register_handler({id}, globalThis.__sloppy_handler_{id});\n"
        ));
    }

    output
}

fn emit_source_map() -> String {
    let value = json!({
        "version": 3,
        "file": "app.js",
        "sources": [],
        "sourcesContent": [],
        "names": [],
        "mappings": ""
    });

    let Value::Object(_) = value else {
        return "{}\n".to_string();
    };
    let json = serde_json::to_string_pretty(&value).unwrap_or_else(|_| "{}".to_string());
    format!("{json}\n")
}

fn line_column(source: &str, offset: u32) -> (usize, usize) {
    let target = usize::try_from(offset).unwrap_or(source.len());
    let mut line = 1;
    let mut last_newline_byte = 0usize;

    for (index, character) in source.char_indices() {
        if index >= target {
            break;
        }
        if character == '\n' {
            line += 1;
            last_newline_byte = index + 1;
        }
    }

    let column = target.saturating_sub(last_newline_byte) + 1;
    (line, column)
}

fn line_bounds(source: &str, offset: usize) -> Option<(usize, usize)> {
    if offset > source.len() {
        return None;
    }
    let line_start = source[..offset].rfind('\n').map_or(0, |index| index + 1);
    let line_end = source[offset..]
        .find('\n')
        .map_or(source.len(), |index| offset + index);
    let line_end = if line_end > line_start && source.as_bytes()[line_end - 1] == b'\r' {
        line_end - 1
    } else {
        line_end
    };
    Some((line_start, line_end))
}

fn source_frame(path: &Path, source: &str, span: Span) -> Option<String> {
    let start = usize::try_from(span.start).ok()?;
    let end = usize::try_from(span.end).ok()?;
    let (line, column) = line_column(source, span.start);
    let (line_start, line_end) = line_bounds(source, start)?;
    let line_text = &source[line_start..line_end];
    let underline = end.saturating_sub(start).max(1);
    let prefix = format!("{line} | ");
    let mut output = String::new();

    output.push_str(&format!("  --> {}:{line}:{column}\n", display_path(path)));
    output.push_str("   |\n");
    output.push_str(&prefix);
    output.push_str(line_text);
    output.push('\n');
    output.push_str("   | ");
    output.push_str(&" ".repeat(column.saturating_sub(1)));
    output.push_str(&"^".repeat(underline.min(line_text.len().saturating_add(1))));
    Some(output)
}

fn display_path(path: &Path) -> String {
    path.to_string_lossy().replace('\\', "/")
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
    use std::{ffi::OsString, fs};

    use super::{command_from_args, extract, CliCommand};

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
    fn accepts_ok_and_no_content_result_helpers() {
        let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapGet("/ok", () => Results.ok({ ok: true }));
app.mapGet("/empty", () => Results.noContent());
export default app;
"#;
        let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
        assert_eq!(app.routes.len(), 2);
        assert_eq!(app.routes[0].pattern, "/ok");
        assert_eq!(app.routes[1].pattern, "/empty");
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
            assert_eq!(emitted_js, expected_js, "{fixture_name} app.js");

            let emitted_plan = super::emit_plan(&app).expect("plan should emit");
            let expected_plan = fs::read_to_string(
                root.join("tests/fixtures")
                    .join(fixture_name)
                    .join("expected/app.plan.json"),
            )
            .expect("expected app.plan.json should exist");
            assert_eq!(emitted_plan, expected_plan, "{fixture_name} app.plan.json");

            let emitted_source_map = super::emit_source_map();
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
            ("unsupported-import-specifier", "input.js"),
            ("node-fs-import", "input.js"),
            ("missing-app", "input.js"),
            ("multiple-apps", "input.js"),
            ("unsupported-http-method", "input.js"),
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
                .replace(&super::display_path(root), "<compiler>");
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

        let failure = super::build(&input, &out_dir).expect_err("fixture should fail to build");
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

        super::build(&input, &out_dir).expect("compiler example should build");

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

        super::build(&input, &first).expect("first build should succeed");
        super::build(&input, &second).expect("second build should succeed");

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
