# Ops Compiler Example

This example uses compiler-visible health and management configuration so Plan,
routes, doctor, audit, package, and run tooling can see the operations endpoints.

This is a test-only audit fixture. It intentionally leaves detailed management
endpoints unprotected in source so doctor and audit can report the exposure in
generated artifact checks. Recommended application code should use the protected
app-host pattern shown in `examples/ops-health-metrics-management`.
