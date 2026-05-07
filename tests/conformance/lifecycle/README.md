# Resource, Leak, Shutdown, And Late Completion Assertions

The resource/lifecycle conformance aliases expose existing default non-V8 resource and
app-host lifecycle tests as reviewable evidence for TEST-PLATFORM-01.

Current default tests cover:

- cleanup exactly once;
- stale handle rejection;
- double close behavior;
- wrong-kind handle preservation;
- request/app shutdown boundaries;
- shutdown while operation state is pending in the native helper layer;
- late completion counted as cleanup-only/rejected terminal work;
- no leaked Slop-owned resources where the current counters can count them.

CTest aliases:

- `conformance.foundation.resource_lifecycle`
- `conformance.foundation.app_host_lifecycle`

These aliases do not claim production monitoring, live provider operation counters, V8
execution, long stress/torture, or benchmark behavior.
