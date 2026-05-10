//! Compiler graph validation and Plan completeness helpers.

#[derive(Debug, Clone, Copy, Eq, PartialEq, Ord, PartialOrd)]
pub enum CompletenessStatus {
    Complete,
    Partial,
    Declared,
    Dynamic,
    Opaque,
    RuntimeOnly,
    Invalid,
}

impl CompletenessStatus {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Complete => "complete",
            Self::Partial => "partial",
            Self::Declared => "declared",
            Self::Dynamic => "dynamic",
            Self::Opaque => "opaque",
            Self::RuntimeOnly => "runtime-only",
            Self::Invalid => "invalid",
        }
    }
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct CompletenessReason {
    pub code: &'static str,
    pub message: &'static str,
}

impl CompletenessReason {
    pub fn new(code: &'static str, message: &'static str) -> Self {
        Self { code, message }
    }
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct Completeness {
    pub status: CompletenessStatus,
    pub reasons: Vec<CompletenessReason>,
}

impl Completeness {
    pub fn complete() -> Self {
        Self {
            status: CompletenessStatus::Complete,
            reasons: Vec::new(),
        }
    }

    pub fn partial(reasons: Vec<CompletenessReason>) -> Self {
        Self {
            status: CompletenessStatus::Partial,
            reasons,
        }
    }

    pub fn runtime_only() -> Self {
        Self {
            status: CompletenessStatus::RuntimeOnly,
            reasons: vec![CompletenessReason::new(
                "runtime-only-route",
                "route explicitly requires runtime-only metadata",
            )],
        }
    }

    pub fn dynamic(reasons: Vec<CompletenessReason>) -> Self {
        Self {
            status: CompletenessStatus::Dynamic,
            reasons,
        }
    }

    pub fn opaque(reasons: Vec<CompletenessReason>) -> Self {
        Self {
            status: CompletenessStatus::Opaque,
            reasons,
        }
    }

    pub fn invalid(reasons: Vec<CompletenessReason>) -> Self {
        Self {
            status: CompletenessStatus::Invalid,
            reasons,
        }
    }
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct RouteCompletenessInput {
    pub has_response_metadata: bool,
    pub body_json_without_schema: bool,
    pub missing_provider_registration: bool,
    pub runtime_only: bool,
}

pub fn route_completeness(input: &RouteCompletenessInput) -> Completeness {
    if input.missing_provider_registration {
        return Completeness::invalid(vec![CompletenessReason::new(
            "missing-provider",
            "route uses a provider effect that is not registered in the Plan",
        )]);
    }
    if input.runtime_only {
        return Completeness::runtime_only();
    }

    let mut reasons = Vec::new();
    if !input.has_response_metadata {
        reasons.push(CompletenessReason::new(
            "response-metadata-missing",
            "route response metadata could not be fully inferred",
        ));
    }
    if input.body_json_without_schema {
        reasons.push(CompletenessReason::new(
            "body-schema-missing",
            "route reads JSON body data without a declared schema",
        ));
    }

    if reasons.is_empty() {
        Completeness::complete()
    } else {
        Completeness::partial(reasons)
    }
}

pub fn plan_completeness(routes: &[Completeness]) -> Completeness {
    if routes
        .iter()
        .any(|route| route.status == CompletenessStatus::Invalid)
    {
        return Completeness::invalid(vec![CompletenessReason::new(
            "invalid-route",
            "one or more routes are invalid",
        )]);
    }

    if routes
        .iter()
        .any(|route| route.status == CompletenessStatus::RuntimeOnly)
    {
        return Completeness {
            status: CompletenessStatus::RuntimeOnly,
            reasons: vec![CompletenessReason::new(
                "runtime-only-route",
                "one or more routes explicitly require runtime-only metadata",
            )],
        };
    }

    if routes
        .iter()
        .any(|route| route.status == CompletenessStatus::Dynamic)
    {
        return Completeness::dynamic(vec![CompletenessReason::new(
            "dynamic-route",
            "one or more routes have dynamic metadata",
        )]);
    }

    if routes
        .iter()
        .any(|route| route.status == CompletenessStatus::Partial)
    {
        return Completeness::partial(vec![CompletenessReason::new(
            "partial-route",
            "one or more routes have partial inferred metadata",
        )]);
    }

    Completeness::complete()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn route_runtime_only_flows_to_plan_completeness() {
        let route = route_completeness(&RouteCompletenessInput {
            has_response_metadata: true,
            body_json_without_schema: false,
            missing_provider_registration: false,
            runtime_only: true,
        });
        assert_eq!(route.status, CompletenessStatus::RuntimeOnly);

        let plan = plan_completeness(&[route]);
        assert_eq!(plan.status, CompletenessStatus::RuntimeOnly);
        assert_eq!(plan.reasons[0].code, "runtime-only-route");
    }

    #[test]
    fn invalid_provider_registration_wins_over_runtime_only() {
        let route = route_completeness(&RouteCompletenessInput {
            has_response_metadata: true,
            body_json_without_schema: false,
            missing_provider_registration: true,
            runtime_only: true,
        });
        assert_eq!(route.status, CompletenessStatus::Invalid);
    }
}
