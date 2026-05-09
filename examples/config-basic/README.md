# Config Basic Example

This example demonstrates typed config binding, defaults, required values, and
secret redaction.

`AUTH_JWT_SECRET` is referenced as a placeholder and must not be committed as a literal
secret. Doctor/package metadata should show the required key and redaction category, not
the value.

## Limitations

This example focuses on local typed config and redaction metadata. Hot reload and external
secret-vault integrations are separate work.
