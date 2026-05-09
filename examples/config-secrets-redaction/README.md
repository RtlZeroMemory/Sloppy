# Config Secrets Redaction Example

This example demonstrates `app.config.getSecret(...)`. The route returns only the redacted
display form so source examples and goldens never carry a raw secret value.

Build works without `AUTH_JWT_SECRET`. Set `AUTH_JWT_SECRET` before running
`sloppy doctor` or handler execution if you want the required secret config to
resolve. Do not replace the placeholder with a literal secret in
source-controlled files.
