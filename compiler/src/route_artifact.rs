use crate::diagnostic::Diagnostic;
use crate::graph::{
    route_pattern_has_params, route_pattern_param_count, ExtractedApp, ProjectKind,
};

pub(crate) const ROUTE_ARTIFACT_PATH: &str = "routes.slrt";
pub(crate) const ROUTE_ARTIFACT_VERSION: u32 = 1;

const HEADER_SIZE: usize = 64;
const ROUTE_ENTRY_SIZE: usize = 48;
const ENDIAN_MARKER: u32 = 0x0102_0304;
const CHECKSUM_OFFSET: usize = 40;
const FNV_OFFSET_BASIS: u64 = 0xcbf2_9ce4_8422_2325;
const FNV_PRIME: u64 = 0x0000_0100_0000_01b3;

#[derive(Debug, Clone)]
pub(crate) struct RouteDispatchArtifactMetadata {
    pub(crate) path: String,
    pub(crate) hash: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum RouteExecutionKind {
    V8Handler,
    NativeStaticText,
    NativeStaticJson,
}

impl RouteExecutionKind {
    pub(crate) fn as_plan_str(self) -> &'static str {
        match self {
            Self::V8Handler => "v8-handler",
            Self::NativeStaticText => "native-static-text",
            Self::NativeStaticJson => "native-static-json",
        }
    }

    fn artifact_code(self) -> u32 {
        match self {
            Self::V8Handler => 1,
            Self::NativeStaticText => 2,
            Self::NativeStaticJson => 3,
        }
    }
}

pub(crate) fn route_execution_kind(
    response_kind: Option<&str>,
    native_body: Option<&str>,
) -> RouteExecutionKind {
    match (response_kind, native_body) {
        (Some("text"), Some(_)) => RouteExecutionKind::NativeStaticText,
        (Some("json"), Some(_)) => RouteExecutionKind::NativeStaticJson,
        _ => RouteExecutionKind::V8Handler,
    }
}

fn route_artifact_pattern_metadata(pattern: &str) -> Result<(u32, u32), Diagnostic> {
    let has_params = route_pattern_has_params(pattern);
    let param_count = checked_u32(route_pattern_param_count(pattern), "route parameter count")?;
    Ok((if has_params { 2 } else { 1 }, param_count))
}

pub(crate) fn emit_route_artifact(app: &ExtractedApp) -> Result<Option<Vec<u8>>, Diagnostic> {
    if app.kind != ProjectKind::Web {
        return Ok(None);
    }

    let route_count = checked_u32(app.routes.len(), "route count")?;
    let route_table_offset = checked_u32(HEADER_SIZE, "route table offset")?;
    let route_table_size = checked_u32(
        app.routes.len().saturating_mul(ROUTE_ENTRY_SIZE),
        "route table size",
    )?;
    let string_table_offset = checked_u32(
        HEADER_SIZE + app.routes.len() * ROUTE_ENTRY_SIZE,
        "string table offset",
    )?;

    let mut entries = Vec::<RouteEntry>::with_capacity(app.routes.len());
    let mut strings = Vec::<u8>::new();

    for (index, route) in app.routes.iter().enumerate() {
        let pattern_offset = checked_u32(strings.len(), "route pattern string offset")?;
        strings.extend_from_slice(route.pattern.as_bytes());
        let pattern_len = checked_u32(route.pattern.len(), "route pattern string length")?;

        let name = route.name.as_deref().unwrap_or("");
        let name_offset = checked_u32(strings.len(), "route name string offset")?;
        strings.extend_from_slice(name.as_bytes());
        let name_len = checked_u32(name.len(), "route name string length")?;

        let response_kind = route
            .handler
            .response
            .as_ref()
            .map(|response| response.kind.as_str());
        let native_body = route
            .handler
            .response
            .as_ref()
            .and_then(|response| response.native_body.as_deref());
        let (strategy, param_count) = route_artifact_pattern_metadata(&route.pattern)?;

        entries.push(RouteEntry {
            method: route_method_code(route.method)?,
            handler_id: checked_u32(index + 1, "handler id")?,
            pattern_offset,
            pattern_len,
            name_offset,
            name_len,
            strategy,
            execution_kind: route_execution_kind(response_kind, native_body).artifact_code(),
            param_count,
            flags: 0,
        });
    }

    let string_table_size = checked_u32(strings.len(), "string table size")?;
    let mut bytes =
        Vec::<u8>::with_capacity(HEADER_SIZE + entries.len() * ROUTE_ENTRY_SIZE + strings.len());
    push_bytes(&mut bytes, b"SLRT");
    push_u32(&mut bytes, ROUTE_ARTIFACT_VERSION);
    push_u32(&mut bytes, ENDIAN_MARKER);
    push_u32(&mut bytes, HEADER_SIZE as u32);
    push_u32(&mut bytes, route_count);
    push_u32(&mut bytes, route_count);
    push_u32(&mut bytes, route_table_offset);
    push_u32(&mut bytes, route_table_size);
    push_u32(&mut bytes, string_table_offset);
    push_u32(&mut bytes, string_table_size);
    push_u64(&mut bytes, 0);
    push_u32(&mut bytes, 0);
    push_u32(&mut bytes, 0);
    push_u32(&mut bytes, string_table_offset);
    push_u32(&mut bytes, string_table_size);

    for entry in entries {
        push_u32(&mut bytes, entry.method);
        push_u32(&mut bytes, entry.handler_id);
        push_u32(&mut bytes, entry.pattern_offset);
        push_u32(&mut bytes, entry.pattern_len);
        push_u32(&mut bytes, entry.name_offset);
        push_u32(&mut bytes, entry.name_len);
        push_u32(&mut bytes, entry.strategy);
        push_u32(&mut bytes, entry.execution_kind);
        push_u32(&mut bytes, entry.param_count);
        push_u32(&mut bytes, entry.flags);
        push_u32(&mut bytes, 0);
        push_u32(&mut bytes, 0);
    }
    push_bytes(&mut bytes, &strings);

    let checksum = fnv1a64_with_zeroed_checksum(&bytes);
    bytes[CHECKSUM_OFFSET..CHECKSUM_OFFSET + 8].copy_from_slice(&checksum.to_le_bytes());
    Ok(Some(bytes))
}

