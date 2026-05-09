# Time Deadline and Cancellation Example

This example shows the app-facing shape for shared
deadlines and caller cancellation.

`Deadline.after` carries a reusable time budget into filesystem work. `Time.timeout`
accepts the function-with-signal form so cancellation can be propagated into cooperative
work. `CancellationController` carries the caller's explicit reason.

Cancellation and deadlines are cooperative API contracts with stable error classes. They do
not preempt arbitrary in-flight work such as native filesystem operations.
