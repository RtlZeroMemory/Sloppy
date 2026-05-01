//! Parse entrypoints for the supported Slop source subset.

use std::path::Path;

use oxc_span::SourceType;

use crate::diagnostic::Diagnostic;

pub fn source_type_for_path(path: &Path, context: ParseContext) -> Result<SourceType, Diagnostic> {
    SourceType::from_path(path).map_err(|_| {
        Diagnostic::new(context.unsupported_code(), context.unsupported_message()).with_path(path)
    })
}

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum ParseContext {
    Entry,
    Module,
}

impl ParseContext {
    fn unsupported_code(self) -> &'static str {
        "SLOPPYC_E_UNSUPPORTED_INPUT"
    }

    fn unsupported_message(self) -> &'static str {
        match self {
            Self::Entry => "compiler input must use a JavaScript file extension",
            Self::Module => {
                "module input must use a supported JavaScript or TypeScript file extension"
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_supported_source_extensions() {
        for name in ["app.js", "app.mjs", "app.ts"] {
            assert!(source_type_for_path(Path::new(name), ParseContext::Module).is_ok());
        }
    }

    #[test]
    fn rejects_unsupported_source_extensions_with_context() {
        let diagnostic =
            source_type_for_path(Path::new("app.txt"), ParseContext::Entry).unwrap_err();
        assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_INPUT");
        assert_eq!(
            diagnostic.message,
            "compiler input must use a JavaScript file extension"
        );
    }
}