fn route_method_code(method: &str) -> Result<u32, Diagnostic> {
    match method {
        "GET" => Ok(1),
        "POST" => Ok(2),
        "PUT" => Ok(3),
        "DELETE" => Ok(4),
        "PATCH" => Ok(5),
        "OPTIONS" => Ok(6),
        "HEAD" => Ok(7),
        _ => Err(Diagnostic::new(
            "SLOPPYC_E_ROUTE_ARTIFACT",
            format!("cannot emit route artifact for unsupported method {method}"),
        )),
    }
}

fn checked_u32(value: usize, label: &str) -> Result<u32, Diagnostic> {
    u32::try_from(value).map_err(|_| {
        Diagnostic::new(
            "SLOPPYC_E_ROUTE_ARTIFACT",
            format!("route artifact {label} exceeds uint32"),
        )
    })
}

fn push_u32(bytes: &mut Vec<u8>, value: u32) {
    bytes.extend_from_slice(&value.to_le_bytes());
}

fn push_u64(bytes: &mut Vec<u8>, value: u64) {
    bytes.extend_from_slice(&value.to_le_bytes());
}

fn push_bytes(bytes: &mut Vec<u8>, value: &[u8]) {
    bytes.extend_from_slice(value);
}

fn fnv1a64_with_zeroed_checksum(bytes: &[u8]) -> u64 {
    let mut hash = FNV_OFFSET_BASIS;
    for (index, byte) in bytes.iter().enumerate() {
        let value = if (CHECKSUM_OFFSET..CHECKSUM_OFFSET + 8).contains(&index) {
            0
        } else {
            *byte
        };
        hash ^= u64::from(value);
        hash = hash.wrapping_mul(FNV_PRIME);
    }
    hash
}

struct RouteEntry {
    method: u32,
    handler_id: u32,
    pattern_offset: u32,
    pattern_len: u32,
    name_offset: u32,
    name_len: u32,
    strategy: u32,
    execution_kind: u32,
    param_count: u32,
    flags: u32,
}

#[cfg(test)]
mod tests {
    use super::route_artifact_pattern_metadata;

    #[test]
    fn artifact_pattern_metadata_uses_complete_braced_segments() {
        assert_eq!(
            route_artifact_pattern_metadata("/users/{id").unwrap(),
            (1, 0)
        );
        assert_eq!(
            route_artifact_pattern_metadata("/orgs/{org}/users/{id:int}").unwrap(),
            (2, 2)
        );
    }
}
