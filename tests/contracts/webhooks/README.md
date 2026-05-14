# Webhooks Contracts

The PR-tier webhooks contract uses the real `Webhooks` API with `TestHttp.mock`
and a deterministic in-memory provider. It intentionally has no live network or
database dependency.

The provider double currently recognizes the SQL templates that
`stdlib/sloppy/webhooks.js` emits. That keeps this lane small and close to the
current storage contract, but a future storage adapter/test double boundary
would be less coupled to harmless SQL formatting changes.

`Retry-After` coverage uses the current delivery API's wall-clock scheduling.
The test asserts bounded behavior and verifies that an early delivery pass does
not claim the row. If delivery scheduling becomes fully clock-injectable, prefer
a fake clock here.
