# Time Conformance

This directory is the evidence index.
Time conformance is split by lane:

- default native diagnostics: `core.diagnostics.foundation` pins JSON goldens for timeout,
  cancellation, disposed timers, invalid delays, expired deadlines, interval overflow,
  skipped scheduled runs, and fake-clock misuse;
- default source examples: `examples.time.api_shape` checks the public source examples for
  delay, timeout, deadline, cancellation, interval, scheduled jobs, fake clocks, and
  filesystem deadline integration;
- bootstrap JavaScript stdlib evidence: `bootstrap.stdlib.app_host_foundation` covers
  deterministic delay/timeout/cancellation/deadline behavior, async iterable intervals,
  scheduled jobs, fake-clock advancement/disposal, and filesystem Time option behavior;
- V8-gated evidence: `conformance.v8.runtime_bridge` covers native delay settlement
  through the owner-thread scheduler and inactive `__sloppy.time` registration.

This is not Node timer compatibility, a global fake-timer system, a cron parser,
package-manager behavior, public release documentation, benchmark evidence, or unrelated
network/crypto/process implementation.
