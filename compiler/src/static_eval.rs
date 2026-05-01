//! Static evaluation for supported literal route, config, schema, and metadata values.

use std::collections::BTreeMap;

use oxc_ast::ast::{Argument, BinaryExpression, BinaryOperator, Expression};

#[derive(Debug, Default)]
pub struct StaticStringEnv {
    aliases: BTreeMap<String, String>,
}

impl StaticStringEnv {
    pub fn bind(&mut self, name: impl Into<String>, value: impl Into<String>) {
        self.aliases.insert(name.into(), value.into());
    }

    pub fn get(&self, name: &str) -> Option<&str> {
        self.aliases.get(name).map(String::as_str)
    }
}

pub fn eval_string_argument(argument: &Argument<'_>, env: &StaticStringEnv) -> Option<String> {
    match argument {
        Argument::StringLiteral(literal) => Some(literal.value.as_str().to_string()),
        Argument::Identifier(identifier) => {
            env.get(identifier.name.as_str()).map(ToOwned::to_owned)
        }
        Argument::BinaryExpression(binary) => eval_string_binary_expression(binary, env),
        _ => None,
    }
}

pub fn eval_string_expression(
    expression: &Expression<'_>,
    env: &StaticStringEnv,
) -> Option<String> {
    match expression {
        Expression::StringLiteral(literal) => Some(literal.value.as_str().to_string()),
        Expression::Identifier(identifier) => {
            env.get(identifier.name.as_str()).map(ToOwned::to_owned)
        }
        Expression::BinaryExpression(binary) => eval_string_binary_expression(binary, env),
        Expression::ParenthesizedExpression(parenthesized) => {
            eval_string_expression(&parenthesized.expression, env)
        }
        _ => None,
    }
}

fn eval_string_binary_expression(
    binary: &BinaryExpression<'_>,
    env: &StaticStringEnv,
) -> Option<String> {
    if binary.operator != BinaryOperator::Addition {
        return None;
    }
    let left = eval_string_expression(&binary.left, env)?;
    let right = eval_string_expression(&binary.right, env)?;
    Some(format!("{left}{right}"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use oxc_allocator::Allocator;
    use oxc_parser::Parser;
    use oxc_span::SourceType;

    #[test]
    fn evaluates_string_literal_aliases_and_concatenation() {
        let allocator = Allocator::default();
        let parsed = Parser::new(
            &allocator,
            r#"const route = "/api" + "/users"; app.get(route, handler);"#,
            SourceType::mjs(),
        )
        .parse();
        assert!(parsed.errors.is_empty());

        let mut env = StaticStringEnv::default();
        let statement = &parsed.program.body[0];
        let oxc_ast::ast::Statement::VariableDeclaration(declaration) = statement else {
            panic!("fixture declaration should parse");
        };
        let declarator = &declaration.declarations[0];
        let value = eval_string_expression(
            declarator.init.as_ref().expect("initializer should exist"),
            &env,
        )
        .expect("concatenation should evaluate");
        env.bind("route", value);
        assert_eq!(env.get("route"), Some("/api/users"));
    }
}
