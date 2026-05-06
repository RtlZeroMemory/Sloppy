# V8 Bridge Test Template

Use this template for new stdlib V8 bridge tests. V8 evidence is `V8-gated` and must stay
separate from default non-V8 evidence.

Required cases:

1. Active feature path: a Plan that activates the feature registers the JS bridge and the
   public stdlib wrapper can complete the documented operation.
2. Inactive or missing feature diagnostic: a Plan without the feature reports the expected
   diagnostic before native work starts.
3. JS option validation before native work: invalid options throw JS-facing validation
   errors without entering the native backend.
4. JS error class mapping: native status/diagnostic classes map to stable JS error names,
   codes, and redacted messages.
5. Owner-thread settlement: Promise settlement happens on the V8 owner thread where the
   bridge has an async path.
6. No raw native handle exposure: no raw native handle, JS values must not expose native pointers, integer table
   indexes, or ownership tokens.
7. Redaction: JS-facing errors must not include secret values, credentials, connection
   strings, private keys, or passphrases.

Do not add a V8 test to the default non-V8 lane. If the bridge does not exist yet, add a
negative/inactive-feature diagnostic or defer the scenario with an issue reference.
