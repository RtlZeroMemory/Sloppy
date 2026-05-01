use std::path::{Path, PathBuf};

use oxc_span::Span;

use crate::source::{display_path, line_column, source_frame};

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum DiagnosticSeverity {
    Error,
    Warning,
    Note,
}

impl DiagnosticSeverity {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Error => "error",
            Self::Warning => "warning",
            Self::Note => "note",
        }
    }
}

#[derive(Debug, Clone)]
pub struct Diagnostic {
    pub code: &'static str,
    pub severity: DiagnosticSeverity,
    pub path: Option<PathBuf>,
    pub span: Option<Span>,
    pub message: String,
    pub hint: Option<String>,
}

impl Diagnostic {
    pub fn new(code: &'static str, message: impl Into<String>) -> Self {
        Self {
            code,
            severity: DiagnosticSeverity::Error,
            path: None,
            span: None,
            message: message.into(),
            hint: None,
        }
    }

    pub fn with_path(mut self, path: &Path) -> Self {
        self.path = Some(path.to_path_buf());
        self
    }

    pub fn with_span(mut self, span: Span) -> Self {
        self.span = Some(span);
        self
    }

    pub fn with_hint(mut self, hint: impl Into<String>) -> Self {
        self.hint = Some(hint.into());
        self
    }

    pub fn render(&self, source: Option<&str>) -> String {
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
