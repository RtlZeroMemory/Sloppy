# Config Secrets Redaction Example

This example demonstrates `app.config.getSecret(...)`. The route returns only the redacted
display form so source examples and goldens never carry a raw secret value.

Set `AUTH_JWT_SECRET` locally when compiling/running this example. Do not replace the
placeholder with a literal secret in source-controlled files.
