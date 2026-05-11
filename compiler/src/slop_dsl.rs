//! Slop DSL recognition helpers.

use oxc_ast::ast::{Argument, CallExpression, Expression};

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum SlopFactory {
    Create,
    CreateBuilder,
}

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum RouteMethod {
    Get,
    Post,
    Put,
    Patch,
    Delete,
}

impl RouteMethod {
    pub fn from_property(property: &str) -> Option<Self> {
        match property {
            "mapGet" | "get" => Some(Self::Get),
            "sse" | "ws" => Some(Self::Get),
            "mapPost" | "post" => Some(Self::Post),
            "mapPut" | "put" => Some(Self::Put),
            "mapPatch" | "patch" => Some(Self::Patch),
            "mapDelete" | "delete" => Some(Self::Delete),
            _ => None,
        }
    }

    pub fn as_plan_method(self) -> &'static str {
        match self {
            Self::Get => "GET",
            Self::Post => "POST",
            Self::Put => "PUT",
            Self::Patch => "PATCH",
            Self::Delete => "DELETE",
        }
    }
}

pub fn slop_factory_call(expression: &Expression<'_>) -> Option<SlopFactory> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    if !call.arguments.is_empty() {
        return None;
    }
    match static_member_name(&call.callee) {
        Some(("Sloppy", "create")) => Some(SlopFactory::Create),
        Some(("Sloppy", "createBuilder")) => Some(SlopFactory::CreateBuilder),
        _ => None,
    }
}

pub fn route_method_from_property(property: &str) -> Option<&'static str> {
    RouteMethod::from_property(property).map(RouteMethod::as_plan_method)
}

pub fn route_kind_from_property(property: &str) -> Option<&'static str> {
    match property {
        "sse" => Some("sse"),
        "ws" => Some("websocket"),
        _ => RouteMethod::from_property(property).map(|_| "http"),
    }
}

pub fn static_member_name<'a>(expression: &'a Expression<'a>) -> Option<(&'a str, &'a str)> {
    let Expression::StaticMemberExpression(member) = expression else {
        return None;
    };
    let object = match &member.object {
        Expression::Identifier(identifier) => identifier.name.as_str(),
        _ => return None,
    };
    Some((object, member.property.name.as_str()))
}

pub fn static_member_chain<'a>(expression: &'a Expression<'a>) -> Option<Vec<&'a str>> {
    let mut parts = Vec::new();
    let mut current = expression;

    loop {
        match current {
            Expression::Identifier(identifier) => {
                parts.push(identifier.name.as_str());
                parts.reverse();
                return Some(parts);
            }
            Expression::StaticMemberExpression(member) => {
                parts.push(member.property.name.as_str());
                current = &member.object;
            }
            _ => return None,
        }
    }
}

pub fn computed_member_receiver<'a>(expression: &'a Expression<'a>) -> Option<&'a str> {
    let Expression::ComputedMemberExpression(member) = expression else {
        return None;
    };
    match &member.object {
        Expression::Identifier(identifier) => Some(identifier.name.as_str()),
        _ => None,
    }
}

pub fn string_argument<'a>(argument: &'a Argument<'a>) -> Option<&'a str> {
    match argument {
        Argument::StringLiteral(literal) => Some(literal.value.as_str()),
        _ => None,
    }
}

pub fn sqlite_provider_call_name<'a>(call: &'a CallExpression<'a>) -> Option<&'a str> {
    let Expression::Identifier(callee) = &call.callee else {
        return None;
    };
    if callee.name.as_str() != "sqlite" || call.arguments.is_empty() || call.arguments.len() > 2 {
        return None;
    }
    string_argument(call.arguments.first()?)
}

pub fn is_results_helper<'a>(expression: &'a Expression<'a>) -> Option<&'a str> {
    let (receiver, helper) = static_member_name(expression)?;
    if receiver == "Results" {
        Some(helper)
    } else {
        None
    }
}

pub fn is_ctx_binding_helper<'a>(expression: &'a Expression<'a>) -> Option<(&'a str, &'a str)> {
    let chain = static_member_chain(expression)?;
    if chain.len() == 3 && chain[0] == "ctx" && matches!(chain[1], "route" | "query" | "header") {
        return Some((chain[1], chain[2]));
    }
    if chain.len() == 3 && chain[0] == "ctx" && chain[1] == "body" {
        return Some(("body", chain[2]));
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;
    use oxc_allocator::Allocator;
    use oxc_parser::Parser;
    use oxc_span::SourceType;

    #[test]
    fn recognizes_route_methods_and_helpers() {
        assert_eq!(route_method_from_property("get"), Some("GET"));
        assert_eq!(route_method_from_property("mapDelete"), Some("DELETE"));
        assert_eq!(route_method_from_property("sse"), Some("GET"));
        assert_eq!(route_method_from_property("ws"), Some("GET"));
        assert_eq!(route_method_from_property("connect"), None);

        assert_eq!(route_kind_from_property("get"), Some("http"));
        assert_eq!(route_kind_from_property("sse"), Some("sse"));
        assert_eq!(route_kind_from_property("ws"), Some("websocket"));
        assert_eq!(route_kind_from_property("connect"), None);
    }

    #[test]
    fn recognizes_results_and_context_helpers() {
        let allocator = Allocator::default();
        let parsed = Parser::new(
            &allocator,
            "Results.json();\nctx.route.id();",
            SourceType::mjs(),
        )
        .parse();
        assert!(parsed.errors.is_empty());

        let oxc_ast::ast::Statement::ExpressionStatement(results_statement) =
            &parsed.program.body[0]
        else {
            panic!("fixture should be an expression statement");
        };
        let Expression::CallExpression(call) = &results_statement.expression else {
            panic!("fixture should call");
        };
        assert_eq!(is_results_helper(&call.callee), Some("json"));

        let oxc_ast::ast::Statement::ExpressionStatement(ctx_statement) = &parsed.program.body[1]
        else {
            panic!("fixture should be an expression statement");
        };
        let Expression::CallExpression(call) = &ctx_statement.expression else {
            panic!("fixture should call");
        };
        assert_eq!(is_ctx_binding_helper(&call.callee), Some(("route", "id")));
    }
}
