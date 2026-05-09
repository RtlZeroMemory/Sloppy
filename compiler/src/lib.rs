#![forbid(unsafe_code)]

pub mod bundle_emit;
pub mod capability_inference;
pub mod diagnostic;
pub mod effects;
pub mod fixtures;
mod framework_runtime;
pub mod graph;
mod hash;
pub mod module_graph;
pub mod parser;
pub mod plan_emit;
pub mod resolver;
pub mod result_inference;
pub mod schema_inference;
pub mod slop_dsl;
pub mod source;
pub mod source_map;
pub mod static_eval;
pub mod symbols;
pub mod validation;

mod sloppyc;
mod version;

pub use diagnostic::{Diagnostic, DiagnosticSeverity};
pub use sloppyc::{
    compile_file, compile_project, run, BundleOutput, CliExit, CompileOptions, CompileOutput,
    PlanOutput, SourceMapOutput,
};
