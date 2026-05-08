# Framework v2 Controller Example

Status: Bootstrap controller API-shape example.

This example shows the implemented bootstrap `app.mapController(...)` surface, explicit
method mapping, and constructor injection through the same service provider used by route
handlers.

Current limits are intentional: controller class compiler extraction, decorators,
reflection-style scanning, and package discovery are deferred. This example is not a
`sloppyc` source-input example, does not emit `app.plan.json`, and does not claim
Node/Bun/Deno compatibility, production-readiness, or package-manager behavior.
