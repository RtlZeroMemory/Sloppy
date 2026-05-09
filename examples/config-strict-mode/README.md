# Config Strict Mode Example

This example keeps a required `Auth:Issuer` read visible to the compiler and doctor
without providing a value. It is intended for missing-config diagnostics and package
configuration checks.

Dynamic config keys and reload-on-change are not implemented by this example.
