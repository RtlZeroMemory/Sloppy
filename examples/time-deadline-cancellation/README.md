# Time Deadline and Cancellation Example

Status: CORE-TIME-01.I source example. This example shows the app-facing shape for shared
deadlines and caller cancellation.

`Deadline.after` carries a reusable time budget into filesystem work. `Time.timeout`
accepts the function-with-signal form so cancellation can be propagated into cooperative
work. `CancellationController` carries the caller's explicit reason.

This example does not claim to cancel arbitrary already-running work or preempt native
filesystem calls. Cancellation and deadlines are cooperative API contracts with stable
error classes.
