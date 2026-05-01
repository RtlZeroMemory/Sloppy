use std::path::Path;

use oxc_span::Span;

pub fn source_map_source_name(path: &Path) -> String {
    path.file_name()
        .and_then(|name| name.to_str())
        .map_or_else(|| "input.js".to_string(), ToOwned::to_owned)
}

pub fn line_column(source: &str, offset: u32) -> (usize, usize) {
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

pub fn source_frame(path: &Path, source: &str, span: Span) -> Option<String> {
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

pub fn display_path(path: &Path) -> String {
    path.to_string_lossy().replace('\\', "/")
}
