//! Compiler graph validation and Plan completeness helpers.

#[derive(Debug, Clone, Copy, Eq, PartialEq, Ord, PartialOrd)]
pub enum CompletenessStatus {
    Complete,
    Partial,
    RuntimeOnly,
    Invalid,
}

impl CompletenessStatus {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Complete => "complete",
            Self::Partial => "partial",
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
}

pub fn route_completeness(input: &RouteCompletenessInput) -> Completeness {
    if input.missing_provider_registration {
        return Completeness::invalid(vec![CompletenessReason::new(
            "missing-provider",
            "route uses a provider effect that is not registered in the Plan",
        )]);
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
        .any(|route| route.status == CompletenessStatus::Partial)
    {
        return Completeness::partial(vec![CompletenessReason::new(
            "partial-route",
            "one or more routes have partial inferred metadata",
        )]);
    }

    Completeness::complete()
}
