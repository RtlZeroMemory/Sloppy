# Configuration Model

Sloppy separates build/run selection from application behavior metadata.

At runtime, configuration is intentionally explicit:

- command mode selects source input or artifact input (`src/main.c`);
- source-input run defaults are explicit (`SL_RUN_DEFAULT_SOURCE_OUT_DIR`,
  default host `127.0.0.1`, default port `5173`, default environment
  `Development`);
- plan metadata is parsed and validated before startup (`src/core/plan_parse.c`,
  `src/core/app_host.c`).

The design goal is to avoid hidden ambient configuration contracts. Inputs should
be explicit, parseable, and validated up front.

The parser also enforces a redaction-oriented rule: plan fields matching
secret-like names are rejected (`app plan contains secret-bearing field` in
`src/core/plan_parse.c`). This keeps long-lived artifacts from becoming a secret
storage layer.
