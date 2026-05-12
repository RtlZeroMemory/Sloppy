# Security Headers

`app.securityHeaders(options?)` installs first-party response security headers
for routes registered after the call. `app.useSecurityHeaders(...)` is an
alias.

```ts
app.securityHeaders({
  contentSecurityPolicy: "default-src 'self'",
  frameOptions: "deny",
  referrerPolicy: "no-referrer",
});
```

Default headers:

- `X-Content-Type-Options: nosniff`
- `X-Frame-Options: DENY`
- `Referrer-Policy: no-referrer`

`Strict-Transport-Security` is only emitted when `hsts: true` is configured and
the request connection is marked secure. Custom `Content-Security-Policy` and
`Permissions-Policy` values are passed through after validation as safe header
strings.

Security headers are middleware. Later route handlers can still set a header
explicitly; explicit response headers win over the baseline.
